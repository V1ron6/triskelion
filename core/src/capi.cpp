#include "triskelion/capi.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>

#include "triskelion/client.hpp"
#include "triskelion/verify.hpp"
#include "triskelion/version.hpp"

using namespace triskelion;

struct tri_response {
  int status = 0;
  std::string body;
};

extern "C" void tri_response_set(tri_response* out, int status, const char* body, size_t body_len) {
  if (!out) return;
  out->status = status;
  out->body.assign(body && body_len ? body : "", body ? body_len : 0);
}

namespace {

// ---- adapters from the C callbacks to the core's C++ interfaces ----------------------------------

struct HostHttp : Http {
  tri_host host;
  explicit HostHttp(const tri_host& h) : host(h) {}

  HttpResponse get(const std::string& url) override {
    tri_response r;
    host.http_get(host.user, url.c_str(), &r);
    return HttpResponse{r.status, std::move(r.body)};
  }
  HttpResponse post_json(const std::string& url, const std::string& body) override {
    tri_response r;
    host.http_post_json(host.user, url.c_str(), body.c_str(), &r);
    return HttpResponse{r.status, std::move(r.body)};
  }
  bool download(const std::string& url, const std::filesystem::path& dest) override {
    return host.download(host.user, url.c_str(), dest.u8string().c_str()) == 1;
  }
};

void ping_trampoline(void* ctx) { (*static_cast<const std::function<void()>*>(ctx))(); }

struct HostStream : Stream {
  tri_host host;
  explicit HostStream(const tri_host& h) : host(h) {}

  void run(const std::string& url, const std::function<void()>& on_update) override {
    host.stream_run(host.user, url.c_str(), &ping_trampoline, const_cast<std::function<void()>*>(&on_update));
  }
  void abort() override { host.stream_abort(host.user); }
};

struct HostApplier : Applier {
  tri_host host;
  explicit HostApplier(const tri_host& h) : host(h) {}
  bool apply(const std::filesystem::path& f) override { return host.apply(host.user, f.u8string().c_str()) == 1; }
};

int to_c_outcome(Outcome o) {
  switch (o) {
    case Outcome::UpToDate: return TRI_UP_TO_DATE;
    case Outcome::Applied: return TRI_APPLIED;
    case Outcome::CheckFailed: return TRI_CHECK_FAILED;
    case Outcome::DownloadFailed: return TRI_DOWNLOAD_FAILED;
    case Outcome::Rejected: return TRI_REJECTED;
    case Outcome::ApplyFailed: return TRI_APPLY_FAILED;
  }
  return TRI_CHECK_FAILED;
}

void set_err(char* err, size_t len, const char* msg) {
  if (err && len) std::snprintf(err, len, "%s", msg);
}

bool nonempty(const char* s) { return s && *s; }

// Same rule as the server's app id and install id checks.
bool valid_app_id(const char* s) {
  const size_t n = std::strlen(s);
  if (n < 2 || n > 64) return false;
  for (size_t i = 0; i < n; i++) {
    const char c = s[i];
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (!(alnum || (i > 0 && (c == '.' || c == '_' || c == '-')))) return false;
  }
  return true;
}
bool valid_install_id(const char* s) {
  const size_t n = std::strlen(s);
  if (n < 16 || n > 64) return false;
  for (size_t i = 0; i < n; i++) {
    const char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == '-')) return false;
  }
  return true;
}

}  // namespace

struct tri_client {
  HostHttp http;
  HostStream stream;
  HostApplier applier;
  std::unique_ptr<Client> client;  // last member: destroyed first, its threads still use the adapters above
  explicit tri_client(const tri_host& h) : http(h), stream(h), applier(h) {}
};

