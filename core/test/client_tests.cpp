// Client: timer, broadcast pings, retry, registration, idle-after-apply and clean shutdown,
// driven by thread-safe fakes. Real threads, tiny intervals, polling with generous timeouts.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "check.hpp"
#include "triskelion/client.hpp"
#include "triskelion/json.hpp"

using namespace triskelion;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

static const fs::path kFixtures = FIXTURE_DIR;

static std::string read_all(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
static void write_all(const fs::path& p, const std::string& bytes) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
static std::string public_key() {
  std::ifstream in(kFixtures / "vector.txt");
  std::string line;
  while (std::getline(in, line))
    if (line.rfind("publicKey=", 0) == 0) return line.substr(10);
  return "";
}

template <class Pred>
static bool wait_until(Pred pred, std::chrono::milliseconds limit = 5000ms) {
  const auto end = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < end) {
    if (pred()) return true;
    std::this_thread::sleep_for(3ms);
  }
  return pred();
}

// ---- fakes (every field the test thread touches is behind a mutex) ----------------------------

struct FakeHttp : Http {
  mutable std::mutex m;
  HttpResponse check_reply;
  HttpResponse register_reply{200, "{\"ok\":true}"};
  std::string download_bytes;
  int register_fail_times = 0;  // first N registrations get a 500
  int gets = 0, posts = 0, downloads = 0;

  void set_check(HttpResponse r) { std::lock_guard<std::mutex> l(m); check_reply = std::move(r); }
  int n_gets() const { std::lock_guard<std::mutex> l(m); return gets; }
  int n_posts() const { std::lock_guard<std::mutex> l(m); return posts; }
  int n_downloads() const { std::lock_guard<std::mutex> l(m); return downloads; }

  HttpResponse get(const std::string&) override {
    std::lock_guard<std::mutex> l(m);
    gets++;
    return check_reply;
  }
  HttpResponse post_json(const std::string&, const std::string&) override {
    std::lock_guard<std::mutex> l(m);
    posts++;
    if (register_fail_times > 0) { register_fail_times--; return {500, ""}; }
    return register_reply;
  }
  bool download(const std::string&, const fs::path& dest) override {
    std::string bytes;
    { std::lock_guard<std::mutex> l(m); downloads++; bytes = download_bytes; }
    write_all(dest, bytes);
    return true;
  }
};

struct FakeApplier : Applier {
  std::atomic<int> calls{0};
  bool apply(const fs::path&) override { calls++; return true; }
};

// Blocks in run() until pinged or aborted. Abort is sticky, as the contract requires.
struct FakeStream : Stream {
  std::mutex m;
  std::condition_variable cv;
  bool aborted = false;
  int pending_pings = 0;
  int fail_next_runs = 0;  // return immediately, like a dropped connection
  int runs = 0;
  std::string last_url;

  void ping() { { std::lock_guard<std::mutex> l(m); pending_pings++; } cv.notify_all(); }
  int n_runs() { std::lock_guard<std::mutex> l(m); return runs; }
  bool is_aborted() { std::lock_guard<std::mutex> l(m); return aborted; }
  std::string url() { std::lock_guard<std::mutex> l(m); return last_url; }

  void run(const std::string& url, const std::function<void()>& on_update) override {
    std::unique_lock<std::mutex> l(m);
    runs++;
    last_url = url;
    if (aborted) return;
    if (fail_next_runs > 0) { fail_next_runs--; return; }
    while (true) {
      cv.wait(l, [&] { return aborted || pending_pings > 0; });
      if (aborted) return;
      pending_pings--;
      l.unlock();
      on_update();
      l.lock();
    }
  }
  void abort() override { { std::lock_guard<std::mutex> l(m); aborted = true; } cv.notify_all(); }
};

struct Results {
  std::mutex m;
  std::vector<RunResult> all;
  void add(const RunResult& r) { std::lock_guard<std::mutex> l(m); all.push_back(r); }
  size_t count() { std::lock_guard<std::mutex> l(m); return all.size(); }
  size_t count_of(Outcome o) { std::lock_guard<std::mutex> l(m); size_t n = 0; for (auto& r : all) if (r.outcome == o) n++; return n; }
};

static UpdateConfig make_cfg(const fs::path& work) {
  UpdateConfig c;
  c.base_url = "https://svc.example.com";
  c.policy = Policy{"locked", "android", "1.0.0", public_key()};
  c.install_id = "aaaaaaaa-0000-4000-8000-000000000001";
  c.work_dir = work;
  return c;
}

