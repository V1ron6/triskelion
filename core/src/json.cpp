#include "triskelion/json.hpp"

#include <cstdio>

namespace triskelion {

namespace {

constexpr std::size_t kMaxInput = 64 * 1024;

struct Parser {
  std::string_view s;
  std::size_t i = 0;

  void ws() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
  }
  bool eat(char c) {
    ws();
    if (i < s.size() && s[i] == c) { i++; return true; }
    return false;
  }
  bool literal(std::string_view lit) {
    if (s.substr(i, lit.size()) != lit) return false;
    i += lit.size();
    return true;
  }

  static void put_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  bool hex4(unsigned& out) {
    if (i + 4 > s.size()) return false;
    unsigned v = 0;
    for (int k = 0; k < 4; k++) {
      const char c = s[i + k];
      unsigned d;
      if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
      else return false;
      v = v * 16 + d;
    }
    i += 4;
    out = v;
    return true;
  }

  bool string(std::string& out) {
    ws();
    if (i >= s.size() || s[i] != '"') return false;
    i++;
    out.clear();
    while (i < s.size()) {
      const unsigned char c = static_cast<unsigned char>(s[i++]);
      if (c == '"') return true;
      if (c < 0x20) return false;  // raw control characters are not valid JSON
      if (c != '\\') { out.push_back(static_cast<char>(c)); continue; }
      if (i >= s.size()) return false;
      const char e = s[i++];
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          unsigned cp;
          if (!hex4(cp)) return false;
          if (cp >= 0xDC00 && cp <= 0xDFFF) return false;  // lone low surrogate
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            unsigned lo;
            if (!literal("\\u") || !hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          put_utf8(out, cp);
          break;
        }
        default: return false;
      }
    }
    return false;  // unterminated
  }

  bool integer(std::int64_t& out) {
    const std::size_t start = i;
    if (i < s.size() && s[i] == '-') i++;
    const std::size_t digits = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
    const std::size_t n = i - digits;
    if (n == 0 || n > 18) return false;
    if (n > 1 && s[digits] == '0') return false;  // leading zeros
    if (i < s.size() && (s[i] == '.' || s[i] == 'e' || s[i] == 'E')) return false;  // floats not accepted
    std::int64_t v = 0;
    for (std::size_t k = digits; k < i; k++) v = v * 10 + (s[k] - '0');
    out = (s[start] == '-') ? -v : v;
    return true;
  }

  bool value(JsonValue& out) {
    ws();
    if (i >= s.size()) return false;
    const char c = s[i];
    if (c == '"') { std::string str; if (!string(str)) return false; out = std::move(str); return true; }
    if (c == '-' || (c >= '0' && c <= '9')) { std::int64_t n; if (!integer(n)) return false; out = n; return true; }
    if (c == 't') { if (!literal("true")) return false; out = true; return true; }
    if (c == 'f') { if (!literal("false")) return false; out = false; return true; }
    if (c == 'n') { if (!literal("null")) return false; out = JsonNull{}; return true; }
    return false;  // objects and arrays inside the object are not accepted
  }
};

}  // namespace

std::optional<JsonObject> parse_flat_json(std::string_view text) {
  if (text.size() > kMaxInput) return std::nullopt;
  Parser p{text};
  if (!p.eat('{')) return std::nullopt;
  JsonObject obj;
  if (p.eat('}')) {
    p.ws();
    return p.i == text.size() ? std::optional<JsonObject>(obj) : std::nullopt;
  }
  while (true) {
    std::string key;
    JsonValue val;
    if (!p.string(key) || !p.eat(':') || !p.value(val)) return std::nullopt;
    if (!obj.emplace(std::move(key), std::move(val)).second) return std::nullopt;  // duplicate key
    if (p.eat(',')) continue;
    if (p.eat('}')) break;
    return std::nullopt;
  }
  p.ws();
  if (p.i != text.size()) return std::nullopt;
  return obj;
}

std::string json_quote(std::string_view s) {
  std::string out = "\"";
  for (const unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
  return out;
}

}  // namespace triskelion
