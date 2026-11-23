// Minimal host for the library, doubling as a command-line tool for testing a deployment.
//   triskelion-updater.exe           check once, apply if there is a verified update, exit
//   triskelion-updater.exe --watch   stay running: timer + broadcasts, exit after an update is applied
// A real app does NOT ship this exe: it embeds WindowsClient as shown in windows_client.hpp.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "triskelion_win/windows_client.hpp"

int main(int argc, char** argv) {
  const bool watch = argc > 1 && std::strcmp(argv[1], "--watch") == 0;

  triskelion_win::Config cfg;
  cfg.server_url = "https://your-service.onrender.com";
  cfg.app_id = "hollow";
  cfg.current_version = "1.0.0";
  cfg.public_key_b64 = "REPLACE_WITH_APP_PUBLIC_KEY_BASE64";  // output of: node tools/sign/sign.js keygen
  cfg.mode = triskelion_win::ApplyMode::Installer;

  std::atomic<bool> got_result{false}, applied{false};
  try {
    triskelion_win::WindowsClient updater(cfg, [&](const triskelion::RunResult& r) {
      std::printf("[triskelion] %s", triskelion::to_string(r.outcome));
      if (!r.detail.empty()) std::printf(": %s", r.detail.c_str());
      if (r.outcome == triskelion::Outcome::Applied) std::printf(" (%s)", r.info.manifest.version.c_str());
      std::printf("\n");
      if (r.outcome == triskelion::Outcome::Applied) applied = true;
      got_result = true;
    });
    updater.start();

    using namespace std::chrono_literals;
    const auto give_up = std::chrono::steady_clock::now() + 5min;
    while (!applied && (watch || (!got_result && std::chrono::steady_clock::now() < give_up)))
      std::this_thread::sleep_for(200ms);
    updater.stop();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[triskelion] %s\n", e.what());
    return 2;
  }
  return 0;
}