static const std::string kUpToDate = R"({"update":false})";

struct Rig {
  FakeHttp http;
  FakeStream stream;
  FakeApplier applier;
  Results results;
  fs::path work;
  Rig(const fs::path& tmp, const char* name) : work(tmp / name) {
    fs::remove_all(work);
    http.check_reply = {200, kUpToDate};
    http.download_bytes = read_all(kFixtures / "artifact.bin");
  }
  Client make(ClientOptions o) {
    return Client(make_cfg(work), http, stream, applier, o, [this](const RunResult& r) { results.add(r); });
  }
};

static ClientOptions opts(int check_ms, int retry_ms, int reconnect_ms) {
  return ClientOptions{std::chrono::milliseconds(check_ms), std::chrono::milliseconds(retry_ms), std::chrono::milliseconds(reconnect_ms)};
}

// ---- tests ------------------------------------------------------------------------------------

static void test_launch_check_and_register(const fs::path& tmp) {
  Rig rig(tmp, "launch");
  Client c = rig.make(opts(3600000, 3600000, 20));
  c.start();
  CHECK(wait_until([&] { return rig.results.count() >= 1; }));
  CHECK(rig.results.count_of(Outcome::UpToDate) >= 1);
  CHECK(rig.http.n_posts() == 1);  // registered exactly once
  CHECK(wait_until([&] { return rig.stream.n_runs() >= 1; }));
  CHECK(rig.stream.url() == "https://svc.example.com/v1/stream?install=aaaaaaaa-0000-4000-8000-000000000001");
  c.stop();
  CHECK(!c.update_applied());
}

static void test_ping_triggers_check_and_apply(const fs::path& tmp) {
  Rig rig(tmp, "ping");
  Client c = rig.make(opts(3600000, 3600000, 20));  // timer is effectively off
  c.start();
  CHECK(wait_until([&] { return rig.results.count() == 1; }));
  const int gets_before = rig.http.n_gets();

  rig.http.set_check({200, read_all(kFixtures / "check_response.json")});
  CHECK(wait_until([&] { return rig.stream.n_runs() >= 1; }));
  rig.stream.ping();

  CHECK(wait_until([&] { return rig.results.count_of(Outcome::Applied) == 1; }));
  CHECK(rig.http.n_gets() == gets_before + 1);
  CHECK(rig.applier.calls == 1);
  CHECK(c.update_applied());
  CHECK(wait_until([&] { return rig.stream.is_aborted(); }));  // listener is shut down after applying

  // idle for good: more pings and check_now change nothing
  const int gets_after = rig.http.n_gets();
  rig.stream.ping();
  c.check_now();
  std::this_thread::sleep_for(80ms);
  CHECK(rig.http.n_gets() == gets_after && rig.applier.calls == 1 && rig.results.count() == 2);
  c.stop();
}

static void test_timer(const fs::path& tmp) {
  Rig rig(tmp, "timer");
  Client c = rig.make(opts(25, 25, 20));
  c.start();
  CHECK(wait_until([&] { return rig.http.n_gets() >= 4; }));
  c.stop();
}

static void test_check_now(const fs::path& tmp) {
  Rig rig(tmp, "checknow");
  Client c = rig.make(opts(3600000, 3600000, 20));
  c.start();
  CHECK(wait_until([&] { return rig.results.count() == 1; }));
  c.check_now();
  CHECK(wait_until([&] { return rig.results.count() == 2; }));
  c.stop();
}

static void test_retry_after_failure(const fs::path& tmp) {
  Rig rig(tmp, "retry");
  rig.http.set_check({503, ""});
  Client c = rig.make(opts(3600000, 25, 20));  // failures retry fast, routine checks do not
  c.start();
  CHECK(wait_until([&] { return rig.results.count_of(Outcome::CheckFailed) >= 3; }));
  rig.http.set_check({200, read_all(kFixtures / "check_response.json")});
  CHECK(wait_until([&] { return rig.results.count_of(Outcome::Applied) == 1; }));
  c.stop();
}

static void test_register_retries_until_ok(const fs::path& tmp) {
  Rig rig(tmp, "register");
  rig.http.register_fail_times = 2;
  Client c = rig.make(opts(25, 25, 20));
  c.start();
  CHECK(wait_until([&] { return rig.http.n_posts() >= 3; }));
  std::this_thread::sleep_for(100ms);
  CHECK(rig.http.n_posts() == 3);  // two failures, one success, then never again
  c.stop();
}

