#pragma once
#include <optional>
#include <string_view>

namespace triskelion {

struct Version {
  unsigned major = 0, minor = 0, patch = 0;
};

// Strict "x.y.z": digits only, 1-9 digits per part. Same rule as the server.
std::optional<Version> parse_version(std::string_view s);

// -1, 0, 1
int compare(const Version& a, const Version& b);

}  // namespace triskelion
