#pragma once
// Per-app build configuration. Copy this file for each app and edit it.
// Everything here is compiled into the binary on purpose: the server never tells the client
// who it is, which key to trust, or how to install.
#include <filesystem>
#include <string>

// 1 = release is an installer (.exe/.msi) run silently. 0 = release is the app's single .exe, swapped in place.
#define TRISKELION_APPLY_MODE_INSTALLER 1

namespace cfg {

inline constexpr const char* SERVER_URL = "https://your-service.onrender.com";
inline constexpr const char* APP_ID = "hollow";
inline constexpr const char* CURRENT_VERSION = "1.0.0";  // bump with every build you publish
inline constexpr const char* CHANNEL = "stable";

// Output of `node tools/sign/sign.js keygen`. This is the PUBLIC key; never put the private key here.
inline constexpr const char* PUBLIC_KEY_B64 = "REPLACE_WITH_APP_PUBLIC_KEY_BASE64";

// Installer mode
inline constexpr bool INSTALLER_IS_MSI = false;
inline const wchar_t* INSTALLER_SILENT_ARGS = L"/VERYSILENT /NORESTART";  // Inno Setup. NSIS: /S. MSI: /qn

// Replace mode
inline std::filesystem::path INSTALLED_EXE_PATH() {
  return std::filesystem::path(L"C:\\Program Files\\Hollow\\hollow.exe");  // or derive from the app's own location
}

}  // namespace cfg
