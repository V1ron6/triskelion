#include "triskelion/updater.hpp"

#include <system_error>

#include "triskelion/json.hpp"
#include "triskelion/version.hpp"

namespace triskelion {

namespace fs = std::filesystem;

namespace {

const std::string* get_str(const JsonObject& o, const char* key) {
  const auto it = o.find(key);
  return it == o.end() ? nullptr : std::get_if<std::string>(&it->second);
}
const std::int64_t* get_int(const JsonObject& o, const char* key) {
  const auto it = o.find(key);
  return it == o.end() ? nullptr : std::get_if<std::int64_t>(&it->second);
}
const bool* get_bool(const JsonObject& o, const char* key) {
  const auto it = o.find(key);
  return it == o.end() ? nullptr : std::get_if<bool>(&it->second);
}

CheckResult fail(std::string why) {
  CheckResult r;
  r.kind = CheckResult::Kind::Error;
  r.error = std::move(why);
  return r;
}

RunResult finish(Outcome o, std::string detail, UpdateInfo info = {}, Decision d = Decision::Install) {
  RunResult r;
  r.outcome = o;
  r.decision = d;
  r.detail = std::move(detail);
  r.info = std::move(info);
  return r;
}

bool starts_with(const std::string& s, std::string_view p) { return s.compare(0, p.size(), p) == 0; }

}  // namespace

const char* to_string(Outcome o) {
  switch (o) {
    case Outcome::UpToDate: return "up to date";
    case Outcome::Applied: return "applied";
    case Outcome::CheckFailed: return "check failed";
    case Outcome::DownloadFailed: return "download failed";
    case Outcome::Rejected: return "rejected";
    case Outcome::ApplyFailed: return "apply failed";
  }
  return "unknown";
}

std::string url_encode(std::string_view s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (const unsigned char c : s) {
    const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                            c == '-' || c == '.' || c == '_' || c == '~';
    if (unreserved) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0xf]);
    }
  }
  return out;
}

static std::string base_of(const UpdateConfig& cfg) {
  std::string b = cfg.base_url;
  while (!b.empty() && b.back() == '/') b.pop_back();
  return b;
}

std::string build_check_url(const UpdateConfig& cfg) {
  return base_of(cfg) + "/v1/check?app=" + url_encode(cfg.policy.app) + "&platform=" + url_encode(cfg.policy.platform) +
         "&version=" + url_encode(cfg.policy.current_version) + "&install=" + url_encode(cfg.install_id) +
         "&channel=" + url_encode(cfg.channel);
}

std::string build_register_body(const UpdateConfig& cfg, std::string_view push_token) {
  std::string body = "{\"install\":" + json_quote(cfg.install_id) + ",\"app\":" + json_quote(cfg.policy.app) +
                     ",\"platform\":" + json_quote(cfg.policy.platform) +
                     ",\"version\":" + json_quote(cfg.policy.current_version) +
                     ",\"channel\":" + json_quote(cfg.channel);
  if (!push_token.empty()) body += ",\"pushToken\":" + json_quote(push_token);
  return body + "}";
}

CheckResult parse_check_response(const Policy& policy, const HttpResponse& response) {
  if (response.status != 200) return fail("http " + std::to_string(response.status));
  const auto obj = parse_flat_json(response.body);
  if (!obj) return fail("malformed response");

  const bool* update = get_bool(*obj, "update");
  if (!update) return fail("missing update flag");
  CheckResult r;
  if (!*update) {
    r.kind = CheckResult::Kind::NoUpdate;
    return r;
  }

  const std::string* version = get_str(*obj, "version");
  const std::string* sha256 = get_str(*obj, "sha256");
  const std::string* signature = get_str(*obj, "signature");
  const std::string* url = get_str(*obj, "url");
  const std::int64_t* size = get_int(*obj, "size");
  if (!version || !sha256 || !signature || !url || !size || *size <= 0) return fail("incomplete update response");
  // The version later becomes part of a file name, so it must be a plain x.y.z first.
  if (!parse_version(*version)) return fail("bad version in response");

  const std::string* changelog = get_str(*obj, "changelog");
  const bool* mandatory = get_bool(*obj, "mandatory");

  r.kind = CheckResult::Kind::Update;
  r.info.manifest = Manifest{policy.app, policy.platform, *version, *sha256, static_cast<std::uint64_t>(*size), *signature};
  r.info.url = *url;
  r.info.changelog = changelog ? *changelog : "";
  r.info.mandatory = mandatory ? *mandatory : false;
  return r;
}

bool register_install(const UpdateConfig& cfg, Http& http, std::string_view push_token) {
  return http.post_json(base_of(cfg) + "/v1/register", build_register_body(cfg, push_token)).status == 200;
}

RunResult run_update(const UpdateConfig& cfg, Http& http, Applier& applier) {
  const CheckResult check = parse_check_response(cfg.policy, http.get(build_check_url(cfg)));
  if (check.kind == CheckResult::Kind::Error) return finish(Outcome::CheckFailed, check.error);
  if (check.kind == CheckResult::Kind::NoUpdate) return finish(Outcome::UpToDate, "");
  const UpdateInfo& info = check.info;

  // A server offering something not newer is broken or hostile: do not even download it.
  const auto current = parse_version(cfg.policy.current_version);
  const auto offered = parse_version(info.manifest.version);
  if (!current || !offered) return finish(Outcome::CheckFailed, "bad version", info);
  if (compare(*offered, *current) <= 0) return finish(Outcome::CheckFailed, "server offered a version that is not newer", info);

  // Artifacts come from R2 presigned URLs, always https.
  if (!starts_with(info.url, "https://")) return finish(Outcome::CheckFailed, "download url is not https", info);

  std::error_code ec;
  fs::create_directories(cfg.work_dir, ec);
  const fs::path file = cfg.work_dir / ("triskelion-" + info.manifest.version + ".part");
  fs::remove(file, ec);

  if (!http.download(info.url, file)) {
    fs::remove(file, ec);
    return finish(Outcome::DownloadFailed, "download did not complete", info);
  }

  const Decision d = decide(cfg.policy, info.manifest, file);
  if (d != Decision::Install) {
    fs::remove(file, ec);
    return finish(Outcome::Rejected, to_string(d), info, d);
  }

  if (!applier.apply(file)) {
    fs::remove(file, ec);
    return finish(Outcome::ApplyFailed, "could not put the update in place", info);
  }
  return finish(Outcome::Applied, "", info);
}

}  // namespace triskelion
