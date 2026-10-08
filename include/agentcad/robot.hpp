#pragma once
#include "agentcad/json.hpp"

namespace agentcad {
inline constexpr std::size_t robot_coordinate_limit = 4096;
inline constexpr std::size_t robot_link_limit = 8192;
// Flat names remain compatible. Nested names encode segment lengths to avoid
// collisions with identifiers that already contain underscores.
std::string robot_name(const std::string& kind, const std::string& occurrence);
Json composed_robot_motion(const Json& model, const std::string& assembly_id);
Json robot_options_schema();
void validate_robot_options(const Json& options);
// Pure serialization of native-resolved frames. Does not build geometry or write files.
// Returns XML files, an explicit coordinate/physical-data ledger and mesh sources.
Json robot_description(const Json& model, const Json& frames, const Json& options);
}
