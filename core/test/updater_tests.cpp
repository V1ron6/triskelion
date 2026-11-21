// Parser, check handling and the full check -> download -> decide -> apply flow, with fake Http/Applier.
// The check response fixture is produced by Node's JSON.stringify, the same serializer the server uses.
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "triskelion/json.hpp"
#include "triskelion/updater.hpp"
#include "triskelion/version.hpp"

using namespace triskelion;
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
static std::string replace_first(std::string s, const std::string& from, const std::string& to) {
  const auto at = s.find(from);
  if (at != std::string::npos) s.replace(at, from.size(), to);
  return s;
}
static std::string str_of(const JsonObject& o, const char* k) { return std::get<std::string>(o.at(k)); }

static std::string public_key() {
  std::ifstream in(kFixtures / "vector.txt");
  std::string line;
  while (std::getline(in, line))
    if (line.rfind("publicKey=", 0) == 0) return line.substr(10);
  return "";
}

// ---- JSON -------------------------------------------------------------------------------------

static void test_json() {
  const auto ok = parse_flat_json(R"({"a":"x","b":12,"c":true,"d":null,"e":-5,"f":false})");
  CHECK(ok && ok->size() == 6);
  CHECK(ok && str_of(*ok, "a") == "x");
  CHECK(ok && std::get<std::int64_t>(ok->at("b")) == 12 && std::get<std::int64_t>(ok->at("e")) == -5);
  CHECK(ok && std::get<bool>(ok->at("c")) && !std::get<bool>(ok->at("f")));
  CHECK(ok && std::holds_alternative<JsonNull>(ok->at("d")));
  CHECK(parse_flat_json(" { } ") && parse_flat_json(" { } ")->empty());
  CHECK(parse_flat_json("{\"n\":9007199254740993}") &&
        std::get<std::int64_t>(parse_flat_json("{\"n\":9007199254740993}")->at("n")) == 9007199254740993LL);

  const auto esc = parse_flat_json(R"({"s":"a\"b\\c\/d\b\f\n\r\t","e":"\u00e9","p":"\ud83d\ude80","z":"\u0000"})");
  CHECK(esc && str_of(*esc, "s") == std::string("a\"b\\c/d\b\f\n\r\t"));
  CHECK(esc && str_of(*esc, "e") == "\xC3\xA9");
  CHECK(esc && str_of(*esc, "p") == "\xF0\x9F\x9A\x80");  // surrogate pair -> 4-byte UTF-8
  CHECK(esc && str_of(*esc, "z") == std::string(1, '\0'));

  const std::vector<std::string> bad = {
      "", "{", "}", "[]", "\"x\"", "{\"a\":{}}", "{\"a\":[1]}", "{\"a\":1.5}", "{\"a\":1e3}", "{\"a\":01}", "{\"a\":-}",
      "{\"a\":1,\"a\":2}", "{\"a\":\"x\"} trailing", "{\"a\":\"x\"", "{\"a\":\"\\q\"}", "{\"a\":\"\\ud83d\"}",
      "{\"a\":\"\\ude80\"}", "{\"a\":\"\\ud83dx\"}", "{\"a\":\"\\u12\"}", "{\"a\":\"line\nbreak\"}", "{a:1}", "{\"a\":1,}",
      "{\"a\":123456789012345678901}", "{\"a\" 1}", "{\"a\":tru}", "{\"a\":nul}", std::string("{\"a\":\"") + std::string(70000, 'x') + "\"}",
  };
  for (const auto& b : bad) CHECK(!parse_flat_json(b));

  // json_quote output must parse back to the same bytes
  const std::string nasty = "q\" b\\ n\n r\r t\t ctl\x01 \xC3\xA9 \xF0\x9F\x9A\x80";
  const auto rt = parse_flat_json("{\"k\":" + json_quote(nasty) + "}");
  CHECK(rt && str_of(*rt, "k") == nasty);

  // real server-shaped output from Node
  const auto real = parse_flat_json(read_all(kFixtures / "check_response.json"));
  CHECK(real && str_of(*real, "changelog") == read_all(kFixtures / "changelog.txt"));
}

