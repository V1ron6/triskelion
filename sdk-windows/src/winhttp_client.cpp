#include "winhttp_client.hpp"

#include <fstream>
#include <memory>
#include <vector>

#include "win_util.hpp"

namespace triskelion_win {

namespace {

struct HandleCloser {
  void operator()(HINTERNET h) const { if (h) WinHttpCloseHandle(h); }
};
using Handle = std::unique_ptr<void, HandleCloser>;

constexpr std::size_t kMaxJsonBody = 128 * 1024;

// One request, sent and with response headers received. status == 0 means transport failure.
struct Request {
  Handle conn, req;
  int status = 0;
};

Request open(HINTERNET session, const std::string& url, const wchar_t* method, const std::string* body) {
  Request r;
  const std::wstring wurl = to_wide(url);

  wchar_t host[256], path[2048], extra[2048];
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  uc.lpszHostName = host;     uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;      uc.dwUrlPathLength = 2048;
  uc.lpszExtraInfo = extra;   uc.dwExtraInfoLength = 2048;
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return r;
  if (uc.nScheme != INTERNET_SCHEME_HTTPS && uc.nScheme != INTERNET_SCHEME_HTTP) return r;

  const std::wstring target = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);
  r.conn.reset(WinHttpConnect(session, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0));
  if (!r.conn) return r;
  r.req.reset(WinHttpOpenRequest(r.conn.get(), method, target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
  if (!r.req) return r;

  const wchar_t* headers = body ? L"Content-Type: application/json" : WINHTTP_NO_ADDITIONAL_HEADERS;
  const DWORD len = body ? static_cast<DWORD>(body->size()) : 0;
  if (!WinHttpSendRequest(r.req.get(), headers, body ? static_cast<DWORD>(-1L) : 0,
                          body ? const_cast<char*>(body->data()) : WINHTTP_NO_REQUEST_DATA, len, len, 0))
    return r;
  if (!WinHttpReceiveResponse(r.req.get(), nullptr)) return r;

  DWORD status = 0, size = sizeof(status);
  if (!WinHttpQueryHeaders(r.req.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
    return r;
  r.status = static_cast<int>(status);
  return r;
}

// Reads the next chunk. Returns false on error, true with n == 0 at end of body.
bool read_chunk(HINTERNET req, char* buf, DWORD cap, DWORD& n) {
  n = 0;
  return WinHttpReadData(req, buf, cap, &n) == TRUE;
}

triskelion::HttpResponse exchange(HINTERNET session, const std::string& url, const wchar_t* method, const std::string* body) {
  Request r = open(session, url, method, body);
  triskelion::HttpResponse out;
  if (r.status == 0) return out;

  std::vector<char> buf(16 * 1024);
  DWORD n = 0;
  while (read_chunk(r.req.get(), buf.data(), static_cast<DWORD>(buf.size()), n) && n > 0) {
    out.body.append(buf.data(), n);
    if (out.body.size() > kMaxJsonBody) return triskelion::HttpResponse{};  // far bigger than any real reply
  }
  if (n != 0) return triskelion::HttpResponse{};  // read error mid-body
  out.status = r.status;
  return out;
}

}  // namespace

WinHttpClient::WinHttpClient() {
  session_ = WinHttpOpen(L"Triskelion/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                         WINHTTP_NO_PROXY_BYPASS, 0);
  if (session_) WinHttpSetTimeouts(session_, 10000, 10000, 30000, 60000);  // resolve, connect, send, receive (ms)
}

WinHttpClient::~WinHttpClient() {
  if (session_) WinHttpCloseHandle(session_);
}

triskelion::HttpResponse WinHttpClient::get(const std::string& url) {
  return session_ ? exchange(session_, url, L"GET", nullptr) : triskelion::HttpResponse{};
}

triskelion::HttpResponse WinHttpClient::post_json(const std::string& url, const std::string& body) {
  return session_ ? exchange(session_, url, L"POST", &body) : triskelion::HttpResponse{};
}

bool WinHttpClient::download(const std::string& url, const std::filesystem::path& dest) {
  if (!session_) return false;
  Request r = open(session_, url, L"GET", nullptr);
  if (r.status != 200) return false;

  std::ofstream out(dest, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  std::vector<char> buf(64 * 1024);
  DWORD n = 0;
  while (read_chunk(r.req.get(), buf.data(), static_cast<DWORD>(buf.size()), n) && n > 0) {
    out.write(buf.data(), n);
    if (!out) return false;
  }
  out.close();
  return n == 0 && !out.fail();  // n != 0 would mean the last read failed. Size and hash are checked by the core.
}

void WinHttpClient::stream(const std::string& url, const std::function<void()>& on_update, const std::atomic<bool>& stop) {
  if (!session_) return;
  Request r = open(session_, url, L"GET", nullptr);
  if (r.status != 200) return;

  std::string pending;
  std::vector<char> buf(4096);
  DWORD n = 0;
  while (!stop && read_chunk(r.req.get(), buf.data(), static_cast<DWORD>(buf.size()), n) && n > 0) {
    pending.append(buf.data(), n);
    for (std::size_t at; (at = pending.find("event: update")) != std::string::npos;) {
      on_update();
      pending.erase(0, at + 13);
    }
    if (pending.size() > 8192) pending.erase(0, pending.size() - 64);  // keepalive noise; keep a tail for split markers
  }
}

}  // namespace triskelion_win