extern "C" {

tri_client* tri_client_create(const tri_config* cfg, const tri_host* host, char* err, size_t err_len) {
  if (!cfg || !host) { set_err(err, err_len, "config and host are required"); return nullptr; }
  if (!host->http_get || !host->http_post_json || !host->download || !host->stream_run || !host->stream_abort || !host->apply) {
    set_err(err, err_len, "host is missing a required callback");
    return nullptr;
  }
  if (!nonempty(cfg->server_url) || !nonempty(cfg->app_id) || !nonempty(cfg->platform) || !nonempty(cfg->current_version) ||
      !nonempty(cfg->public_key_b64) || !nonempty(cfg->install_id) || !nonempty(cfg->work_dir)) {
    set_err(err, err_len, "a required config field is missing or empty");
    return nullptr;
  }
  if (!valid_app_id(cfg->app_id)) { set_err(err, err_len, "app_id must be 2-64 chars of [a-z0-9._-]"); return nullptr; }
  if (std::strcmp(cfg->platform, "android") != 0 && std::strcmp(cfg->platform, "windows") != 0) {
    set_err(err, err_len, "platform must be android or windows");
    return nullptr;
  }
  if (!parse_version(cfg->current_version)) { set_err(err, err_len, "current_version must be x.y.z"); return nullptr; }
  const std::string channel = cfg->channel ? cfg->channel : "stable";
  if (channel != "stable" && channel != "beta") { set_err(err, err_len, "channel must be stable or beta"); return nullptr; }
  if (!valid_install_id(cfg->install_id)) { set_err(err, err_len, "install_id must be 16-64 chars of [0-9a-fA-F-]"); return nullptr; }
  {
    const auto keys = split_public_keys(cfg->public_key_b64);
    bool ok = !keys.empty() && keys.size() <= kMaxPublicKeys;
    for (const auto& k : keys) {
      const auto raw = base64_decode(k);
      if (!raw || raw->size() != 32) ok = false;
    }
    if (!ok) {
      set_err(err, err_len, "public_key_b64 must be 1-8 comma-separated base64 32-byte Ed25519 keys");
      return nullptr;
    }
  }
  const std::string url = cfg->server_url;
  const bool https = url.rfind("https://", 0) == 0;
  if (!https && !(cfg->allow_insecure_http && url.rfind("http://", 0) == 0)) {
    set_err(err, err_len, "server_url must be https://");
    return nullptr;
  }

  try {
    UpdateConfig uc;
    uc.base_url = url;
    uc.policy = Policy{cfg->app_id, cfg->platform, cfg->current_version, cfg->public_key_b64};
    uc.install_id = cfg->install_id;
    uc.channel = channel;
    uc.work_dir = std::filesystem::u8path(cfg->work_dir);

    ClientOptions opts;
    if (cfg->check_interval_ms) opts.check_interval = std::chrono::milliseconds(cfg->check_interval_ms);
    if (cfg->retry_interval_ms) opts.retry_interval = std::chrono::milliseconds(cfg->retry_interval_ms);
    if (cfg->reconnect_delay_ms) opts.reconnect_delay = std::chrono::milliseconds(cfg->reconnect_delay_ms);

    auto c = std::make_unique<tri_client>(*host);
    const tri_host h = *host;
    Client::Callback cb;
    if (h.on_result) {
      cb = [h](const RunResult& r) {
        h.on_result(h.user, to_c_outcome(r.outcome), r.detail.c_str(), r.info.manifest.version.c_str());
      };
    }
    c->client = std::make_unique<Client>(std::move(uc), c->http, c->stream, c->applier, opts, std::move(cb));
    return c.release();
  } catch (...) {
    set_err(err, err_len, "out of memory or internal error");
    return nullptr;
  }
}

void tri_client_start(tri_client* c) { if (c) c->client->start(); }
void tri_client_check_now(tri_client* c) { if (c) c->client->check_now(); }
int tri_client_update_applied(const tri_client* c) { return c && c->client->update_applied() ? 1 : 0; }
void tri_client_stop(tri_client* c) { if (c) c->client->stop(); }
void tri_client_destroy(tri_client* c) { delete c; }

}  // extern "C"
