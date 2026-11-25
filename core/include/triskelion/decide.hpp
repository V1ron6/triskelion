#pragma once
#include <filesystem>
#include <string>
#include "triskelion/verify.hpp"

namespace triskelion {

// What this build of the app is, baked in at compile time. Never taken from the server.
struct Policy {
  std::string app;
  std::string platform;         // "android" | "windows"
  std::string current_version;  // x.y.z
  std::string public_key_b64;   // 32-byte Ed25519 key, base64; or several, comma-separated (rotation)
};

enum class Decision {
  Install,
  RejectBadVersion,
  RejectWrongTarget,
  RejectDowngrade,
  RejectBadSignature,
  RejectFileUnreadable,
  RejectSizeMismatch,
  RejectHashMismatch,
};

const char* to_string(Decision d);

// Run after the download finished. Cheap checks first, bytes last.
// Install means the file at `downloaded` is exactly what the key holder signed.
Decision decide(const Policy& policy, const Manifest& manifest, const std::filesystem::path& downloaded);

}  // namespace triskelion
