#pragma once
#include <filesystem>
#include <string>
#include <string_view>

#include "triskelion/decide.hpp"
#include "triskelion/verify.hpp"

namespace triskelion {

struct HttpResponse {
  int status = 0;  // 0 = transport failure
  std::string body;
};

// Platform code implements these. The core never touches a socket or replaces a file itself.
class Http {
 public:
  virtual ~Http() = default;
  virtual HttpResponse get(const std::string& url) = 0;                               // small JSON
  virtual HttpResponse post_json(const std::string& url, const std::string& body) = 0;
  virtual bool download(const std::string& url, const std::filesystem::path& dest) = 0;  // true only on a full 200
};

class Applier {
 public:
  virtual ~Applier() = default;
  // Called only with a file that decide() approved. True if the update was put in place.
  virtual bool apply(const std::filesystem::path& verified_file) = 0;
};

struct UpdateConfig {
  std::string base_url;  // https://your-service.onrender.com
  Policy policy;
  std::string install_id;
  std::string channel = "stable";
  std::filesystem::path work_dir;  // downloads land here
};

struct UpdateInfo {
  Manifest manifest;  // app and platform come from the Policy, not from the server
  std::string url;
  std::string changelog;
  bool mandatory = false;
};

struct CheckResult {
  enum class Kind { NoUpdate, Update, Error } kind = Kind::Error;
  UpdateInfo info;
  std::string error;
};

std::string url_encode(std::string_view s);
std::string build_check_url(const UpdateConfig& cfg);
std::string build_register_body(const UpdateConfig& cfg, std::string_view push_token = {});
CheckResult parse_check_response(const Policy& policy, const HttpResponse& response);

enum class Outcome { UpToDate, Applied, CheckFailed, DownloadFailed, Rejected, ApplyFailed };
const char* to_string(Outcome o);

struct RunResult {
  Outcome outcome = Outcome::CheckFailed;
  Decision decision = Decision::Install;  // meaningful when outcome == Rejected
  std::string detail;
  UpdateInfo info;  // filled when an update was found
};

// Tell the server this install exists. Best effort; false just means no broadcast target yet.
bool register_install(const UpdateConfig& cfg, Http& http, std::string_view push_token = {});

// check -> download -> decide -> apply. The one entry point apps call, on a timer and on a broadcast.
RunResult run_update(const UpdateConfig& cfg, Http& http, Applier& applier);

}  // namespace triskelion