// ---- check parsing ----------------------------------------------------------------------------

static Policy policy_at(const std::string& version) { return Policy{"locked", "android", version, public_key()}; }

static void test_parse_check() {
  const Policy pol = policy_at("1.0.0");
  const std::string real = read_all(kFixtures / "check_response.json");

  const auto r = parse_check_response(pol, {200, real});
  CHECK(r.kind == CheckResult::Kind::Update);
  CHECK(r.info.manifest.app == "locked" && r.info.manifest.platform == "android");
  CHECK(r.info.manifest.version == "1.1.0" && r.info.manifest.size == 4096);
  CHECK(!r.info.mandatory);
  CHECK(r.info.url.rfind("https://r2.example/", 0) == 0);
  CHECK(r.info.changelog == read_all(kFixtures / "changelog.txt"));
  CHECK(verify_manifest(pol.public_key_b64, r.info.manifest));  // parsed straight into something verifiable

  CHECK(parse_check_response(pol, {200, R"({"update":false})"}).kind == CheckResult::Kind::NoUpdate);

  for (const int status : {0, 204, 400, 404, 429, 500, 503}) CHECK(parse_check_response(pol, {status, real}).kind == CheckResult::Kind::Error);
  for (const char* body : {"", "not json", "[]", "{}", R"({"update":"yes"})", R"({"update":1})", R"({"update":true})"})
    CHECK(parse_check_response(pol, {200, body}).kind == CheckResult::Kind::Error);

  CHECK(parse_check_response(pol, {200, replace_first(real, "\"size\":4096", "\"size\":0")}).kind == CheckResult::Kind::Error);
  CHECK(parse_check_response(pol, {200, replace_first(real, "\"size\":4096", "\"size\":-4")}).kind == CheckResult::Kind::Error);
  CHECK(parse_check_response(pol, {200, replace_first(real, "\"size\":4096", "\"size\":\"4096\"")}).kind == CheckResult::Kind::Error);
  CHECK(parse_check_response(pol, {200, replace_first(real, "\"version\":\"1.1.0\"", "\"version\":\"../../evil\"")}).kind == CheckResult::Kind::Error);
  CHECK(parse_check_response(pol, {200, replace_first(real, "\"version\":\"1.1.0\"", "\"version\":\"1.1\"")}).kind == CheckResult::Kind::Error);

  // The server cannot choose the target: extra app/platform fields are ignored.
  const auto sneaky = parse_check_response(pol, {200, replace_first(real, "{", R"({"app":"evil","platform":"windows",)")});
  CHECK(sneaky.kind == CheckResult::Kind::Update && sneaky.info.manifest.app == "locked" && sneaky.info.manifest.platform == "android");
}

static void test_urls_and_register() {
  UpdateConfig cfg;
  cfg.base_url = "https://svc.example.com//";
  cfg.policy = policy_at("1.0.0");
  cfg.install_id = "id with/odd&chars";
  cfg.channel = "beta";
  CHECK(build_check_url(cfg) ==
        "https://svc.example.com/v1/check?app=locked&platform=android&version=1.0.0&install=id%20with%2Fodd%26chars&channel=beta");
  CHECK(url_encode("aZ09-._~") == "aZ09-._~" && url_encode(" /?&=%") == "%20%2F%3F%26%3D%25");

  const auto body = parse_flat_json(build_register_body(cfg));
  CHECK(body && str_of(*body, "install") == cfg.install_id && str_of(*body, "app") == "locked" &&
        str_of(*body, "platform") == "android" && str_of(*body, "version") == "1.0.0" && str_of(*body, "channel") == "beta");
  CHECK(body && body->count("pushToken") == 0);
  const auto with_token = parse_flat_json(build_register_body(cfg, "tok\"en"));
  CHECK(with_token && str_of(*with_token, "pushToken") == "tok\"en");
}

// ---- full flow with fakes ---------------------------------------------------------------------

struct FakeHttp : Http {
  HttpResponse check_reply;
  HttpResponse register_reply{200, "{\"ok\":true}"};
  bool download_ok = true;
  std::string download_bytes;  // what "R2" serves
  std::vector<std::string> gets, posts, downloads;
  std::string last_post_body;

