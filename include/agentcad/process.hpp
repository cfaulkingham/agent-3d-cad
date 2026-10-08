#pragma once
#include "agentcad/storage.hpp"
#include <functional>

namespace agentcad {
// Internal fixed-argv native process runner, never a generic MCP execution tool.
Json supervise_process(const Json& request, const fs::path& control_directory,
                       const std::function<void()>& checkpoint);
}
