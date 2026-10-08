#include "agentcad/measurement.hpp"
#include "agentcad/model.hpp"
#include "agentcad/section.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace agentcad {
namespace {
Json object(Json properties,Json required){return {{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};}
Json array(Json item,std::size_t maximum,std::size_t minimum=0){return {{"type","array"},{"items",item},{"minItems",minimum},{"maxItems",maximum}};}
void target(const Json& value){
  const auto kind=text_field(value,"kind");
  if(kind=="part"){fields(value,{"kind","part_id"});validate_occurrence_path(text_field(value,"part_id"));}
  else if(kind=="face"||kind=="edge"){
    fields(value,{"kind","entity_id"});const auto id=text_field(value,"entity_id"),prefix=kind+"-";
    if(!id.starts_with(prefix)||id.size()>32||id.size()==prefix.size()||id[prefix.size()]=='0'||
      id.find_first_not_of("0123456789",prefix.size())!=std::string::npos)throw Error("invalid_argument","Measurement requires an evaluated face or edge ID");
  }else throw Error("invalid_argument","Measurement targets must be faces, edges or leaf occurrences");
}
bool equal(const Json& a,const Json& b){
  if(a.is_number()&&b.is_number()){
    const double x=a.get<double>(),y=b.get<double>();return std::isfinite(x)&&std::isfinite(y)&&std::abs(x-y)<=1e-7+1e-9*std::max(std::abs(x),std::abs(y));
  }
  if(a.type()!=b.type())return false;
  if(a.is_array()){if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i)if(!equal(a[i],b[i]))return false;return true;}
  if(a.is_object()){if(a.size()!=b.size())return false;for(const auto& item:a.items())if(!b.contains(item.key())||!equal(item.value(),b.at(item.key())))return false;return true;}
  return a==b;
}
}
Json measurement_definitions(){
  const Json id={{"$ref","#/$defs/model_id"}},part={{"type","string"},{"minLength",1},{"maxLength",519},{"pattern","^[A-Za-z][A-Za-z0-9_-]{0,63}(/[A-Za-z][A-Za-z0-9_-]{0,63}){0,7}$"}};
  const Json scalar={{"type","number"}},nonnegative={{"type","number"},{"minimum",0}},point=array(scalar,3,3);
  const Json target_schema={{"oneOf",Json::array({object({{"kind",{{"const","part"}}},{"part_id",part}},{"kind","part_id"}),
    object({{"kind",{{"const","face"}}},{"entity_id",{{"type","string"},{"maxLength",32},{"pattern","^face-[1-9][0-9]*$"}}}},{"kind","entity_id"}),
    object({{"kind",{{"const","edge"}}},{"entity_id",{{"type","string"},{"maxLength",32},{"pattern","^edge-[1-9][0-9]*$"}}}},{"kind","entity_id"})})}};
  const Json threshold={{"type","number"},{"minimum",0},{"maximum",1e6}};
  const Json query={{"oneOf",Json::array({object({{"action",{{"const","pair"}}},{"targets",array({{"$ref","#/$defs/measurement_target"}},2,2)},{"minimum_clearance_mm",threshold}},{"action","targets"}),
    object({{"action",{{"const","clearance"}}},{"part_ids",array(part,measurement_part_limit,2)},{"minimum_clearance_mm",threshold}},{"action"})})}};
  const Json nullable={{"type",Json::array({"number","null"})},{"minimum",0}};
  const Json pair=object({{"targets",array({{"$ref","#/$defs/measurement_target"}},2,2)},{"distance_mm",nonnegative},
    {"witnesses",array(object({{"a_mm",point},{"b_mm",point}},{"a_mm","b_mm"}),measurement_witness_limit,1)},
    {"solution_count",{{"type","integer"},{"minimum",1}}},{"witnesses_truncated",{{"type","boolean"}}},
    {"rejected_witness_count",{{"type","integer"},{"minimum",0},{"maximum",measurement_witness_limit}}},
    {"intersection_volume_mm3",nullable},{"interference",{{"type",Json::array({"boolean","null"})}}},
    {"angle_deg",{{"type","number"},{"minimum",0},{"maximum",90}}},{"angle_method",{{"enum",{"unoriented_plane_normals","unoriented_line_directions","line_to_plane"}}}}},
    {"targets","distance_mm","witnesses","solution_count","rejected_witness_count","witnesses_truncated","intersection_volume_mm3","interference"});
  const Json report=object({{"schema_version",{{"const",1}}},{"units",{{"const","mm"}}},{"action",{{"enum",{"pair","clearance"}}}},
    {"method",{{"const","exact_BRep_minimum_distance"}}},{"coordinate_space",{{"const","committed_source_pose"}}},
    {"coverage",{{"enum",{"explicit_pair","all_assembly_leaves","explicit_leaf_subset"}}}},{"part_ids",array(part,measurement_part_limit)},
    {"pairs",array(pair,measurement_pair_limit,1)},{"minimum_distance_mm",nonnegative},{"interference_count",{{"type","integer"},{"minimum",0},{"maximum",measurement_pair_limit}}},
    {"status",{{"enum",{"measured","pass","fail"}}}},{"minimum_clearance_mm",threshold},
    {"distance_tolerance_mm",{{"const",measurement_distance_tolerance}}},{"intersection_volume_tolerance_mm3",{{"const",measurement_volume_tolerance}}}},
    {"schema_version","units","action","method","coordinate_space","coverage","part_ids","pairs","minimum_distance_mm","interference_count","status","distance_tolerance_mm","intersection_volume_tolerance_mm3"});
  const Json result=object({{"document_id",id},{"revision",{{"$ref","#/$defs/revision"}}},{"evaluation_id",id},{"feature_id",id},
    {"kernel_version",{{"const","8.0.1"}}},{"model_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},{"native_build",{{"type","string"}}},
    {"report",{{"$ref","#/$defs/measurement_report"}}}}, {"document_id","revision","evaluation_id","feature_id","kernel_version","model_sha256","native_build","report"});
  auto definitions=section_definitions();definitions.update({{"measurement_target",target_schema},{"measurement_distance_query",query},{"measurement_distance_report",report},{"measurement_result",result}});
  auto distance_result=result;distance_result["properties"]["report"]={{"$ref","#/$defs/measurement_distance_report"}};
  definitions["measurement_distance_result"]=std::move(distance_result);
  definitions["measurement_query"]={{"oneOf",Json::array({{{"$ref","#/$defs/measurement_distance_query"}},{{"$ref","#/$defs/section_query"}}})}};
  definitions["measurement_report"]={{"oneOf",Json::array({{{"$ref","#/$defs/measurement_distance_report"}},{{"$ref","#/$defs/section_report"}}})}};
  return definitions;
}
void validate_measurement_query(const Json& query){
  const auto action=text_field(query,"action");
  if(action=="section"){validate_section_query(query);return;}
  if(action=="pair"){
    fields(query,{"action","targets"},{"minimum_clearance_mm"});const auto& values=query.at("targets");
    if(!values.is_array()||values.size()!=2)throw Error("invalid_argument","A measurement pair needs exactly two targets");
    for(const auto& value:values)target(value);
    if(values[0]==values[1])throw Error("invalid_argument","Choose two distinct measurement targets");
  }else if(action=="clearance"){
    fields(query,{"action"},{"part_ids","minimum_clearance_mm"});
    if(query.contains("part_ids")){
      const auto& values=query.at("part_ids");if(!values.is_array()||values.size()<2||values.size()>measurement_part_limit)throw Error("invalid_argument","Clearance requires between 2 and 23 distinct leaf occurrences");
      std::set<std::string> seen;for(const auto& value:values){if(!value.is_string())throw Error("invalid_argument","Clearance leaf IDs must be strings");const auto id=value.get<std::string>();validate_occurrence_path(id);if(!seen.insert(id).second)throw Error("invalid_argument","Clearance leaf IDs must be unique");}
    }
  }else throw Error("invalid_argument","Measurement action must be pair, clearance or section");
  if(query.contains("minimum_clearance_mm")){const auto value=number(query.at("minimum_clearance_mm"));if(value<0||value>1e6)throw Error("invalid_argument","Minimum clearance must be within 0 and 1000000 mm");}
}
bool measurement_descriptor_matches(const Json& expected,const Json& actual){
  auto a=expected,b=actual;for(const auto* key:{"id","selector"}){a.erase(key);b.erase(key);}return equal(a,b);
}
}
