#pragma once
#include "agentcad/json.hpp"
namespace agentcad {
inline constexpr std::size_t authoring_source_bytes = 4 * 1024 * 1024;
inline constexpr std::size_t authoring_font_bytes = 8 * 1024 * 1024;
// These parsers produce bounded exact planar curve descriptions, never shapes.
// Font bytes and imported text remain the authoritative immutable source.
Json authoring_profile_schemas(const Json& scalar_schema);
void validate_authoring_profile(const Json& profile, const Json& parameters);
Json authoring_contours(const Json& profile, const Json& parameters);
Json capture_sketch_source(const Json& args, const Json& parameters = Json::object());
bool authoring_source_field(const Json& object, const std::string& key);
std::string encode_font_bytes(const std::string& bytes);
std::string decode_font_bytes(const std::string& encoded);
}
