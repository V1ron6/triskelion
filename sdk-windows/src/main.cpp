// triskelion-updater.exe: the Windows side of Triskelion.
// Per app, edit app_config.hpp (app id, version, embedded public key, server URL, apply mode) and build.
//
//   triskelion-updater.exe          check once, apply if a verified update exists, exit
//   triskelion-updater.exe --watch  stay running: check now, then every CHECK_INTERVAL, and
//                                   immediately whenever the server broadcasts a new version
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include "app_config.hpp"
#include "install_id.hpp"
#include "triskelion/updater.hpp"
#include "win_appliers.hpp"
#include "win_util.hpp"
#include "winhttp_client.hpp"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

constexpr auto CHECK_INTERVAL = 6h;
std::atomic<bool> g_stop{false};

fs::path own_exe() {
  wchar_t buf[MAX_PATH * 4];
  const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
  return n ? fs::path(std::wstring(buf, n)) : fs::path();
}

triskelion::UpdateConfig make_config(const fs::path& data_dir, const std::string& install_id) {
  triskelion::UpdateConfig uc;
  uc.base_url = cfg::SERVER_URL;
  uc.policy = triskelion::Policy{cfg::APP_ID, "windows", cfg::CURRENT_VERSION, cfg::PUBLIC_KEY_B64};
  uc.install_id = install_id;
  uc.channel = cfg::CHANNEL;
  uc.work_dir = data_dir / "downloads";
  return uc;
}

// Both the timer and the broadcast thread call this, so a ping that lands mid-check waits its turn.
std::mutex g_run_lock;

bool run_once(const triskelion::UpdateConfig& cfg, triskelion_win::WinHttpClient& http, triskelion::Applier& applier) {
  std::lock_guard<std::mutex> lock(g_run_lock);
  const triskelion::RunResult r = triskelion::run_update(cfg, http, applier);
  std::printf("[triskelion] %s", triskelion::to_string(r.outcome));
  if (!r.detail.empty()) std::printf(": %s", r.detail.c_str());
  if (r.outcome == triskelion::Outcome::Applied) std::printf(" (%s)", r.info.manifest.version.c_str());
  std::printf("\n");
  return r.outcome == triskelion::Outcome::Applied;
}

}  // namespace

int main(int argc, char** argv) {
  const bool watch = argc > 1 && std::strcmp(argv[1], "--watch") == 0;

  const fs::path data_dir = triskelion_win::app_data_dir(triskelion_win::to_wide(cfg::APP_ID));
  const std::string install_id = triskelion_win::load_or_create_install_id(data_dir);
  if (install_id.empty()) {
    std::fprintf(stderr, "[triskelion] could not create an install id\n");
    return 2;
  }
  const triskelion::UpdateConfig config = make_config(data_dir, install_id);

  triskelion_win::WinHttpClient http;

#if TRISKELION_APPLY_MODE_INSTALLER
  triskelion_win::InstallerApplier applier(cfg::INSTALLER_IS_MSI ? triskelion_win::InstallerApplier::Kind::Msi
                                                                 : triskelion_win::InstallerApplier::Kind::Exe,
                                           cfg::INSTALLER_SILENT_ARGS);
#else
  triskelion_win::ReplaceApplier::cleanup_previous(cfg::INSTALLED_EXE_PATH());
  triskelion_win::ReplaceApplier applier(cfg::INSTALLED_EXE_PATH());
#endif

  triskelion::register_install(config, http);  // best effort: lets the server know whom to broadcast to
  if (run_once(config, http, applier)) return 0;
  if (!watch) return 0;

  // Watch mode: a broadcast just triggers the same pull. The ping itself is never trusted or parsed.
  std::atomic<bool> ping{false};
  std::thread listener([&] {
    triskelion_win::WinHttpClient stream_http;  // own session: the stream blocks for a long time
    const std::string url = config.base_url + "/v1/stream?install=" + triskelion::url_encode(config.install_id);
    while (!g_stop) {
      stream_http.stream(url, [&] { ping = true; }, g_stop);
      for (int i = 0; i < 30 && !g_stop; i++) std::this_thread::sleep_for(1s);  // reconnect backoff
    }
  });

  auto next_timer = std::chrono::steady_clock::now() + CHECK_INTERVAL;
  while (true) {
    std::this_thread::sleep_for(1s);
    const bool due = std::chrono::steady_clock::now() >= next_timer;
    if (ping.exchange(false) || due) {
      next_timer = std::chrono::steady_clock::now() + CHECK_INTERVAL;
      if (run_once(config, http, applier)) break;
    }
  }
  g_stop = true;
  listener.detach();  // blocked in a read; the process is exiting so the installer or new exe can take over
  return 0;
}
