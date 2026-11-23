#include "triskelion_win/triskelion_win_c.h"

#include <cstdio>
#include <exception>
#include <memory>

#include "triskelion/capi.h"  // for the tri_outcome numbers only
#include "triskelion_win/windows_client.hpp"

struct triskelion_win_client {
  std::unique_ptr<triskelion_win::WindowsClient> client;
};

namespace {

int to_code(triskelion::Outcome o) {
  switch (o) {
    case triskelion::Outcome::UpToDate: return TRI_UP_TO_DATE;
    case triskelion::Outcome::Applied: return TRI_APPLIED;
    case triskelion::Outcome::CheckFailed: return TRI_CHECK_FAILED;
    case triskelion::Outcome::DownloadFailed: return TRI_DOWNLOAD_FAILED;
    case triskelion::Outcome::Rejected: return TRI_REJECTED;
    case triskelion::Outcome::ApplyFailed: return TRI_APPLY_FAILED;
  }
  return TRI_CHECK_FAILED;
}

void set_err(char* err, size_t len, const char* msg) {
  if (err && len) std::snprintf(err, len, "%s", msg);
}

}  // namespace

extern "C" {

triskelion_win_client* triskelion_win_create(const triskelion_win_config* c, triskelion_win_callback cb, void* user,
                                             char* err, size_t err_len) {
  if (!c || !c->server_url || !c->app_id || !c->current_version || !c->public_key_b64) {
    set_err(err, err_len, "server_url, app_id, current_version and public_key_b64 are required");
    return nullptr;
  }
  try {
    triskelion_win::Config cfg;
    cfg.server_url = c->server_url;
    cfg.app_id = c->app_id;
    cfg.current_version = c->current_version;
    cfg.public_key_b64 = c->public_key_b64;
    if (c->channel) cfg.channel = c->channel;
    cfg.mode = c->mode == 1 ? triskelion_win::ApplyMode::ReplaceExe : triskelion_win::ApplyMode::Installer;
    cfg.installer_is_msi = c->installer_is_msi != 0;
    if (c->installer_args) cfg.installer_args = c->installer_args;
    if (c->installed_exe) cfg.installed_exe = c->installed_exe;
    if (c->check_interval_minutes) cfg.check_interval = std::chrono::minutes(c->check_interval_minutes);
    cfg.allow_insecure_http = c->allow_insecure_http != 0;

    triskelion_win::WindowsClient::Callback on_result;
    if (cb) {
      on_result = [cb, user](const triskelion::RunResult& r) {
        cb(user, to_code(r.outcome), r.detail.c_str(), r.info.manifest.version.c_str());
      };
    }
    auto out = std::make_unique<triskelion_win_client>();
    out->client = std::make_unique<triskelion_win::WindowsClient>(std::move(cfg), std::move(on_result));
    return out.release();
  } catch (const std::exception& e) {
    set_err(err, err_len, e.what());
  } catch (...) {
    set_err(err, err_len, "unknown error");
  }
  return nullptr;
}

void triskelion_win_start(triskelion_win_client* c) { if (c) c->client->start(); }
void triskelion_win_check_now(triskelion_win_client* c) { if (c) c->client->check_now(); }
int triskelion_win_update_applied(const triskelion_win_client* c) { return c && c->client->update_applied() ? 1 : 0; }
void triskelion_win_stop(triskelion_win_client* c) { if (c) c->client->stop(); }
void triskelion_win_destroy(triskelion_win_client* c) { delete c; }

}  // extern "C"
