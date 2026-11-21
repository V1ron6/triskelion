#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace triskelion {

struct JsonNull {};
using JsonValue = std::variant<JsonNull, bool, std::int64_t, std::string>;
using JsonObject = std::map<std::string, JsonValue>;

// Parser for the one shape the server sends: a flat object of strings, integers, booleans and null.
// Rejects nesting, floats, duplicate keys, control characters in strings, lone surrogates,
// trailing data and input over 64 KiB, so there is nothing clever for a hostile response to hit.
std::optional<JsonObject> parse_flat_json(std::string_view text);

// Quote and escape a string for use inside JSON we build ourselves.
std::string json_quote(std::string_view s);

}  // namespace triskelion
