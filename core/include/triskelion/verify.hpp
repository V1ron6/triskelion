#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace triskelion {

// What the server's /v1/check returns about a release.
struct Manifest {
  std::string app;
  std::string platform;
  std::string version;
  std::string sha256_hex;  // lowercase hex, exactly as signed
  std::uint64_t size = 0;
  std::string signature_b64;
};

// Standard base64 with padding. Returns nullopt on any malformed input.
std::optional<std::vector<std::uint8_t>> base64_decode(std::string_view s);

// Streaming SHA-256 of a file, lowercase hex. nullopt if the file cannot be read.
std::optional<std::string> sha256_file_hex(const std::filesystem::path& file);

// "triskelion:v1\n<app>\n<platform>\n<version>\n<sha256>\n<size>", must match server/src/crypto.js.
std::string manifest_message(const Manifest& m);

// Ed25519 check of the manifest signature against the app's embedded public key (base64, 32 bytes).
// `public_keys` is one base64 Ed25519 public key, or several separated by commas (key rotation: the
// build trusts the current key and the next one). The signature must verify against at least one.
// More than kMaxPublicKeys entries, or none, never verifies.
constexpr size_t kMaxPublicKeys = 8;
std::vector<std::string> split_public_keys(std::string_view list);
bool verify_manifest(std::string_view public_keys, const Manifest& m);

}  // namespace triskelion
