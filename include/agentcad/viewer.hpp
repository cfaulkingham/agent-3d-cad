#pragma once
#include "agentcad/json.hpp"

namespace agentcad {
// Standalone, offline browser artifact. The embedded payload is data, never code.
std::string viewer_html(const Json& evaluation);
}
