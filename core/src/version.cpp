#include "triskelion/version.hpp"

#include <cctype>
#include <string>

namespace triskelion {

namespace {
bool parse_part(std::string_view s, unsigned& out) {
  if (s.empty() || s.size() > 9) return false;
  unsigned v = 0;
  for (char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    v = v * 10 + static_cast<unsigned>(c - '0');
  }
  out = v;
  return true;
}
}  // namespace

std::optional<Version> parse_version(std::string_view s) {
  const auto d1 = s.find('.');
  if (d1 == std::string_view::npos) return std::nullopt;
  const auto d2 = s.find('.', d1 + 1);
  if (d2 == std::string_view::npos) return std::nullopt;
  Version v;
  if (!parse_part(s.substr(0, d1), v.major)) return std::nullopt;
  if (!parse_part(s.substr(d1 + 1, d2 - d1 - 1), v.minor)) return std::nullopt;
  if (!parse_part(s.substr(d2 + 1), v.patch)) return std::nullopt;  // a 4th ".n" fails the digit check
  return v;
}

int compare(const Version& a, const Version& b) {
  if (a.major != b.major) return a.major < b.major ? -1 : 1;
  if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
  if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
  return 0;
}

}  // namespace triskelion
