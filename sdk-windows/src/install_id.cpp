#include "install_id.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>

#include <fstream>
#include <system_error>

namespace triskelion_win {

namespace fs = std::filesystem;

fs::path app_data_dir(const std::wstring& app) {
  PWSTR raw = nullptr;
  fs::path base;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) base = raw;
  CoTaskMemFree(raw);
  if (base.empty()) base = fs::temp_directory_path();
  return base / app;
}

static bool is_valid_id(const std::string& s) {
  if (s.size() != 32) return false;
  for (const char c : s)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}

std::string load_or_create_install_id(const fs::path& dir) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  const fs::path file = dir / "install-id";

  {
    std::ifstream in(file);
    std::string id;
    if (in >> id && is_valid_id(id)) return id;
  }

  unsigned char bytes[16];
  if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return {};
  static const char* hex = "0123456789abcdef";
  std::string id;
  for (const unsigned char b : bytes) {
    id.push_back(hex[b >> 4]);
    id.push_back(hex[b & 0xf]);
  }
  std::ofstream(file, std::ios::trunc) << id;
  return id;
}

}  // namespace triskelion_win
