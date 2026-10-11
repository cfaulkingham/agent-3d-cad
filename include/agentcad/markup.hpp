#pragma once
#include "agentcad/json.hpp"

namespace agentcad {
// Review-only normalized viewport coordinates. Does not create topology or edit a model.
Json markup_scene_schema();
Json markup_context_schema();
void validate_markup_scene(const Json& scene);
void validate_markup_binding(const Json& binding);
// Native callers derive source/frame from their locked live state. Viewport is
// client display geometry; omit its comparison when reconciling native context.
bool markup_matches(const Json& scene, const Json& binding, bool compare_viewport = true);
Json markup_context(const Json& scene, const Json& binding);
}
