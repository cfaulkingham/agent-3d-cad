#pragma once
#include "agentcad/json.hpp"
#include <vector>

namespace agentcad {
struct HelpGuide { std::string path, text; };
// Generated from reviewed repository documentation at build time. No runtime
// paths, network, interpreter or installed source tree are required.
const std::vector<HelpGuide>& help_guides();
Json help_tool_definition();
void validate_help_arguments(const Json& args);
Json native_help(const Json& args);
}
