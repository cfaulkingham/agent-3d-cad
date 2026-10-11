#pragma once
#include "agentcad/storage.hpp"
namespace agentcad {
Json visual_appearance_schema();
Json visual_animation_schema();
void validate_visual_animation(const Json& value);
// Capture an immutable declarative sequence, qualified by requested model hash,
// feature and revision. Subsequent view/source edits cannot rebind the recipe.
Json capture_visual_sequence(Store&,const Json& value,const Json& record,const std::string& feature,bool allow_clipping=false);
Json visual_sequence_sample(const Json& sequence,double time);
Json visual_sequence_operations(const Json& joints,const Json& model,const std::string& feature);
Json visual_sample_times(const Json& sequence,double rate,std::size_t limit);
// GLB can represent rigid exploded leaves; a clipping plane is not a rigid pose.
Json visual_pose_parts(const Json& summary,const Json& presentation);
}
