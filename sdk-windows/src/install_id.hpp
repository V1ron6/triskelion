#pragma once
#include <filesystem>
#include <string>

namespace triskelion_win {

// %LOCALAPPDATA%\<app>\ : per-user, so other users cannot touch downloads or the id.
std::filesystem::path app_data_dir(const std::wstring& app);

// A random id, created once and stored in <dir>\install-id. Not derived from the machine.
// 32 lowercase hex chars, which the server's install-id check accepts.
std::string load_or_create_install_id(const std::filesystem::path& dir);

}  // namespace triskelion_win
