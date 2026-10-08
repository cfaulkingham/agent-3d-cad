#include "agentcad/section.hpp"
#include "agentcad/model.hpp"
#include <cmath>
#include <set>

namespace agentcad {
namespace {
Json object(Json properties){Json required=Json::array();for(const auto& item:properties.items())required.push_back(item.key());return {{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};}
Json array(Json item,std::size_t maximum,std::size_t minimum=0){return {{"type","array"},{"items",item},{"minItems",minimum},{"maxItems",maximum}};}
void unit(const Json& value){
  if(!value.is_array()||value.size()!=3)throw Error("invalid_argument","Section normals and directions need three components");
  double square=0;for(const auto& v:value){const auto n=number(v);if(std::abs(n)>1)throw Error("invalid_argument","Section direction components exceed one");square+=n*n;}
  if(std::abs(square-1)>1e-6)throw Error("invalid_argument","Section normals and directions must be unit vectors");
}
}
Json section_definitions(){
  const Json scalar={{"type","number"}},nonnegative={{"type","number"},{"minimum",0}},point=array(scalar,3,3),count={{"type","integer"},{"minimum",0},{"maximum",section_entity_limit}};
  const Json part={{"type","string"},{"minLength",1},{"maxLength",519},{"pattern","^[A-Za-z][A-Za-z0-9_-]{0,63}(/[A-Za-z][A-Za-z0-9_-]{0,63}){0,7}$"}},owner={{"type",{"string","null"}},{"minLength",1},{"maxLength",519},{"pattern","^[A-Za-z][A-Za-z0-9_-]{0,63}(/[A-Za-z][A-Za-z0-9_-]{0,63}){0,7}$"}},unit_vector=array({{"type","number"},{"minimum",-1},{"maximum",1}},3,3);
  const Json plane=object({{"normal",unit_vector},{"offset_mm",{{"type","number"},{"minimum",-1e12},{"maximum",1e12}}}});
  const Json explode=object({{"distance_mm",{{"type","number"},{"minimum",0},{"maximum",1e6}}},{"directions",array(object({{"part_id",part},{"direction",unit_vector}}),section_part_limit)}});
  auto query=object({{"action",{{"const","section"}}},{"plane",plane},{"explode",explode},{"part_ids",array(part,section_part_limit,1)}});query["required"]={"action","plane"};
  const Json cap=object({{"id",{{"type","string"},{"pattern","^cap-[1-9][0-9]*$"}}},{"part_id",owner},{"solid_index",{{"type","integer"},{"minimum",1},{"maximum",section_part_limit}}},
    {"area_mm2",nonnegative},{"perimeter_mm",nonnegative},{"center_mm",point},{"wire_count",count}});
  auto curve=object({{"id",{{"type","string"},{"pattern","^section-[1-9][0-9]*$"}}},{"part_id",owner},{"solid_index",{{"type","integer"},{"minimum",1},{"maximum",section_part_limit}}},
    {"curve_kind",{{"enum",{"line","circle","ellipse","hyperbola","parabola","bezier","bspline","offset","other"}}}},{"length_mm",nonnegative},{"center_mm",point},{"bounds_mm",object({{"min",point},{"max",point}})},
    {"degenerate",{{"type","boolean"}}},{"points",array(point,section_point_limit)},{"direction",point},{"axis",point},{"radius_mm",nonnegative}});
  curve["required"]={"id","part_id","solid_index","curve_kind","length_mm","center_mm","bounds_mm","degenerate","points"};
  const Json status={{"enum",{"empty","tangent","area"}}};
  const Json scope=object({{"part_id",owner},{"source_plane_offset_mm",scalar},{"displacement_mm",point},{"area_mm2",nonnegative},{"boundary_length_mm",nonnegative},
    {"region_count",count},{"curve_count",count},{"contact_points",array(point,section_entity_limit)},{"status",status}});
  const Json index={{"type","integer"},{"minimum",0},{"maximum",section_vertex_limit-1}};
  const Json mesh=object({{"positions",array(point,section_vertex_limit)},{"triangles",array(array(index,3,3),section_triangle_limit)},
    {"triangle_regions",array({{"type","string"},{"pattern","^cap-[1-9][0-9]*$"}},section_triangle_limit)},{"linear_deflection_mm",{{"const",section_deflection_mm}}}});
  const Json report=object({{"schema_version",{{"const",1}}},{"units",{{"const","mm"}}},{"action",{{"const","section"}}},{"method",{{"const","native_BRep_planar_section"}}},
    {"coordinate_space",{{"const","committed_source_pose"}}},{"plane_coordinate_space",{{"const","displayed_world_mm"}}},
    {"coverage",{{"enum",{"feature_solids","all_assembly_leaves","explicit_leaf_subset"}}}},{"area_semantics",{{"const","sum_of_solid_sections"}}},
    {"plane",plane},{"explode",explode},{"sections",array(scope,section_part_limit,1)},{"regions",array(cap,section_entity_limit)},
    {"curves",array(curve,section_entity_limit)},{"mesh",mesh},{"area_mm2",nonnegative},{"boundary_length_mm",nonnegative},{"status",status},
    {"selection_lifetime",{{"const","section_result"}}},{"tolerance_mm",{{"const",1e-7}}},{"point_tolerance_mm",{{"const",section_point_tolerance_mm}}}});
  return {{"section_query",query},{"section_report",report}};
}
void validate_section_query(const Json& query){
  fields(query,{"action","plane"},{"explode","part_ids"});if(text_field(query,"action")!="section")throw Error("invalid_argument","Expected a section query");
  const auto& plane=query.at("plane");fields(plane,{"normal","offset_mm"});unit(plane.at("normal"));
  const auto& offset=plane.at("offset_mm");if(!offset.is_number()||!std::isfinite(offset.get<double>())||std::abs(offset.get<double>())>1e12)throw Error("invalid_argument","Section plane offset exceeds coordinate bounds");
  if(query.contains("explode")){
    const auto& e=query.at("explode");fields(e,{"distance_mm","directions"});const auto d=number(e.at("distance_mm"));if(d<0||d>1e6)throw Error("invalid_argument","Section exploded distance exceeds bounds");
    const auto& values=e.at("directions");if(!values.is_array()||values.size()>section_part_limit)throw Error("invalid_argument","Section directions exceed leaf bounds");
    std::set<std::string> seen;for(const auto& item:values){fields(item,{"part_id","direction"});const auto id=text_field(item,"part_id");validate_occurrence_path(id);unit(item.at("direction"));if(!seen.insert(id).second)throw Error("invalid_argument","Section direction leaf IDs must be unique");}
  }
  if(query.contains("part_ids")){
    const auto& values=query.at("part_ids");if(!values.is_array()||values.empty()||values.size()>section_part_limit)throw Error("invalid_argument","Section subset needs 1 to 1024 leaf paths");
    std::set<std::string> seen;for(const auto& value:values){if(!value.is_string())throw Error("invalid_argument","Section leaf IDs must be strings");const auto id=value.get<std::string>();validate_occurrence_path(id);if(!seen.insert(id).second)throw Error("invalid_argument","Section leaf IDs must be unique");}
  }
}
}
