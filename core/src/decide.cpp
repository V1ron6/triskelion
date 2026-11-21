#include "triskelion/decide.hpp"

#include <system_error>

#include "triskelion/version.hpp"

namespace triskelion {

const char* to_string(Decision d) {
  switch (d) {
    case Decision::Install: return "install";
    case Decision::RejectBadVersion: return "reject: bad version";
    case Decision::RejectWrongTarget: return "reject: wrong app or platform";
    case Decision::RejectDowngrade: return "reject: not newer than current";
    case Decision::RejectBadSignature: return "reject: bad signature";
    case Decision::RejectFileUnreadable: return "reject: downloaded file unreadable";
    case Decision::RejectSizeMismatch: return "reject: size mismatch";
    case Decision::RejectHashMismatch: return "reject: hash mismatch";
  }
  return "reject: unknown";
}

Decision decide(const Policy& policy, const Manifest& m, const std::filesystem::path& downloaded) {
  const auto current = parse_version(policy.current_version);
  const auto candidate = parse_version(m.version);
  if (!current || !candidate) return Decision::RejectBadVersion;

  // Target comes from the build, never from the server's say-so.
  if (m.app != policy.app || m.platform != policy.platform) return Decision::RejectWrongTarget;

  if (compare(*candidate, *current) <= 0) return Decision::RejectDowngrade;

  // Signature before touching the bytes: it covers app, platform, version, hash and size.
  if (!verify_manifest(policy.public_key_b64, m)) return Decision::RejectBadSignature;

  std::error_code ec;
  const auto size = std::filesystem::file_size(downloaded, ec);
  if (ec) return Decision::RejectFileUnreadable;
  if (size != m.size) return Decision::RejectSizeMismatch;

  const auto hash = sha256_file_hex(downloaded);
  if (!hash) return Decision::RejectFileUnreadable;
  if (*hash != m.sha256_hex) return Decision::RejectHashMismatch;

  return Decision::Install;
}

}  // namespace triskelion
