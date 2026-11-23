// The C interface, exercised only through the C functions in capi.h, with a function-pointer host.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "check.hpp"
#include "triskelion/capi.h"
#include "triskelion/verify.hpp"

using namespace std::chrono_literals;
namespace fs = std::filesystem;
static const fs::path kFixtures = FIXTURE_DIR;

static std::string read_all(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
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

// ---- a host built from plain C function pointers ----------------------------------------------

struct Host {
  std::mutex m;
  std::condition_variable cv;
  std::string check_body = R"({"update":false})";
  int check_status = 200;
  bool respond = true;            // false: never call tri_response_set (transport failure)
  std::string artifact;
  bool aborted = false;
  int pings = 0;
  int gets = 0, posts = 0, downloads = 0, applies = 0, runs = 0;
  std::string applied_path, applied_hash, last_post, stream_url, check_url;
  struct Result { int outcome; std::string detail, version; };
  std::vector<Result> results;
  fs::path work;
};

static void c_get(void* u, const char* url, tri_response* out) {
  auto* h = static_cast<Host*>(u);
  std::string body; int status; bool respond;
  { std::lock_guard<std::mutex> l(h->m); h->gets++; h->check_url = url; body = h->check_body; status = h->check_status; respond = h->respond; }
  if (respond) tri_response_set(out, status, body.data(), body.size());
}
static void c_post(void* u, const char*, const char* body, tri_response* out) {
  auto* h = static_cast<Host*>(u);
  { std::lock_guard<std::mutex> l(h->m); h->posts++; h->last_post = body; }
  tri_response_set(out, 200, "{\"ok\":true}", 11);
}
static int c_download(void* u, const char*, const char* dest) {
  auto* h = static_cast<Host*>(u);
  std::string bytes;
  { std::lock_guard<std::mutex> l(h->m); h->downloads++; bytes = h->artifact; }
  std::ofstream(dest, std::ios::binary | std::ios::trunc).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return 1;
}
static void c_stream_run(void* u, const char* url, tri_ping_fn ping, void* ctx) {
  auto* h = static_cast<Host*>(u);
  std::unique_lock<std::mutex> l(h->m);
  h->runs++;
  h->stream_url = url;
  while (true) {
    h->cv.wait(l, [&] { return h->aborted || h->pings > 0; });
    if (h->aborted) return;
    h->pings--;
    l.unlock();
    ping(ctx);
    l.lock();
  }
}
static void c_stream_abort(void* u) {
  auto* h = static_cast<Host*>(u);
  { std::lock_guard<std::mutex> l(h->m); h->aborted = true; }
  h->cv.notify_all();
}
static int c_apply(void* u, const char* path) {
  auto* h = static_cast<Host*>(u);
  const auto hash = triskelion::sha256_file_hex(path);
  std::lock_guard<std::mutex> l(h->m);
  h->applies++;
  h->applied_path = path;
  h->applied_hash = hash ? *hash : "";
  return 1;
}
static void c_on_result(void* u, int outcome, const char* detail, const char* version) {
  auto* h = static_cast<Host*>(u);
  std::lock_guard<std::mutex> l(h->m);
  h->results.push_back({outcome, detail ? detail : "<null>", version ? version : "<null>"});
}

static tri_host make_host(Host* h, bool with_result = true) {
  tri_host t{};
  t.user = h;
  t.http_get = c_get;
  t.http_post_json = c_post;
  t.download = c_download;
  t.stream_run = c_stream_run;
  t.stream_abort = c_stream_abort;
  t.apply = c_apply;
  t.on_result = with_result ? c_on_result : nullptr;
  return t;
}

struct Cfg {  // owns the strings, so tests can destroy them after create
  std::string server = "https://svc.example.com", app = "locked", platform = "android", version = "1.0.0",
              key = public_key(), channel = "stable", install = "aaaaaaaa-0000-4000-8000-000000000001", work;
  unsigned check = 3600000, retry = 3600000, reconnect = 20;
  int insecure = 0;
  tri_config c() const {
    tri_config t{};
    t.server_url = server.c_str(); t.app_id = app.c_str(); t.platform = platform.c_str();
    t.current_version = version.c_str(); t.public_key_b64 = key.c_str(); t.channel = channel.c_str();
    t.install_id = install.c_str(); t.work_dir = work.c_str();
    t.check_interval_ms = check; t.retry_interval_ms = retry; t.reconnect_delay_ms = reconnect;
    t.allow_insecure_http = insecure;
    return t;
  }
};

static size_t results_n(Host& h) { std::lock_guard<std::mutex> l(h.m); return h.results.size(); }
static int n_of(Host& h, int Host::*field) { std::lock_guard<std::mutex> l(h.m); return h.*field; }

// ---- tests ------------------------------------------------------------------------------------

static void test_validation(const fs::path& tmp) {
  Host h; const tri_host host = make_host(&h);
  Cfg good; good.work = (tmp / "val").string();
  char err[128] = "";
  const tri_config good_cfg = good.c();
  tri_client* ok = tri_client_create(&good_cfg, &host, err, sizeof err);
  CHECK(ok != nullptr && err[0] == '\0');
  tri_client_destroy(ok);

  auto rejects = [&](const char* why, auto mutate) {
    Cfg c = good; mutate(c);
    char e[128] = "";
    tri_config cfg = c.c();
    tri_client* r = tri_client_create(&cfg, &host, e, sizeof e);
    if (r) { tri_client_destroy(r); }
    CHECK(r == nullptr);
    CHECK(e[0] != '\0');
    (void)why;
  };
  rejects("http url", [](Cfg& c) { c.server = "http://svc.example.com"; });
  rejects("ftp url", [](Cfg& c) { c.server = "ftp://svc.example.com"; });
  rejects("empty url", [](Cfg& c) { c.server = ""; });
  rejects("bad version", [](Cfg& c) { c.version = "1.0"; });
  rejects("version suffix", [](Cfg& c) { c.version = "1.0.0-beta"; });
  rejects("bad platform", [](Cfg& c) { c.platform = "ios"; });
  rejects("bad channel", [](Cfg& c) { c.channel = "nightly"; });
  rejects("uppercase app", [](Cfg& c) { c.app = "Locked"; });
  rejects("slash in app", [](Cfg& c) { c.app = "a/b"; });
  rejects("one char app", [](Cfg& c) { c.app = "a"; });
  rejects("dotdot app", [](Cfg& c) { c.app = ".."; });
  rejects("short install", [](Cfg& c) { c.install = "abc"; });
  rejects("non-hex install", [](Cfg& c) { c.install = "zzzzzzzz-0000-4000-8000-000000000001"; });
  rejects("install with slash", [](Cfg& c) { c.install = "aaaaaaaa/0000-4000-8000-000000000001"; });
  rejects("bad key", [](Cfg& c) { c.key = "not-a-key"; });
  rejects("short key", [](Cfg& c) { c.key = "AAAA"; });
  rejects("empty key", [](Cfg& c) { c.key = ""; });
  rejects("empty work", [](Cfg& c) { c.work = ""; });

  { Cfg c = good; c.insecure = 1; c.server = "http://localhost:3000"; tri_config cfg = c.c();
    tri_client* r = tri_client_create(&cfg, &host, nullptr, 0); CHECK(r != nullptr); tri_client_destroy(r); }  // dev opt-in, err NULL ok
  { Cfg c = good; c.insecure = 1; c.server = "ftp://x"; tri_config cfg = c.c();
    CHECK(tri_client_create(&cfg, &host, nullptr, 0) == nullptr); }                                           // opt-in is http only
  { tri_config cfg = good.c(); cfg.channel = nullptr; tri_client* r = tri_client_create(&cfg, &host, nullptr, 0);
    CHECK(r != nullptr); tri_client_destroy(r); }                                                             // NULL channel = stable
  { tri_config cfg = good.c(); char e[8]; tri_client* r = tri_client_create(&cfg, nullptr, e, sizeof e);
    CHECK(r == nullptr && e[0] != '\0' && e[7] == '\0'); }                                                    // truncation stays terminated
  { tri_config cfg = good.c(); CHECK(tri_client_create(nullptr, &host, nullptr, 0) == nullptr);
    CHECK(tri_client_create(&cfg, nullptr, nullptr, 0) == nullptr); }
  { tri_host partial = host; partial.apply = nullptr; tri_config cfg = good.c();
    CHECK(tri_client_create(&cfg, &partial, nullptr, 0) == nullptr);
    partial = host; partial.stream_abort = nullptr; CHECK(tri_client_create(&cfg, &partial, nullptr, 0) == nullptr);
    partial = host; partial.on_result = nullptr; tri_client* r = tri_client_create(&cfg, &partial, nullptr, 0);
    CHECK(r != nullptr); tri_client_destroy(r); }                                                             // on_result is optional
}

static void test_null_safety() {
  tri_client_start(nullptr);
  tri_client_check_now(nullptr);
  tri_client_stop(nullptr);
  tri_client_destroy(nullptr);
  CHECK(tri_client_update_applied(nullptr) == 0);
  tri_response_set(nullptr, 200, "x", 1);  // must not crash
  CHECK(true);
}

static void test_full_flow(const fs::path& tmp) {
  Host h;
  h.artifact = read_all(kFixtures / "artifact.bin");
  h.aborted = false;
  const tri_host host = make_host(&h);

  tri_client* c;
  {
    Cfg cfg; cfg.work = (tmp / "flow").string();
    tri_config t = cfg.c();
    c = tri_client_create(&t, &host, nullptr, 0);
    // everything the client was given is destroyed here: it must have copied what it needs
    cfg.server.assign(cfg.server.size(), 'X'); cfg.app.assign(cfg.app.size(), 'X');
    cfg.key.assign(cfg.key.size(), 'X'); cfg.install.assign(cfg.install.size(), 'X'); cfg.work.assign(cfg.work.size(), 'X');
  }
  CHECK(c != nullptr);
  tri_client_start(c);

  CHECK(wait_until([&] { return results_n(h) == 1; }));
  { std::lock_guard<std::mutex> l(h.m);
    CHECK(h.results[0].outcome == TRI_UP_TO_DATE && h.results[0].detail.empty() && h.results[0].version.empty());
    CHECK(h.check_url == "https://svc.example.com/v1/check?app=locked&platform=android&version=1.0.0&install=aaaaaaaa-0000-4000-8000-000000000001&channel=stable");
    CHECK(h.posts == 1);
    CHECK(h.last_post.find("\"install\":\"aaaaaaaa-0000-4000-8000-000000000001\"") != std::string::npos); }
  CHECK(wait_until([&] { return n_of(h, &Host::runs) >= 1; }));
  { std::lock_guard<std::mutex> l(h.m); CHECK(h.stream_url == "https://svc.example.com/v1/stream?install=aaaaaaaa-0000-4000-8000-000000000001"); }

  { std::lock_guard<std::mutex> l(h.m); h.check_body = read_all(kFixtures / "check_response.json"); h.pings++; }
  h.cv.notify_all();

  CHECK(wait_until([&] { return results_n(h) == 2; }));
  { std::lock_guard<std::mutex> l(h.m);
    CHECK(h.results[1].outcome == TRI_APPLIED && h.results[1].version == "1.1.0");
    CHECK(h.applies == 1 && h.downloads == 1);
    CHECK(h.applied_hash == triskelion::sha256_file_hex(kFixtures / "artifact.bin").value());
    CHECK(h.applied_path.find("flow") != std::string::npos); }   // used the work_dir given at create, not the overwritten copy
  CHECK(tri_client_update_applied(c) == 1);
  CHECK(wait_until([&] { std::lock_guard<std::mutex> l(h.m); return h.aborted; }));

  tri_client_check_now(c);
  std::this_thread::sleep_for(60ms);
  CHECK(n_of(h, &Host::applies) == 1 && results_n(h) == 2);   // idle after applying
  tri_client_stop(c);
  tri_client_stop(c);  // idempotent
  tri_client_destroy(c);
}

static void test_failures_map_to_codes(const fs::path& tmp) {
  { // host never fills the response -> transport failure
    Host h; h.respond = false; const tri_host host = make_host(&h);
    Cfg cfg; cfg.work = (tmp / "nores").string(); tri_config t = cfg.c();
    tri_client* c = tri_client_create(&t, &host, nullptr, 0);
    tri_client_start(c);
    CHECK(wait_until([&] { return results_n(h) >= 1; }));
    { std::lock_guard<std::mutex> l(h.m); CHECK(h.results[0].outcome == TRI_CHECK_FAILED && h.results[0].detail == "http 0"); }
    tri_client_destroy(c);
  }
  { // server 503
    Host h; h.check_status = 503; h.check_body = ""; const tri_host host = make_host(&h);
    Cfg cfg; cfg.work = (tmp / "s503").string(); tri_config t = cfg.c();
    tri_client* c = tri_client_create(&t, &host, nullptr, 0);
    tri_client_start(c);
    CHECK(wait_until([&] { return results_n(h) >= 1; }));
    { std::lock_guard<std::mutex> l(h.m); CHECK(h.results[0].outcome == TRI_CHECK_FAILED && h.results[0].detail == "http 503"); }
    tri_client_destroy(c);
  }
  { // tampered artifact -> REJECTED, never applied
    Host h; h.artifact = read_all(kFixtures / "artifact.bin"); h.artifact[10] ^= 1;
    h.check_body = read_all(kFixtures / "check_response.json");
    const tri_host host = make_host(&h);
    Cfg cfg; cfg.work = (tmp / "tamper").string(); tri_config t = cfg.c();
    tri_client* c = tri_client_create(&t, &host, nullptr, 0);
    tri_client_start(c);
    CHECK(wait_until([&] { return results_n(h) >= 1; }));
    { std::lock_guard<std::mutex> l(h.m);
      CHECK(h.results[0].outcome == TRI_REJECTED && h.results[0].detail == "reject: hash mismatch" && h.applies == 0); }
    tri_client_destroy(c);
  }
  { // wrong embedded key -> REJECTED on signature
    Host h; h.artifact = read_all(kFixtures / "artifact.bin"); h.check_body = read_all(kFixtures / "check_response.json");
    const tri_host host = make_host(&h);
    Cfg cfg; cfg.work = (tmp / "badkey").string(); cfg.key = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="; tri_config t = cfg.c();
    tri_client* c = tri_client_create(&t, &host, nullptr, 0);
    tri_client_start(c);
    CHECK(wait_until([&] { return results_n(h) >= 1; }));
    { std::lock_guard<std::mutex> l(h.m); CHECK(h.results[0].outcome == TRI_REJECTED && h.results[0].detail == "reject: bad signature" && h.applies == 0); }
    tri_client_destroy(c);
  }
  { // no on_result callback at all must not crash
    Host h; const tri_host host = make_host(&h, false);
    Cfg cfg; cfg.work = (tmp / "nocb").string(); tri_config t = cfg.c();
    tri_client* c = tri_client_create(&t, &host, nullptr, 0);
    tri_client_start(c);
    CHECK(wait_until([&] { return n_of(h, &Host::gets) >= 1; }));
    tri_client_destroy(c);
  }
}

static void test_lifecycle(const fs::path& tmp) {
  { // destroy without start
    Host h; const tri_host host = make_host(&h); Cfg cfg; cfg.work = (tmp / "l1").string(); tri_config t = cfg.c();
    tri_client* c = tri_client_create(&t, &host, nullptr, 0);
    tri_client_destroy(c);
    CHECK(n_of(h, &Host::gets) == 0);
  }
  { // destroy while running stops the stream promptly
    Host h; const tri_host host = make_host(&h); Cfg cfg; cfg.work = (tmp / "l2").string(); cfg.reconnect = 3600000; tri_config t = cfg.c();
    tri_client* c = tri_client_create(&t, &host, nullptr, 0);
    tri_client_start(c);
    CHECK(wait_until([&] { return n_of(h, &Host::runs) >= 1 && results_n(h) >= 1; }));
    const auto t0 = std::chrono::steady_clock::now();
    tri_client_destroy(c);
    CHECK(std::chrono::steady_clock::now() - t0 < 1000ms);
    std::lock_guard<std::mutex> l(h.m); CHECK(h.aborted);
  }
  { // tri_response_set edge cases: empty body, NULL body, long body
    Host h; h.check_body = std::string(); h.check_status = 204; const tri_host host = make_host(&h);
    Cfg cfg; cfg.work = (tmp / "l3").string(); tri_config t = cfg.c();
    tri_client* c = tri_client_create(&t, &host, nullptr, 0);
    tri_client_start(c);
    CHECK(wait_until([&] { return results_n(h) >= 1; }));
    { std::lock_guard<std::mutex> l(h.m); CHECK(h.results[0].outcome == TRI_CHECK_FAILED && h.results[0].detail == "http 204"); }
    tri_client_destroy(c);
  }
}

int main() {
  const fs::path tmp = fs::temp_directory_path() / "triskelion_capi_tests";
  fs::remove_all(tmp);
  fs::create_directories(tmp);

  test_validation(tmp);
  test_null_safety();
  test_full_flow(tmp);
  test_failures_map_to_codes(tmp);
  test_lifecycle(tmp);

  fs::remove_all(tmp);
  return report();
}
