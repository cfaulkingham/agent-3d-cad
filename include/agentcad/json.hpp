#pragma once
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <initializer_list>

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
void identifier(const std::string& value);
double number(const Json& value);
std::uint64_t revision_number(const Json& value);
}
