#pragma once
#include "agentcad/json.hpp"
#include <array>

namespace agentcad {
Json model_definitions();
// Replaces a standalone schema's $defs with only the definitions it reaches
// through "#/$defs/<name>" references (transitively). Drops an unused $defs.
void prune_definitions(Json& schema);
void validate_model(const Json& model);
double scalar(const Json& value, const Json& parameters, const std::string& unit = "mm");
std::array<double, 3> vector3(const Json& value, const Json& parameters, const std::string& unit = "mm");
Json apply_operations(const Json& model, const Json& operations);
// Pure document semantics: validates motion declarations, resolves coupled
// coordinates and all saved poses, and returns evaluated bounded DOFs.
Json assembly_motion(const Json& feature, const Json& parameters);
}
