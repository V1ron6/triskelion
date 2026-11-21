#pragma once
#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <functional>
#include <string>

#include "triskelion/updater.hpp"

namespace triskelion_win {

// Http for the core, on top of WinHTTP. TLS certificates are validated by the OS;
// WinHTTP refuses redirects from https to http by default.
class WinHttpClient : public triskelion::Http {
 public:
  WinHttpClient();
  ~WinHttpClient() override;
  WinHttpClient(const WinHttpClient&) = delete;
  WinHttpClient& operator=(const WinHttpClient&) = delete;

  triskelion::HttpResponse get(const std::string& url) override;
  triskelion::HttpResponse post_json(const std::string& url, const std::string& body) override;
  bool download(const std::string& url, const std::filesystem::path& dest) override;

  // Blocks on the SSE stream and calls on_update for every "update" event.
  // Returns when the stream ends, errors, or `stop` is set (checked between reads; the server's
  // 25 s keepalive bounds how long that takes). The caller reconnects.
  void stream(const std::string& url, const std::function<void()>& on_update, const std::atomic<bool>& stop);

 private:
  HINTERNET session_ = nullptr;
};

}  // namespace triskelion_win
