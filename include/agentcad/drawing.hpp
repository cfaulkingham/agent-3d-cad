#pragma once
#include "agentcad/json.hpp"

namespace agentcad {
// drawing_schema is embedded in a tool schema with model_definitions() at $defs.
Json drawing_schema();
Json normalize_drawing(const Json& spec, const Json& model);
// Projection JSON contains model-millimeter entities; no OCCT state crosses here.
// Returned contents are native vector artifacts, ready for atomic publication.
Json render_drawing(const Json& projected, const Json& normalized, const Json& identity);
}
