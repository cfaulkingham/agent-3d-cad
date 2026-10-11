#pragma once
#include "agentcad/json.hpp"
namespace agentcad {
// Shared closed contracts; source coordinates are mm and IDs remain evaluation-local.
inline void progressive_definitions(Json& definitions) {
  const auto object=[](Json properties,Json required){return Json{{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};};
  const auto array=[](Json item,int maximum){return Json{{"type","array"},{"items",item},{"maxItems",maximum}};};
  const Json id={{"$ref","#/$defs/model_id"}},hash={{"type","string"},{"pattern","^[a-f0-9]{64}$"}},number={{"type","number"}},level={{"type","integer"},{"minimum",0},{"maximum",2}};
  auto point=array(number,3);point["minItems"]=3;const auto bounds=object({{"min",point},{"max",point}},{"min","max"});
  auto transform=array(number,16);transform["minItems"]=16;
  auto topology=definitions.at("topology");for(const auto* name:{"faces","edges"})topology["properties"][name]["maxItems"]=100000;definitions["progressive_topology"]=topology;
  auto mesh=definitions.at("mesh");mesh["properties"]["linear_deflection_mm"]={{"type","number"},{"exclusiveMinimum",0},{"maximum",1000000}};mesh["properties"]["angular_deflection_rad"]={{"type","number"},{"exclusiveMinimum",0},{"maximum",1}};definitions["component_mesh_data"]=mesh;
  const auto detail=object({{"level",level},{"linear_deflection_mm",mesh["properties"]["linear_deflection_mm"]},{"angular_deflection_rad",mesh["properties"]["angular_deflection_rad"]}},{"level","linear_deflection_mm","angular_deflection_rad"});
  auto levels=array(detail,3);levels["minItems"]=3;
  const Json occurrence={{"type",{"string","null"}},{"pattern","^[A-Za-z][A-Za-z0-9_-]{0,63}(/[A-Za-z][A-Za-z0-9_-]{0,63}){0,7}$"}};
  const Json face={{"type","string"},{"pattern","^face-[1-9][0-9]*$"}},edge={{"type","string"},{"pattern","^edge-[1-9][0-9]*$"}};
  definitions["component_manifest"]=object({{"schema_version",{{"const",1}}},{"coordinate_space",{{"const","source_local_mm"}}},
    {"geometries",array(object({{"id",id},{"bounds_mm",bounds},{"topology_sha256",hash},{"levels",levels}},{"id","bounds_mm","topology_sha256","levels"}),256)},
    {"occurrences",array(object({{"id",occurrence},{"geometry_id",id},{"transform",transform},{"faces",array(face,10000)},{"edges",array(edge,10000)}},{"id","geometry_id","transform","faces","edges"}),1024)}},
    {"schema_version","coordinate_space","geometries","occurrences"});
  definitions["component_mesh_result"]=object({{"geometry_id",id},{"level",level},{"topology_sha256",hash},{"mesh",{{"$ref","#/$defs/component_mesh_data"}}}},{"geometry_id","level","topology_sha256","mesh"});
}
}
