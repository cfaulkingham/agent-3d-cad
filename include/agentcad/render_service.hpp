#pragma once
#include "agentcad/storage.hpp"
namespace agentcad {
Json render_tool_definition();
void validate_render_arguments(const Json&);
Json render_visual_output(Store&,const Json&);
}
