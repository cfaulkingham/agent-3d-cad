#pragma once
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace agentcad {
using Json = nlohmann::json;
inline constexpr std::size_t max_json_bytes = 1024 * 1024;

class Error : public std::runtime_error {
public:
  std::string code;
  Json details;
  Error(std::string code, std::string message, Json details = Json::object());
  Json json() const;
};

Json parse_json(const std::string& text, std::size_t max_bytes = max_json_bytes);
void fields(const Json& value, std::initializer_list<const char*> required,
            std::initializer_list<const char*> optional = {});
std::string text_field(const Json& value, const std::string& key);
// Grammar only, for names inside a model (features, parameters, parts, mates).
void model_identifier(const std::string& value);
// Grammar plus portability, for IDs that name workspace files (documents,
// requests/jobs, views, evaluations): Windows device names are rejected.
void identifier(const std::string& value);
double number(const Json& value);
std::uint64_t revision_number(const Json& value);
std::optional<std::size_t> invalid_utf8_offset(std::string_view text);
}
