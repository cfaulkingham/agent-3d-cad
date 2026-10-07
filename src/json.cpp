#include "agentcad/json.hpp"
#include <cmath>
#include <set>

namespace agentcad {
Error::Error(std::string c, std::string message, Json d)
    : std::runtime_error(std::move(message)), code(std::move(c)), details(std::move(d)) {}
Json Error::json() const { return {{"code", code}, {"message", what()}, {"details", details}}; }

Json parse_json(const std::string& text, std::size_t max_bytes) {
  if (text.size() > max_bytes) throw Error("limit_exceeded", "JSON exceeds its byte limit");
  try {
    return Json::parse(text, [](int depth, Json::parse_event_t, Json&) {
      if (depth > 64) throw Error("limit_exceeded", "JSON nesting exceeds 64 levels");
      return true;
    });
  } catch (const Json::exception& e) { throw Error("invalid_json", e.what()); }
}

void fields(const Json& value, std::initializer_list<const char*> required,
            std::initializer_list<const char*> optional) {
  if (!value.is_object()) throw Error("invalid_argument", "Expected an object");
  std::set<std::string> allowed;
  for (const auto* key : required) {
    allowed.insert(key);
    if (!value.contains(key)) throw Error("invalid_argument", "Missing field: " + std::string(key));
  }
  for (const auto* key : optional) allowed.insert(key);
  for (const auto& [key, unused] : value.items()) {
    (void)unused;
    if (!allowed.contains(key)) throw Error("invalid_argument", "Unknown field: " + key);
  }
}

std::string text_field(const Json& value, const std::string& key) {
  if (!value.contains(key) || !value.at(key).is_string())
    throw Error("invalid_argument", "Expected string field: " + key);
  return value.at(key).get<std::string>();
}

void model_identifier(const std::string& value) {
  const auto alpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
  if (value.empty() || value.size() > 64 || !alpha(value[0]))
    throw Error("invalid_argument", "Identifiers must start with a letter and contain 1–64 ASCII letters, digits, _ or -");
  for (const char c : value)
    if (!alpha(c) && !(c >= '0' && c <= '9') && c != '_' && c != '-')
      throw Error("invalid_argument", "Invalid identifier: " + value);
}

void identifier(const std::string& value) { model_identifier(value); }

void portable_identifier(const std::string& value) {
  identifier(value);
  // These names become files and directories (documents/<id>, .locks/<id>.lock,
  // jobs/<id>, views/<id>). Identifiers cannot contain '.', so only the bare
  // name needs checking.
  std::string upper = value;
  for (auto& c : upper) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL" ||
      (upper.size() == 4 && (upper.starts_with("COM") || upper.starts_with("LPT")) && upper[3] >= '0' && upper[3] <= '9'))
    throw Error("invalid_argument", "Identifier is a reserved Windows device name (CON, PRN, AUX, NUL, COM0-9, LPT0-9): " + value);
}

double number(const Json& value) {
  if (!value.is_number()) throw Error("invalid_argument", "Expected a number");
  const auto result = value.get<double>();
  if (!std::isfinite(result) || std::abs(result) > 1e6)
    throw Error("invalid_argument", "Numbers must be finite and within +/-1000000");
  return result;
}

std::uint64_t revision_number(const Json& value) {
  if ((!value.is_number_unsigned() && !value.is_number_integer()) || value < 1 || value > 9007199254740991ULL)
    throw Error("invalid_argument", "Revision must be a positive safe integer");
  return value.get<std::uint64_t>();
}
// Strict UTF-8 (Unicode Table 3-7): no overlongs, surrogates or values beyond
// U+10FFFF, matching what JSON serialization accepts.
std::optional<std::size_t> invalid_utf8_offset(std::string_view text) {
  const auto byte = [&](std::size_t index) { return static_cast<unsigned char>(text[index]); };
  for (std::size_t i = 0; i < text.size();) {
    const auto lead = byte(i);
    if (lead < 0x80) { ++i; continue; }
    std::size_t length = 0; unsigned char low = 0x80, high = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF) length = 2;
    else if (lead >= 0xE0 && lead <= 0xEF) { length = 3; if (lead == 0xE0) low = 0xA0; if (lead == 0xED) high = 0x9F; }
    else if (lead >= 0xF0 && lead <= 0xF4) { length = 4; if (lead == 0xF0) low = 0x90; if (lead == 0xF4) high = 0x8F; }
    else return i;
    if (i + length > text.size()) return i;
    if (byte(i + 1) < low || byte(i + 1) > high) return i;
    for (std::size_t k = 2; k < length; ++k) if (byte(i + k) < 0x80 || byte(i + k) > 0xBF) return i;
    i += length;
  }
  return std::nullopt;
}
}