  HttpResponse get(const std::string& url) override { gets.push_back(url); return check_reply; }
  HttpResponse post_json(const std::string& url, const std::string& body) override {
    posts.push_back(url);
    last_post_body = body;
    return register_reply;
  }
  bool download(const std::string& url, const fs::path& dest) override {
    downloads.push_back(url);
    if (!download_ok) { write_all(dest, "partial"); return false; }  // a real failure can leave a partial file
    write_all(dest, download_bytes);
    return true;
  }
};

struct FakeApplier : Applier {
  bool ok = true;
  int calls = 0;
  std::string hash_when_applied;
  bool apply(const fs::path& f) override {
    calls++;
    if (const auto h = sha256_file_hex(f)) hash_when_applied = *h;
    return ok;
  }
};

static bool dir_empty(const fs::path& d) { return !fs::exists(d) || fs::is_empty(d); }

static void test_run_update(const fs::path& tmp) {
  const std::string real = read_all(kFixtures / "check_response.json");
  const std::string artifact = read_all(kFixtures / "artifact.bin");
  const auto expected_hash = *sha256_file_hex(kFixtures / "artifact.bin");

  auto make = [&](const std::string& current, const fs::path& work) {
    UpdateConfig c;
    c.base_url = "https://svc.example.com";
    c.policy = policy_at(current);
    c.install_id = "aaaaaaaa-0000-4000-8000-000000000001";
    c.work_dir = work;
    return c;
  };
  auto fresh = [&](const char* name) { const fs::path w = tmp / name; fs::remove_all(w); return w; };

  { // happy path
    FakeHttp http; http.check_reply = {200, real}; http.download_bytes = artifact;
    FakeApplier app;
    const auto cfg = make("1.0.0", fresh("happy"));
    const auto r = run_update(cfg, http, app);
    CHECK(r.outcome == Outcome::Applied);
    CHECK(app.calls == 1 && app.hash_when_applied == expected_hash);
    CHECK(http.gets.size() == 1 && http.gets[0] == build_check_url(cfg));
    CHECK(http.downloads.size() == 1 && http.downloads[0] == r.info.url);
    CHECK(r.info.manifest.version == "1.1.0");
  }
  { // nothing to do
    FakeHttp http; http.check_reply = {200, R"({"update":false})"};
    FakeApplier app;
    const auto r = run_update(make("1.0.0", fresh("none")), http, app);
    CHECK(r.outcome == Outcome::UpToDate && http.downloads.empty() && app.calls == 0);
  }
  { // server error
    FakeHttp http; http.check_reply = {503, ""};
    FakeApplier app;
    const auto r = run_update(make("1.0.0", fresh("503")), http, app);
    CHECK(r.outcome == Outcome::CheckFailed && http.downloads.empty() && app.calls == 0);
  }
  { // transport failure during download leaves nothing behind
    FakeHttp http; http.check_reply = {200, real}; http.download_ok = false;
    FakeApplier app;
    const auto work = fresh("dlfail");
    const auto r = run_update(make("1.0.0", work), http, app);
    CHECK(r.outcome == Outcome::DownloadFailed && app.calls == 0 && dir_empty(work));
  }
  { // one flipped byte in the download
    FakeHttp http; http.check_reply = {200, real}; http.download_bytes = artifact; http.download_bytes[100] ^= 0x01;
    FakeApplier app;
    const auto work = fresh("flip");
    const auto r = run_update(make("1.0.0", work), http, app);
    CHECK(r.outcome == Outcome::Rejected && r.decision == Decision::RejectHashMismatch && app.calls == 0 && dir_empty(work));
  }
  { // truncated download
    FakeHttp http; http.check_reply = {200, real}; http.download_bytes = artifact.substr(0, 4000);
    FakeApplier app;
    const auto r = run_update(make("1.0.0", fresh("trunc")), http, app);
    CHECK(r.outcome == Outcome::Rejected && r.decision == Decision::RejectSizeMismatch && app.calls == 0);
  }
  { // server offers the version we already run
    FakeHttp http; http.check_reply = {200, real}; http.download_bytes = artifact;
    FakeApplier app;
    const auto r = run_update(make("1.1.0", fresh("same")), http, app);
    CHECK(r.outcome == Outcome::CheckFailed && http.downloads.empty() && app.calls == 0);
  }
  { // server offers an older version
    FakeHttp http; http.check_reply = {200, real}; http.download_bytes = artifact;
    FakeApplier app;
    const auto r = run_update(make("2.0.0", fresh("older")), http, app);
    CHECK(r.outcome == Outcome::CheckFailed && http.downloads.empty() && app.calls == 0);
  }
  { // plain http artifact url
    FakeHttp http; http.check_reply = {200, replace_first(real, "https://", "http://")}; http.download_bytes = artifact;
    FakeApplier app;
    const auto r = run_update(make("1.0.0", fresh("http")), http, app);
    CHECK(r.outcome == Outcome::CheckFailed && http.downloads.empty() && app.calls == 0);
  }
  { // wrong embedded key: someone else's signature
    FakeHttp http; http.check_reply = {200, real}; http.download_bytes = artifact;
    FakeApplier app;
    auto cfg = make("1.0.0", fresh("badkey"));
    cfg.policy.public_key_b64 = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
    const auto r = run_update(cfg, http, app);
    CHECK(r.outcome == Outcome::Rejected && r.decision == Decision::RejectBadSignature && app.calls == 0);
  }
  { // replay: the android release served to a windows install of the same app fails the signature
    FakeHttp http; http.check_reply = {200, real}; http.download_bytes = artifact;
    FakeApplier app;
    auto cfg = make("1.0.0", fresh("replay"));
    cfg.policy.platform = "windows";
    const auto r = run_update(cfg, http, app);
    CHECK(r.outcome == Outcome::Rejected && r.decision == Decision::RejectBadSignature && app.calls == 0);
  }
  { // same replay across apps
    FakeHttp http; http.check_reply = {200, real}; http.download_bytes = artifact;
    FakeApplier app;
    auto cfg = make("1.0.0", fresh("replay2"));
    cfg.policy.app = "hollow";
    const auto r = run_update(cfg, http, app);
    CHECK(r.outcome == Outcome::Rejected && r.decision == Decision::RejectBadSignature && app.calls == 0);
  }
  { // path traversal through the version field never reaches the filesystem
    FakeHttp http; http.check_reply = {200, replace_first(real, "\"version\":\"1.1.0\"", "\"version\":\"../../pwned\"")}; http.download_bytes = artifact;
    FakeApplier app;
    const auto work = fresh("trav");
    const auto r = run_update(make("1.0.0", work), http, app);
    CHECK(r.outcome == Outcome::CheckFailed && http.downloads.empty() && !fs::exists(tmp / "pwned.part") && !fs::exists(work.parent_path().parent_path() / "pwned.part"));
  }
  { // applier fails: cleaned up, reported
    FakeHttp http; http.check_reply = {200, real}; http.download_bytes = artifact;
    FakeApplier app; app.ok = false;
    const auto work = fresh("applyfail");
    const auto r = run_update(make("1.0.0", work), http, app);
    CHECK(r.outcome == Outcome::ApplyFailed && app.calls == 1 && dir_empty(work));
  }
  { // registration
    FakeHttp http;
    const auto cfg = make("1.0.0", fresh("reg"));
    CHECK(register_install(cfg, http, "tok"));
    CHECK(http.posts.size() == 1 && http.posts[0] == "https://svc.example.com/v1/register");
    const auto sent = parse_flat_json(http.last_post_body);
    CHECK(sent && str_of(*sent, "pushToken") == "tok");
    http.register_reply = {500, ""};
    CHECK(!register_install(cfg, http));
  }
}

int main() {
  const fs::path tmp = fs::temp_directory_path() / "triskelion_updater_tests";
  fs::remove_all(tmp);
  fs::create_directories(tmp);

  test_json();
  test_parse_check();
  test_urls_and_register();
  test_run_update(tmp);

  fs::remove_all(tmp);
  return report();
}
