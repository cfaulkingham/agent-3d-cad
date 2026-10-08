#pragma once
#include "agentcad/json.hpp"

namespace agentcad {
Json robot_options_schema();
void validate_robot_options(const Json& options);
// Pure serialization of native-resolved frames. Does not build geometry or write files.
// Returns XML files, an explicit coordinate/physical-data ledger and mesh sources.
Json robot_description(const Json& model, const Json& frames, const Json& options);
}
