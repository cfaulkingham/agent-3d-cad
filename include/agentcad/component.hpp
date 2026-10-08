#pragma once
#include "agentcad/json.hpp"
#include <functional>

namespace agentcad {
// The service supplies immutable, explicitly numbered records. Document/kernel
// evaluation never follows workspace references or depends on source files.
using ComponentResolver = std::function<Json(const std::string&,std::uint64_t)>;
Json component_definitions();
void validate_components(const Json& model);
Json component_status(const Json& model);
bool apply_component_operation(Json& candidate,const Json& operation,const ComponentResolver& resolve);
}
