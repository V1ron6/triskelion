#pragma once
#include <windows.h>
#include <winhttp.h>

#include <mutex>
#include <string>

#include "triskelion/client.hpp"
#include "triskelion/updater.hpp"

namespace triskelion_win {

// Http for the core on WinHTTP. TLS certificates are validated by the OS, and WinHTTP refuses
// redirects from https to http by default. Used from the Client's worker thread only.
class WinHttpClient : public triskelion::Http {
 public:
  WinHttpClient();
  ~WinHttpClient() override;
  WinHttpClient(const WinHttpClient&) = delete;
  WinHttpClient& operator=(const WinHttpClient&) = delete;

  triskelion::HttpResponse get(const std::string& url) override;
  triskelion::HttpResponse post_json(const std::string& url, const std::string& body) override;
  bool download(const std::string& url, const std::filesystem::path& dest) override;

 private:
  HINTERNET session_ = nullptr;
};

// Broadcast channel over Server-Sent Events. Own session, used from the Client's listener thread only.
// abort() is sticky and closes the in-flight request so a blocked read returns at once. If it lands
// while a connection is still being opened it takes effect when the connect attempt times out
// (about 5 s), because WinHTTP cannot cancel a handle that has not been returned yet.
class WinHttpStream : public triskelion::Stream {
 public:
  WinHttpStream();
  ~WinHttpStream() override;
  WinHttpStream(const WinHttpStream&) = delete;
  WinHttpStream& operator=(const WinHttpStream&) = delete;

  void run(const std::string& url, const std::function<void()>& on_update) override;
  void abort() override;

 private:
  HINTERNET session_ = nullptr;
  std::mutex m_;
  HINTERNET active_ = nullptr;  // guarded by m_; closed exactly once, by run() or abort()
  bool aborted_ = false;        // guarded by m_
};

}  // namespace triskelion_win
