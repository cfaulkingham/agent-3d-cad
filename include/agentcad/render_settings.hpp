#pragma once
#include "agentcad/json.hpp"
namespace agentcad {
// Closed JSON settings shared by viewer persistence and native visual outputs.
// Missing fields inherit the selected mode's defaults; no caller code executes.
Json normalize_render_settings(const Json& value=Json::object());
Json render_settings_schema();
Json normalize_render_material(const Json& value=Json::object());
Json render_material_schema();
}
