#pragma once
// Embeddable Windows updater. Link the triskelion_win library into the app, then:
//
//   triskelion_win::Config cfg;
//   cfg.server_url = "https://your-service.onrender.com";
//   cfg.app_id = "hollow";
//   cfg.current_version = "1.0.0";
//   cfg.public_key_b64 = "<public key from tools/sign keygen>";
//
//   triskelion_win::WindowsClient updater(cfg, [](const triskelion::RunResult& r) {
//     if (r.outcome == triskelion::Outcome::Applied) { /* tell the user to restart */ }
//   });
//   updater.start();     // on app launch
//   ...
//   updater.stop();      // on exit (the destructor also does it)
//
// The app checks on launch and every check_interval, and also listens for broadcasts while it runs.
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "triskelion/updater.hpp"

namespace triskelion_win {

enum class ApplyMode {
  Installer,   // the release is an .exe/.msi installer, started silently. The app should exit once Applied.
  ReplaceExe,  // the release is the app's single .exe: the running exe is renamed to .old and replaced.
};

struct Config {
  std::string server_url;       // https://...
  std::string app_id;           // must match the app registered on the server: [a-z0-9._-]
  std::string current_version;  // x.y.z of THIS build; bump it with every release you publish
  std::string public_key_b64;   // PUBLIC Ed25519 key, baked into the build. Never the private key.
  std::string channel = "stable";

  ApplyMode mode = ApplyMode::Installer;

  // Installer mode
  bool installer_is_msi = false;
  std::wstring installer_args = L"/VERYSILENT /NORESTART";  // Inno Setup. NSIS: /S. MSI: /qn

  // ReplaceExe mode. Empty = the running executable.
  std::filesystem::path installed_exe;

  std::chrono::minutes check_interval{360};

  // Development only: allow http:// for a local server. Never ship with this on.
  bool allow_insecure_http = false;
};

class WindowsClient {
 public:
  using Callback = std::function<void(const triskelion::RunResult&)>;

  // Throws std::invalid_argument on a bad config (app_id characters, non-https url, bad version)
  // and std::runtime_error if the per-user install id cannot be created.
  explicit WindowsClient(Config config, Callback on_result = {});
  ~WindowsClient();
  WindowsClient(const WindowsClient&) = delete;
  WindowsClient& operator=(const WindowsClient&) = delete;

  void start();
  void stop();
  void check_now();
  bool update_applied() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace triskelion_win
