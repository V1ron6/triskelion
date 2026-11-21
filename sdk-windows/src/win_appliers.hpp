#pragma once
#include <filesystem>
#include <string>

#include "triskelion/updater.hpp"

namespace triskelion_win {

// Mode A: the app is a single .exe. The running exe is renamed aside (Windows allows renaming a
// running image, not overwriting it), the verified file takes its place, and the old one is kept as
// <exe>.old so a failed swap rolls back. The new version starts on the next launch.
class ReplaceApplier : public triskelion::Applier {
 public:
  explicit ReplaceApplier(std::filesystem::path installed_exe) : exe_(std::move(installed_exe)) {}
  bool apply(const std::filesystem::path& verified_file) override;
  // Call at startup: removes the <exe>.old left by the previous update.
  static void cleanup_previous(const std::filesystem::path& installed_exe);

 private:
  std::filesystem::path exe_;
};

// Mode B: the release is an installer (.exe or .msi). The verified file is renamed to a fixed
// name chosen by us (never by the server), then started with silent arguments. The calling app
// should exit after apply() returns true so the installer can replace its files.
class InstallerApplier : public triskelion::Applier {
 public:
  enum class Kind { Exe, Msi };
  InstallerApplier(Kind kind, std::wstring silent_args) : kind_(kind), args_(std::move(silent_args)) {}
  bool apply(const std::filesystem::path& verified_file) override;

 private:
  Kind kind_;
  std::wstring args_;  // e.g. L"/VERYSILENT /NORESTART" for Inno Setup, L"/qn" for msiexec
};

}  // namespace triskelion_win
