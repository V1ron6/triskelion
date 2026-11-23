#include "triskelion_win/windows_client.hpp"

#include <windows.h>

#include <stdexcept>

#include "install_id.hpp"
#include "triskelion/client.hpp"
#include "triskelion/version.hpp"
#include "win_appliers.hpp"
#include "win_util.hpp"
#include "winhttp_client.hpp"

namespace triskelion_win {

namespace fs = std::filesystem;

namespace {

fs::path own_exe() {
  wchar_t buf[MAX_PATH * 4];
  const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
  return n ? fs::path(std::wstring(buf, n)) : fs::path();
}

bool valid_app_id(const std::string& id) {
  if (id.empty() || id.size() > 64) return false;
  for (const char c : id)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
  return id != "." && id != "..";  // it becomes a folder name under %LOCALAPPDATA%
}

}  // namespace

struct WindowsClient::Impl {
  WinHttpClient http;
  WinHttpStream stream;
  std::unique_ptr<triskelion::Applier> applier;
  std::unique_ptr<triskelion::Client> client;
};

WindowsClient::WindowsClient(Config config, Callback on_result) : impl_(std::make_unique<Impl>()) {
  if (!valid_app_id(config.app_id)) throw std::invalid_argument("app_id must be [a-z0-9._-], 1-64 chars");
  if (!triskelion::parse_version(config.current_version)) throw std::invalid_argument("current_version must be x.y.z");
  const bool https = config.server_url.rfind("https://", 0) == 0;
  if (!https && !(config.allow_insecure_http && config.server_url.rfind("http://", 0) == 0))
    throw std::invalid_argument("server_url must be https://");
  if (config.public_key_b64.empty()) throw std::invalid_argument("public_key_b64 is required");

  const fs::path data_dir = app_data_dir(to_wide(config.app_id));
  const std::string install_id = load_or_create_install_id(data_dir);
  if (install_id.empty()) throw std::runtime_error("could not create an install id");

  triskelion::UpdateConfig uc;
  uc.base_url = config.server_url;
  uc.policy = triskelion::Policy{config.app_id, "windows", config.current_version, config.public_key_b64};
  uc.install_id = install_id;
  uc.channel = config.channel;
  uc.work_dir = data_dir / "downloads";

  if (config.mode == ApplyMode::ReplaceExe) {
    const fs::path exe = config.installed_exe.empty() ? own_exe() : config.installed_exe;
    ReplaceApplier::cleanup_previous(exe);  // remove the .old left by the previous update
    impl_->applier = std::make_unique<ReplaceApplier>(exe);
  } else {
    impl_->applier = std::make_unique<InstallerApplier>(
        config.installer_is_msi ? InstallerApplier::Kind::Msi : InstallerApplier::Kind::Exe, config.installer_args);
  }

  triskelion::ClientOptions opts;
  opts.check_interval = std::chrono::duration_cast<std::chrono::milliseconds>(config.check_interval);
  impl_->client = std::make_unique<triskelion::Client>(std::move(uc), impl_->http, impl_->stream, *impl_->applier, opts,
                                                       std::move(on_result));
}

WindowsClient::~WindowsClient() {
  if (impl_ && impl_->client) impl_->client->stop();  // threads must end before http/stream/applier are destroyed
}

void WindowsClient::start() { impl_->client->start(); }
void WindowsClient::stop() { impl_->client->stop(); }
void WindowsClient::check_now() { impl_->client->check_now(); }
bool WindowsClient::update_applied() const { return impl_->client->update_applied(); }

}  // namespace triskelion_win
