#include "win_appliers.hpp"

#include <windows.h>

#include <system_error>

namespace triskelion_win {

namespace fs = std::filesystem;

bool ReplaceApplier::apply(const fs::path& verified_file) {
  std::error_code ec;
  fs::path old = exe_;
  old += L".old";

  fs::remove(old, ec);
  ec.clear();
  fs::rename(exe_, old, ec);  // allowed even while the exe is running
  if (ec) return false;

  // Same volume as the install is not guaranteed for the work dir, so copy rather than rename.
  fs::copy_file(verified_file, exe_, fs::copy_options::none, ec);
  if (ec) {
    std::error_code undo;
    fs::remove(exe_, undo);
    fs::rename(old, exe_, undo);  // roll back to the working version
    return false;
  }
  fs::remove(verified_file, ec);
  return true;
}

void ReplaceApplier::cleanup_previous(const fs::path& installed_exe) {
  std::error_code ec;
  fs::path old = installed_exe;
  old += L".old";
  fs::remove(old, ec);  // fails harmlessly if still locked; retried next launch
}

bool InstallerApplier::apply(const fs::path& verified_file) {
  std::error_code ec;
  const fs::path dir = verified_file.parent_path();
  const fs::path ready = dir / (kind_ == Kind::Msi ? L"triskelion-update.msi" : L"triskelion-update.exe");
  fs::remove(ready, ec);
  ec.clear();
  fs::rename(verified_file, ready, ec);
  if (ec) return false;

  std::wstring cmd;
  if (kind_ == Kind::Msi) {
    cmd = L"msiexec.exe /i \"" + ready.wstring() + L"\" " + args_;
  } else {
    cmd = L"\"" + ready.wstring() + L"\" " + args_;
  }

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  // CreateProcess may modify the command line buffer, so it needs a writable copy.
  std::wstring mutable_cmd = cmd;
  const BOOL ok = CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS,
                                 nullptr, nullptr, &si, &pi);
  if (!ok) return false;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

}  // namespace triskelion_win
