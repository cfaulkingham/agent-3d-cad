#pragma once
#include "agentcad/json.hpp"
#include "agentcad/component.hpp"
#include <array>

namespace agentcad {
Json model_definitions();
void validate_face_selector(const Json& selector, const Json& parameters, const std::string& input);
void validate_face_selection(const Json& selection, const Json& parameters, const std::string& input);
void validate_step_solid_indices(const Json& indices);
inline constexpr std::size_t assembly_leaf_limit = 1024;
inline constexpr std::size_t assembly_depth_limit = 8;
Json occurrence_schema();
void validate_occurrence_path(const std::string& path);
// Validated, deterministic preorder; each node identifies a source feature and
// an occurrence path, independent of geometry enumeration or cached objects.
Json assembly_structure(const Json& model, const std::string& assembly_id = "");
// Replaces a standalone schema's $defs with only the definitions it reaches
// through "#/$defs/<name>" references (transitively). Drops an unused $defs.
void prune_definitions(Json& schema);
void validate_model(const Json& model);
// Supplier identity is caller-supplied; an imported part additionally binds it
// to the exact embedded STEP bytes. This performs no network access.
void validate_purchase(const Json& purchase, bool require_artifact = false);
// Borrowed pointer to an opaque STEP source, following only rigid
// transform/instance features. Geometry-changing features do not inherit an
// unchanged purchased-part identity. The caller owns the validated model.
const Json* imported_step_source(const Json& model, const std::string& feature_id);
double scalar(const Json& value, const Json& parameters, const std::string& unit = "mm");
std::array<double, 3> vector3(const Json& value, const Json& parameters, const std::string& unit = "mm");
Json apply_operations(const Json& model, const Json& operations, const ComponentResolver& resolve = {});
// Pure document semantics: validates motion declarations, resolves coupled
// coordinates and all saved poses, and returns evaluated bounded DOFs.
Json assembly_motion(const Json& feature, const Json& parameters);
// Unique reachable mechanism definitions, with every affected occurrence path.
Json assembly_mechanisms(const Json& model, const std::string& assembly_id);
}
