#include "agentcad/fabrication.hpp"
#include "agentcad/model.hpp"
#include <array>
#include <cmath>
#include <map>
#include <set>

namespace agentcad {
namespace {
Json object(Json properties,Json required) {
  return {{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};
}
Json positive(double maximum=1e6) {return {{"type","number"},{"exclusiveMinimum",0},{"maximum",maximum}};}
Json nonnegative(double maximum=1e6) {return {{"type","number"},{"minimum",0},{"maximum",maximum}};}
Json list(Json item,std::size_t limit) {return {{"type","array"},{"items",item},{"maxItems",limit}};}
double number(const Json& value,bool zero=false,double limit=1e6) {
  if(!value.is_number())throw Error("invalid_argument","Fabrication limits require numbers");
  const auto n=value.get<double>();
  if(!std::isfinite(n)||n<0||(!zero&&n==0)||n>limit)throw Error("invalid_argument","Fabrication limit is outside its finite range");
  return n;
}
std::array<double,3> vector(const Json& value) {
  if(!value.is_array()||value.size()!=3)throw Error("invalid_argument","Fabrication vectors require three numbers");
  std::array<double,3> v;
  for(int i=0;i<3;++i) {
    if(!value[i].is_number())throw Error("invalid_argument","Fabrication vector must be numeric");
    v[i]=value[i].get<double>();
    if(!std::isfinite(v[i])||std::abs(v[i])>1e6)throw Error("invalid_argument","Fabrication vector is outside its finite range");
  }
  return v;
}
void profile(const Json& value) {
  fields(value,{"process","orientation"},{"build_envelope_mm","minimum_wall_mm","overhang_angle_deg","tool_radius_mm",
    "sheet_thickness_mm","sheet_thickness_tolerance_mm","minimum_draft_deg","parting_plane_mm"});
  const auto process=text_field(value,"process");
  if(process!="fdm"&&process!="cnc"&&process!="sheet_laser"&&process!="molding")throw Error("invalid_argument","Unsupported fabrication process");
  const auto& orientation=value.at("orientation");fields(orientation,{"build_direction","x_direction"},{});
  const auto z=vector(orientation.at("build_direction")),x=vector(orientation.at("x_direction"));
  double zn=0,xn=0,dot=0;for(int i=0;i<3;++i){zn+=z[i]*z[i];xn+=x[i]*x[i];dot+=z[i]*x[i];}
  if(zn<1e-20||xn<1e-20||std::abs(dot)/std::sqrt(zn*xn)>1e-9)
    throw Error("invalid_argument","Build and X directions must be nonzero and perpendicular");
  if(value.contains("build_envelope_mm"))for(const auto n:vector(value.at("build_envelope_mm")))if(n<=0)throw Error("invalid_argument","Build envelope dimensions must be positive");
  for(const auto* key:{"minimum_wall_mm","tool_radius_mm","sheet_thickness_mm"})if(value.contains(key))number(value.at(key));
  if(value.contains("overhang_angle_deg")){number(value.at("overhang_angle_deg"),true,90);if(process!="fdm")throw Error("invalid_argument","Overhang angle is an FDM input");}
  if(value.contains("tool_radius_mm")&&process!="cnc")throw Error("invalid_argument","Tool radius is a CNC input");
  if(value.contains("sheet_thickness_mm")!=value.contains("sheet_thickness_tolerance_mm"))throw Error("invalid_argument","Sheet stock requires both nominal thickness and explicit allowance");
  if(value.contains("sheet_thickness_mm")&&process!="sheet_laser")throw Error("invalid_argument","Sheet stock is a sheet/laser input");
  if(value.contains("sheet_thickness_tolerance_mm"))number(value.at("sheet_thickness_tolerance_mm"),true);
  if(value.contains("minimum_draft_deg")){number(value.at("minimum_draft_deg"),true,89);if(process!="molding")throw Error("invalid_argument","Draft angle is a molding input");}
  if(value.contains("parting_plane_mm")){
    if(process!="molding")throw Error("invalid_argument","Parting plane is a molding input");
    const auto p=vector(Json::array({value.at("parting_plane_mm"),0,0}));(void)p;
  }
}
}
Json fabrication_definitions() {
  const Json point={{"type","array"},{"items",{{"type","number"},{"minimum",-1e6},{"maximum",1e6}}},{"minItems",3},{"maxItems",3}};
  const Json id={{"$ref","#/$defs/model_id"}},ref={{"$ref","#/$defs/fabrication_profile"}};
  const Json status={{"enum",{"pass","fail","unknown"}}};
  const Json check=object({{"id",{{"type","string"}}},{"status",status},{"method",{{"type","string"}}},
    {"reason",{{"type","string"}}},{"evidence",{{"type","object"}}}}, {"id","status","method","reason","evidence"});
  Json definitions={
    {"fabrication_profile",object({{"process",{{"enum",{"fdm","cnc","sheet_laser","molding"}}}},
      {"orientation",object({{"build_direction",point},{"x_direction",point}},{"build_direction","x_direction"})},
      {"build_envelope_mm",{{"type","array"},{"items",positive()},{"minItems",3},{"maxItems",3}}},
      {"minimum_wall_mm",positive()},{"overhang_angle_deg",nonnegative(90)},{"tool_radius_mm",positive()},
      {"sheet_thickness_mm",positive()},{"sheet_thickness_tolerance_mm",nonnegative()},
      {"minimum_draft_deg",nonnegative(89)},{"parting_plane_mm",{{"type","number"},{"minimum",-1e6},{"maximum",1e6}}}}, {"process","orientation"})},
    {"fabrication_options",object({{"profile",ref},
      {"parts",list(object({{"feature_id",id},{"profile",ref}}, {"feature_id","profile"}),fabrication_part_limit)},
      {"minimum_clearance_mm",nonnegative()},
      {"clearance_pairs",list(object({{"a",occurrence_schema()},{"b",occurrence_schema()}},{"a","b"}),fabrication_pair_limit)}}, {"profile"})},
    {"fabrication_report",object({{"schema_version",{{"const",1}}},{"status",status},
      {"coordinate_policy",{{"const","source_features_and_saved_occurrences"}}},
      {"selection_lifetime",{{"const","report"}}},{"geometric_tolerance_mm",{{"const",1e-6}}},
      {"checks",list(check,16)},
      {"parts",list(object({{"feature_id",id},{"quantity",{{"type","integer"},{"minimum",1}}},
        {"part_ids",list(occurrence_schema(),1024)},{"profile",ref},{"status",status},
        {"checks",list(check,32)}}, {"feature_id","quantity","part_ids","profile","status","checks"}),fabrication_part_limit)},
      {"guidance",object({{"basis",{{"const","caller_limits_only"}}},{"sources",{{"type","array"},{"maxItems",0}}}},{"basis","sources"})}},
      {"schema_version","status","coordinate_policy","selection_lifetime","geometric_tolerance_mm","checks","parts","guidance"})}
  };
  auto& schema=definitions["fabrication_profile"];
  schema["dependentRequired"]={{"sheet_thickness_mm",{"sheet_thickness_tolerance_mm"}},{"sheet_thickness_tolerance_mm",{"sheet_thickness_mm"}}};
  schema["allOf"]=Json::array();
  const std::map<std::string,std::string> specific={{"overhang_angle_deg","fdm"},{"tool_radius_mm","cnc"},
    {"sheet_thickness_mm","sheet_laser"},{"sheet_thickness_tolerance_mm","sheet_laser"},
    {"minimum_draft_deg","molding"},{"parting_plane_mm","molding"}};
  for(const auto* process:{"fdm","cnc","sheet_laser","molding"}) {
    Json forbidden=Json::object();for(const auto& [key,owner]:specific)if(owner!=process)forbidden[key]=false;
    schema["allOf"].push_back({{"if",{{"properties",{{"process",{{"const",process}}}}}}},{"then",{{"properties",forbidden}}}});
  }
  return definitions;
}
void validate_fabrication_options(const Json& options) {
  fields(options,{"profile"},{"parts","minimum_clearance_mm","clearance_pairs"});profile(options.at("profile"));
  if(options.contains("minimum_clearance_mm"))number(options.at("minimum_clearance_mm"),true);
  std::set<std::string> seen;
  if(options.contains("parts")) {
    const auto& parts=options.at("parts");if(!parts.is_array()||parts.size()>fabrication_part_limit)throw Error("invalid_argument","Too many part process profiles");
    for(const auto& part:parts){fields(part,{"feature_id","profile"},{});const auto id=text_field(part,"feature_id");model_identifier(id);
      if(!seen.insert(id).second)throw Error("invalid_argument","Duplicate part process profile");profile(part.at("profile"));}
  }
  seen.clear();
  if(options.contains("clearance_pairs")) {
    const auto& pairs=options.at("clearance_pairs");if(!pairs.is_array()||pairs.size()>fabrication_pair_limit)throw Error("invalid_argument","Too many clearance pairs");
    for(const auto& pair:pairs){fields(pair,{"a","b"},{});auto a=text_field(pair,"a"),b=text_field(pair,"b");validate_occurrence_path(a);validate_occurrence_path(b);
      if(a==b)throw Error("invalid_argument","Clearance requires two different occurrences");if(b<a)std::swap(a,b);
      if(!seen.insert(a+"|"+b).second)throw Error("invalid_argument","Repeated clearance pair");}
  }
}
}