static void test_rejected_does_not_hammer(const fs::path& tmp) {
  Rig rig(tmp, "rejected");
  rig.http.set_check({200, read_all(kFixtures / "check_response.json")});
  rig.http.download_bytes[100] ^= 0x01;  // tampered artifact
  Client c = rig.make(opts(3600000, 10, 20));  // retry is 10 ms, but a rejection must wait the long interval
  c.start();
  CHECK(wait_until([&] { return rig.results.count_of(Outcome::Rejected) == 1; }));
  std::this_thread::sleep_for(200ms);
  CHECK(rig.http.n_gets() == 1 && rig.http.n_downloads() == 1 && rig.applier.calls == 0);
  c.stop();
}

static void test_stream_reconnects(const fs::path& tmp) {
  Rig rig(tmp, "reconnect");
  rig.stream.fail_next_runs = 2;
  Client c = rig.make(opts(3600000, 3600000, 15));
  c.start();
  CHECK(wait_until([&] { return rig.stream.n_runs() >= 3; }));
  rig.stream.ping();  // the third connection is a good one
  CHECK(wait_until([&] { return rig.results.count() >= 2; }));
  c.stop();
}

static void test_callback_exception_is_survivable(const fs::path& tmp) {
  Rig rig(tmp, "throwing");
  std::atomic<int> calls{0};
  Client c(make_cfg(rig.work), rig.http, rig.stream, rig.applier, opts(3600000, 3600000, 20),
           [&](const RunResult&) { if (calls++ == 0) throw 42; });
  c.start();
  CHECK(wait_until([&] { return calls >= 1; }));
  c.check_now();
  CHECK(wait_until([&] { return calls >= 2; }));  // worker is still alive after the throw
  c.stop();
}

static void test_lifecycle(const fs::path& tmp) {
  { // destructor without start, stop without start, double stop, start after stop
    Rig rig(tmp, "life1");
    Client c = rig.make(opts(25, 25, 20));
    c.stop();
    c.stop();
    c.start();  // no effect after stop
    CHECK(rig.http.n_gets() == 0);
  }
  { // start twice does not spawn twice
    Rig rig(tmp, "life2");
    Client c = rig.make(opts(3600000, 3600000, 20));
    c.start();
    c.start();
    CHECK(wait_until([&] { return rig.results.count() >= 1; }));
    std::this_thread::sleep_for(60ms);
    CHECK(rig.results.count() == 1 && rig.http.n_posts() == 1);
    c.stop();
  }
  { // stop() returns promptly even while the stream is blocked and the worker is asleep on a long timer
    Rig rig(tmp, "life3");
    Client c = rig.make(opts(3600000, 3600000, 3600000));
    c.start();
    CHECK(wait_until([&] { return rig.stream.n_runs() >= 1 && rig.results.count() >= 1; }));
    const auto t0 = std::chrono::steady_clock::now();
    c.stop();
    CHECK(std::chrono::steady_clock::now() - t0 < 1000ms);
  }
  { // stop() racing a listener that has not reached run() yet must not hang
    for (int i = 0; i < 50; i++) {
      Rig rig(tmp, "life4");
      Client c = rig.make(opts(3600000, 3600000, 20));
      c.start();
      c.stop();
    }
    CHECK(true);
  }
  { // destructor stops a running client
    Rig rig(tmp, "life5");
    {
      Client c = rig.make(opts(3600000, 3600000, 20));
      c.start();
      CHECK(wait_until([&] { return rig.results.count() >= 1; }));
    }
    CHECK(rig.stream.is_aborted());
  }
}

int main() {
  const fs::path tmp = fs::temp_directory_path() / "triskelion_client_tests";
  fs::remove_all(tmp);
  fs::create_directories(tmp);

  test_launch_check_and_register(tmp);
  test_ping_triggers_check_and_apply(tmp);
  test_timer(tmp);
  test_check_now(tmp);
  test_retry_after_failure(tmp);
  test_register_retries_until_ok(tmp);
  test_rejected_does_not_hammer(tmp);
  test_stream_reconnects(tmp);
  test_callback_exception_is_survivable(tmp);
  test_lifecycle(tmp);

  fs::remove_all(tmp);
  return report();
}
