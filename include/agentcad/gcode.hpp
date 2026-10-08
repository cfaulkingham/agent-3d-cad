#pragma once
#include "agentcad/json.hpp"
#include <functional>

namespace agentcad {
inline constexpr std::size_t gcode_bytes_limit = 64 * 1024 * 1024;
Json gcode_definitions();
void validate_gcode_options(const Json& options);
// Static commanded-path inspection only. Never interprets artifact text as code
// to execute, contacts hardware, or claims physical firmware/thermal simulation.
Json inspect_gcode(const std::string& content, const Json& options,
                   const std::function<void()>& checkpoint = {});
}
