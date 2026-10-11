#include "agentcad/model.hpp"
#include "agentcad/authoring.hpp"
#include "agentcad/hash.hpp"
#include <set>
#include <map>
#include <cmath>
#include <functional>
#include <numbers>
#include <stdexcept>
#include <vector>
#include <limits>

namespace agentcad {
bool is_sketch_feature_type(const std::string& type) {
  static const std::set<std::string> types={"sketch","sketch_cut","sketch_fuse","sketch_intersection",
    "sketch_offset","sketch_fillet","sketch_chamfer","sketch_hull","sketch_trace","sketch_full_round","sketch_transform","sketch_instance","sketch_mirror",
    "sketch_face","sketch_projection","text_on_path"};
  return types.contains(type);
}
bool is_surface_feature_type(const std::string& type) {
  static const std::set<std::string> types={"surface_bezier","surface_bspline","surface_trim","surface_shell","import_step_surface","surface_fill","surface_gordon","surface_project"};
  return types.contains(type);
}
bool is_curve_feature_type(const std::string& type) { static const std::set<std::string> types={"curve","curve_project","curve_helix","curve_trim","curve_tangent_line","curve_tangent_arc","curve_extract","curve_constrained_line","curve_constrained_arc"};return types.contains(type); }
void validate_purchase(const Json& purchase,bool require_artifact) {
  fields(purchase,{"supplier","part_number","source_url"},{"artifact_sha256"});
  for(const auto* key:{"supplier","part_number","source_url"}) {
    const auto value=text_field(purchase,key);const auto maximum=std::string(key)=="source_url"?512:std::string(key)=="supplier"?120:64;
    if(value.empty()||value.size()>static_cast<std::size_t>(maximum))throw Error("invalid_model","Invalid purchasing text length",{{"field",key}});
    for(const unsigned char c:value)if(c<32||c>126)throw Error("invalid_model","Purchasing metadata must be printable ASCII",{{"field",key}});
    if(std::string(key)=="source_url"&&((!value.starts_with("https://")&&!value.starts_with("http://"))||value.find(' ')!=std::string::npos||value.size()<=value.find("://")+3))
      throw Error("invalid_model","Purchasing source_url must be a nonempty HTTP(S) URL",{{"field",key}});
  }
  if(require_artifact&&!purchase.contains("artifact_sha256"))throw Error("invalid_model","Imported purchasing identity requires the exact artifact SHA-256");
  if(purchase.contains("artifact_sha256")) {
    const auto hash=text_field(purchase,"artifact_sha256");
    if(hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos)throw Error("invalid_model","Invalid purchased artifact SHA-256");
  }
}
const Json* imported_step_source(const Json& model,const std::string& feature_id) {
  auto current=feature_id;
  for(std::size_t depth=0;depth<model.at("features").size();++depth) {
    const Json* found=nullptr;
    for(const auto& feature:model.at("features"))if(feature.at("id")==current){found=&feature;break;}
    if(!found)return nullptr;
    const auto type=text_field(*found,"type");
    if(type=="import_step")return found;
    if(type!="transform"&&type!="instance")return nullptr;
    current=text_field(*found,"input");
  }
  throw Error("invalid_model","Cyclic imported part source chain",{{"feature_id",feature_id}});
}
Json occurrence_schema() {
  return {{"type","string"},{"maxLength",519},
    {"pattern","^[A-Za-z][A-Za-z0-9_-]{0,63}(/[A-Za-z][A-Za-z0-9_-]{0,63}){0,7}$"}};
}
void validate_occurrence_path(const std::string& path) {
  if (path.empty() || path.size()>519) throw Error("invalid_argument","Invalid assembly occurrence path");
  std::size_t start=0,depth=0;
  while (true) {
    const auto end=path.find('/',start);
    model_identifier(path.substr(start,end==std::string::npos?end:end-start));
    if (++depth>assembly_depth_limit) throw Error("invalid_argument","Assembly occurrence exceeds eight levels");
    if (end==std::string::npos) break;
    start=end+1;
  }
}
void validate_step_solid_indices(const Json& indices) {
  if(!indices.is_array()||indices.empty()||indices.size()>4096)throw Error("invalid_argument","STEP solid_indices requires 1–4096 distinct source indices");
  std::set<int> seen;
  for(const auto& index:indices)if(!index.is_number_integer()||index<1||index>4096||!seen.insert(index.get<int>()).second)
    throw Error("invalid_argument","STEP solid_indices requires unique integers between 1 and 4096");
}
Json model_definitions() {
  const Json id = {{"$ref", "#/$defs/model_id"}};
  const Json numeric = {{"type", "number"}, {"minimum", -1e6}, {"maximum", 1e6}};
  auto object = [](Json properties, Json required) {
    return Json{{"type", "object"}, {"properties", properties}, {"required", required}, {"additionalProperties", false}};
  };
  const Json scalar_ref = {{"$ref", "#/$defs/scalar"}};
  const Json vector_ref = {{"$ref", "#/$defs/vector3"}};
  const Json feature_ref = {{"$ref", "#/$defs/feature"}};
  const auto curve_schema = [&](int dimensions) {
    const Json point={{"type","array"},{"items",scalar_ref},{"minItems",dimensions},{"maxItems",dimensions}};
    const auto points=[&](int minimum,int maximum) {return Json{{"type","array"},{"items",point},{"minItems",minimum},{"maxItems",maximum}};};
    return Json{{"oneOf",Json::array({
      object({{"type",{{"const","line"}}},{"start",point},{"end",point}},{"type","start","end"}),
      object({{"type",{{"const","arc"}}},{"start",point},{"mid",point},{"end",point}},{"type","start","mid","end"}),
      object({{"type",{{"const","bezier"}}},{"points",points(2,26)},{"weights",{{"type","array"},{"items",scalar_ref},{"minItems",2},{"maxItems",26}}}},{"type","points"}),
      object({{"type",{{"const","tangent_arc"}}},{"start",point},{"end",point},{"tangent",point}},{"type","start","end","tangent"}),
      object({{"type",{{"const","spline"}}},{"points",points(2,64)},{"periodic",{{"type","boolean"}}},
        {"start_tangent",point},{"end_tangent",point}},{"type","points"})
    })}};
  };
  const Json segments2={{"type","array"},{"items",curve_schema(2)},{"minItems",1},{"maxItems",64}};
  const Json segments3={{"type","array"},{"items",curve_schema(3)},{"minItems",1},{"maxItems",64}};
  const Json selector = object({
    {"type", {{"const", "geometric"}}}, {"feature_id", id},
    {"curve_kind", {{"enum", {"line", "circle", "ellipse", "hyperbola", "parabola", "bezier", "bspline", "offset", "other"}}}},
    {"expected_count", {{"type", "integer"}, {"minimum", 1}, {"maximum", 10000}}},
    {"direction", object({{"vector", vector_ref}, {"tolerance", scalar_ref}}, {"vector", "tolerance"})},
    {"center", object({{"point", vector_ref}, {"tolerance", scalar_ref}}, {"point", "tolerance"})},
    {"length", object({{"value", scalar_ref}, {"tolerance", scalar_ref}}, {"value", "tolerance"})},
    {"radius",object({{"value",scalar_ref},{"tolerance",scalar_ref}},{"value","tolerance"})},
    {"axis",object({{"vector",vector_ref},{"tolerance",scalar_ref}},{"vector","tolerance"})},
    {"closed",{{"type","boolean"}}},
    {"endpoints",object({{"points",{{"type","array"},{"items",vector_ref},{"minItems",2},{"maxItems",2}}},{"tolerance",scalar_ref}},{"points","tolerance"})}
  }, {"type", "feature_id", "curve_kind", "expected_count"});
  const Json face_selector = object({
    {"type",{{"const","geometric"}}},{"feature_id",id},
    {"surface_kind",{{"enum",{"plane","cylinder","cone","sphere","torus","bezier","bspline","revolution","extrusion","offset","other"}}}},
    {"expected_count",{{"type","integer"},{"minimum",1},{"maximum",10000}}},
    {"normal",object({{"vector",vector_ref},{"tolerance",scalar_ref}},{"vector","tolerance"})},
    {"center",object({{"point",vector_ref},{"tolerance",scalar_ref}},{"point","tolerance"})},
    {"area",object({{"value",scalar_ref},{"tolerance",scalar_ref}},{"value","tolerance"})}
  },{"type","feature_id","surface_kind","expected_count"});
  const Json face_selection={{"oneOf",Json::array({Json{{"$ref","#/$defs/face_selector"}},
    Json{{"type","array"},{"items",{{"$ref","#/$defs/face_selector"}}},{"minItems",1},{"maxItems",64}}})}};
  auto shell_faces=face_selection;shell_faces["oneOf"][1]["minItems"]=0;
  const Json offset_join={{"enum",{"arc","intersection"}}};
  Json features = Json::array({
    object({{"id", id}, {"type", {{"const", "box"}}}, {"size", vector_ref}, {"origin", vector_ref}}, {"id", "type", "size"}),
    object({{"id", id}, {"type", {{"const", "cylinder"}}}, {"radius", scalar_ref}, {"height", scalar_ref}, {"origin", vector_ref}}, {"id", "type", "radius", "height"}),
    object({{"id",id},{"type",{{"const","external_thread"}}},{"major_diameter",scalar_ref},{"pitch",scalar_ref},{"length",scalar_ref},{"origin",vector_ref},{"handedness",{{"enum",{"right","left"}}}}}, {"id","type","major_diameter","pitch","length"}),
    object({{"id", id}, {"type", {{"enum", {"cut", "fuse", "intersection"}}}}, {"left", id}, {"right", id}}, {"id", "type", "left", "right"}),
    object({{"id", id}, {"type", {{"const", "fillet"}}}, {"input", id}, {"radius", scalar_ref}, {"edges", {{"oneOf", Json::array({Json{{"const", "all"}}, Json{{"$ref", "#/$defs/selector"}}})}}}}, {"id", "type", "input", "radius", "edges"}),
    object({{"id", id}, {"type", {{"const", "chamfer"}}}, {"input", id}, {"distance", scalar_ref}, {"edges", {{"oneOf", Json::array({Json{{"const", "all"}}, Json{{"$ref", "#/$defs/selector"}}})}}}}, {"id", "type", "input", "distance", "edges"})
  });
  // Non-symmetric chamfers bind their side to a geometric face rule.
  auto& chamfer=features.back();
  chamfer["properties"]["distance2"]=scalar_ref;
  chamfer["properties"]["angle_deg"]=scalar_ref;
  chamfer["properties"]["reference_face"]={{"$ref","#/$defs/face_selector"}};
  Json symmetric=Json::object();
  symmetric["not"]["anyOf"]=Json::array({Json{{"required",{"distance2"}}},Json{{"required",{"angle_deg"}}},Json{{"required",{"reference_face"}}}});
  chamfer["oneOf"]=Json::array({symmetric,
    Json{{"required",{"distance2","reference_face"}},{"not",{{"required",{"angle_deg"}}}}},
    Json{{"required",{"angle_deg","reference_face"}},{"not",{{"required",{"distance2"}}}}}
  });
  features.push_back(object({{"id",id},{"type",{{"const","scale"}}},{"input",id},{"origin",vector_ref},
    {"factors",{{"oneOf",Json::array({scalar_ref,vector_ref})}}}}, {"id","type","input","origin","factors"}));
  features.push_back(object({{"id",id},{"type",{{"const","draft"}}},{"input",id},{"faces",face_selection},
    {"angle_deg",scalar_ref},{"direction",vector_ref},{"neutral_plane",{{"$ref","#/$defs/workplane"}}}},
    {"id","type","input","faces","angle_deg","direction","neutral_plane"}));
  features.push_back(object({{"id",id},{"type",{{"const","twist_extrude"}}},{"input",id},
    {"distance",scalar_ref},{"angle_deg",scalar_ref},{"center",vector_ref}}, {"id","type","input","distance","angle_deg"}));
  const Json pole_row={{"type","array"},{"items",vector_ref},{"minItems",2},{"maxItems",26}};
  const Json poles={{"type","array"},{"items",pole_row},{"minItems",2},{"maxItems",26}};
  const Json weight_row={{"type","array"},{"items",scalar_ref},{"minItems",2},{"maxItems",26}};
  const Json weights={{"type","array"},{"items",weight_row},{"minItems",2},{"maxItems",26}};
  const Json knots={{"type","array"},{"items",scalar_ref},{"minItems",2},{"maxItems",128}};
  const Json multiplicities={{"type","array"},{"items",{{"type","integer"},{"minimum",1},{"maximum",26}}},{"minItems",2},{"maxItems",128}};
  const Json degree={{"type","integer"},{"minimum",1},{"maximum",25}};
  const Json uv_range={{"type","array"},{"items",scalar_ref},{"minItems",2},{"maxItems",2}};
  features.push_back(object({{"id",id},{"type",{{"const","surface_bezier"}}},{"control_points",poles},{"weights",weights}}, {"id","type","control_points"}));
  features.push_back(object({{"id",id},{"type",{{"const","surface_bspline"}}},{"control_points",poles},{"weights",weights},
    {"degree_u",degree},{"degree_v",degree},{"knots_u",knots},{"knots_v",knots},{"multiplicities_u",multiplicities},{"multiplicities_v",multiplicities}},
    {"id","type","control_points","degree_u","degree_v","knots_u","knots_v","multiplicities_u","multiplicities_v"}));
  features.push_back(object({{"id",id},{"type",{{"const","surface_trim"}}},{"input",id},{"u_range",uv_range},{"v_range",uv_range},
    {"boundary",segments2},{"holes",{{"type","array"},{"items",segments2},{"maxItems",16}}}}, {"id","type","input"}));
  features.back()["oneOf"]=parse_json(R"JSON([{"required":["u_range","v_range"],"not":{"anyOf":[{"required":["boundary"]},{"required":["holes"]}]}},{"required":["boundary"],"not":{"anyOf":[{"required":["u_range"]},{"required":["v_range"]}]}}])JSON");
  const auto tolerance=Json{{"type","number"},{"minimum",1e-7},{"maximum",1e-3}};
  auto boundary=object({{"curve",curve_schema(3)},{"input",id},{"edge",{{"$ref","#/$defs/selector"}}},{"face",{{"$ref","#/$defs/face_selector"}}},{"continuity",{{"enum",{"C0","G1","G2"}}}},{"reverse",{{"type","boolean"}}}}, {"continuity"});
  boundary["oneOf"]=parse_json(R"JSON([{"required":["curve"],"properties":{"continuity":{"const":"C0"}},"not":{"anyOf":[{"required":["input"]},{"required":["edge"]},{"required":["face"]}]}},{"required":["input","edge"],"not":{"required":["curve"]}}])JSON");
  boundary["allOf"]=parse_json(R"JSON([{"if":{"properties":{"continuity":{"enum":["G1","G2"]}}},"then":{"required":["face"]}}])JSON");
  features.push_back(object({{"id",id},{"type",{{"const","surface_fill"}}},{"boundaries",{{"type","array"},{"items",boundary},{"minItems",2},{"maxItems",32}}},
    {"points",{{"type","array"},{"items",vector_ref},{"maxItems",64}}},{"tolerance",tolerance},{"angular_tolerance",tolerance},{"curvature_tolerance",tolerance}}, {"id","type","boundaries","tolerance"}));
  const auto family=Json{{"type","array"},{"items",curve_schema(3)},{"minItems",2},{"maxItems",16}};
  features.push_back(object({{"id",id},{"type",{{"const","surface_gordon"}}},{"u_curves",family},{"v_curves",family},
    {"u_parameters",{{"type","array"},{"items",scalar_ref},{"minItems",2},{"maxItems",16}}},{"v_parameters",{{"type","array"},{"items",scalar_ref},{"minItems",2},{"maxItems",16}}},{"tolerance",tolerance}}, {"id","type","u_curves","v_curves","u_parameters","v_parameters","tolerance"}));
  features.push_back(object({{"id",id},{"type",{{"const","curve"}}},{"path",object({{"type",{{"const","wire"}}},{"segments",segments3}}, {"type","segments"})}}, {"id","type","path"}));
  features.push_back(object({{"id",id},{"type",{{"const","curve_helix"}}},{"frame",{{"$ref","#/$defs/workplane"}}},{"radius",scalar_ref},{"pitch",scalar_ref},{"turns",scalar_ref},{"handedness",{{"enum",{"right","left"}}}}}, {"id","type","frame","radius","pitch","turns"}));
  features.push_back(object({{"id",id},{"type",{{"const","curve_trim"}}},{"input",id},{"start",scalar_ref},{"end",scalar_ref}}, {"id","type","input","start","end"}));
  features.push_back(object({{"id",id},{"type",{{"const","curve_tangent_line"}}},{"input",id},{"position",scalar_ref},{"length",scalar_ref},{"reverse",{{"type","boolean"}}}}, {"id","type","input","position","length"}));
  features.push_back(object({{"id",id},{"type",{{"const","curve_tangent_arc"}}},{"input",id},{"position",scalar_ref},{"end",vector_ref},{"reverse",{{"type","boolean"}}}}, {"id","type","input","position","end"}));
  const Json tangency_constraint={{"oneOf",Json::array({
    object({{"point",vector_ref}},{"point"}),
    object({{"edge",{{"$ref","#/$defs/selector"}}},{"qualifier",{{"enum",{"unqualified","enclosed","enclosing","outside"}}}}},{"edge"})})}};
  const Json tangency_solution=object({{"point",vector_ref},{"tolerance",scalar_ref}},{"point","tolerance"});
  for(const auto* kind:{"curve_constrained_line","curve_constrained_arc"}) {
    auto properties=Json{{"id",id},{"type",{{"const",kind}}},{"workplane",{{"$ref","#/$defs/workplane"}}},
      {"constraints",{{"type","array"},{"items",tangency_constraint},{"minItems",2},{"maxItems",2}}},{"solution",tangency_solution}};
    Json required={"id","type","workplane","constraints","solution"};
    if(std::string(kind)=="curve_constrained_arc"){properties["radius"]=scalar_ref;required.push_back("radius");}
    features.push_back(object(properties,required));
  }
  features.push_back(object({{"id",id},{"type",{{"const","curve_extract"}}},{"input",id},{"edges",{{"$ref","#/$defs/selector"}}}}, {"id","type","input","edges"}));
  features.push_back(object({{"id",id},{"type",{{"enum",{"surface_project","curve_project"}}}},{"input",id},{"target",id},{"faces",{{"$ref","#/$defs/face_selector"}}},{"direction",vector_ref}}, {"id","type","input","target","faces","direction"}));
  features.push_back(object({{"id",id},{"type",{{"const","surface_shell"}}},{"inputs",{{"type","array"},{"items",id},{"minItems",1},{"maxItems",64},{"uniqueItems",true}}},
    {"tolerance",{{"type","number"},{"minimum",1e-9},{"maximum",1e-3}}},{"closed",{{"type","boolean"}}}}, {"id","type","inputs","tolerance","closed"}));
  features.push_back(object({{"id",id},{"type",{{"const","surface_solid"}}},{"input",id},{"reverse",{{"type","boolean"}}}}, {"id","type","input"}));
  const Json workplane_schema = object({{"origin",vector_ref},{"normal",vector_ref},{"x_direction",vector_ref}}, {"origin","normal","x_direction"});
  const Json sheet_cut=object({{"offset",scalar_ref},{"width",scalar_ref},{"from",scalar_ref},{"to",scalar_ref}}, {"offset","width","from","to"});
  Json sheet_flange=object({{"id",id},{"edge",{{"$ref","#/$defs/selector"}}},{"parent",id},
    {"attachment",{{"enum",{"tip","start","end"}}}},{"fold_line",{{"type","array"},{"items",vector_ref},{"minItems",2},{"maxItems",2}}},
    {"inside_radius",scalar_ref},{"angle_deg",scalar_ref},{"length",scalar_ref},{"start_gap",scalar_ref},{"end_gap",scalar_ref},
    {"hem",{{"type","boolean"}}},{"relief",object({{"width",scalar_ref},{"depth",scalar_ref}},{"width","depth"})},
    {"miter",object({{"start_deg",scalar_ref},{"end_deg",scalar_ref}},{"start_deg","end_deg"})},
    {"cuts",{{"type","array"},{"items",sheet_cut},{"maxItems",32}}}},
    {"id","inside_radius","angle_deg","length"});
  sheet_flange["oneOf"]=Json::array({Json{{"required",{"edge"}}},Json{{"required",{"parent"}}},Json{{"required",{"fold_line"}}}});
  sheet_flange["dependentRequired"]={{"attachment",{"parent"}}};
  features.push_back(object({{"id",id},{"type",{{"const","sheet_metal"}}},{"input",id},{"thickness",scalar_ref},
    {"k_factor",scalar_ref},{"flanges",{{"type","array"},{"items",sheet_flange},{"maxItems",32}}}},
    {"id","type","input","thickness","k_factor","flanges"}));
  features.push_back(object({{"id",id},{"type",{{"const","sheet_unfold"}}},{"input",id}}, {"id","type","input"}));
  features.push_back(object({{"id",id},{"type",{{"const","shell"}}},{"input",id},
    {"thickness",scalar_ref},{"faces",shell_faces},{"join",offset_join}},{"id","type","input","thickness","faces"}));
  features.push_back(object({{"id",id},{"type",{{"const","offset"}}},{"input",id},
    {"distance",scalar_ref},{"join",offset_join}},{"id","type","input","distance"}));
  features.push_back(object({{"id",id},{"type",{{"const","thicken"}}},{"input",id},
    {"thickness",scalar_ref},{"faces",face_selection},{"join",offset_join}},{"id","type","input","thickness"}));
  Json profile_schema = {{"oneOf", Json::array({
    object({{"type",{{"const","rectangle"}}},{"width",scalar_ref},{"height",scalar_ref}}, {"type","width","height"}),
    object({{"type",{{"const","circle"}}},{"radius",scalar_ref}}, {"type","radius"}),
    object({{"type",{{"const","polygon"}}},{"points",{{"type","array"},{"minItems",3},{"maxItems",128},{"items",{{"type","array"},{"items",scalar_ref},{"minItems",2},{"maxItems",2}}}}}}, {"type","points"}),
    object({{"type",{{"const","wire"}}},{"segments",segments2},
      {"holes",{{"type","array"},{"items",segments2},{"maxItems",16}}}},{"type","segments"})
  })}};
  for(const auto& variant:authoring_profile_schemas(scalar_ref))profile_schema["oneOf"].push_back(variant);
  const Json axis_schema = object({{"origin",vector_ref},{"direction",vector_ref}}, {"origin","direction"});
  const Json rotation_schema = object({{"origin",vector_ref},{"axis",vector_ref},{"angle_deg",scalar_ref}}, {"origin","axis","angle_deg"});
  const Json placement_schema = object({{"translation",vector_ref},{"rotation",rotation_schema}}, Json::array());
  const auto bom_text=[](int maximum) {return Json{{"type","string"},{"maxLength",maximum},{"not",{{"pattern","[^ -~]"}}}};};
  const auto purchase_text=[&](int limit){auto result=bom_text(limit);result["minLength"]=1;return result;};
  const Json purchase=object({{"supplier",purchase_text(120)},{"part_number",purchase_text(64)},
    {"source_url",{{"type","string"},{"maxLength",512},{"pattern","^https?://[^ \\t\\r\\n]+$"}}},
    {"artifact_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}}}, {"supplier","part_number","source_url"});
  const Json bom_item_schema=object({{"input",id},{"item_number",{{"type","integer"},{"minimum",1},{"maximum",999}}},
    {"part_number",bom_text(64)},{"description",bom_text(120)},{"material",bom_text(64)},{"purchase",{{"$ref","#/$defs/purchase"}}}},{"input"});
  Json mate_variants=Json::array();
  for (const auto* type:{"rigid","revolute","slider","cylindrical"}) {
    Json properties={{"id",id},{"type",{{"const",type}}},{"parent",id},{"child",id},
      {"parent_frame",{{"$ref","#/$defs/workplane"}}},{"child_frame",{{"$ref","#/$defs/workplane"}}},{"offset",vector_ref},{"angle_deg",scalar_ref}};
    Json required={"id","type","parent","child","parent_frame","child_frame"};
    const Json limits={{"type","array"},{"items",scalar_ref},{"minItems",2},{"maxItems",2}};
    if (std::string(type)=="revolute" || std::string(type)=="cylindrical") {properties["angle_limits_deg"]=limits;required.push_back("angle_limits_deg");}
    if (std::string(type)=="slider" || std::string(type)=="cylindrical") {properties["travel_mm"]=scalar_ref;properties["travel_limits_mm"]=limits;required.push_back("travel_limits_mm");}
    mate_variants.push_back(object(properties,required));
  }
  const Json mate_schema={{"oneOf",mate_variants}};
  const Json coordinate={{"enum",{"angle_deg","travel_mm"}}};
  const Json motion_ref=object({{"mate_id",id},{"coordinate",coordinate}},{"mate_id","coordinate"});
  const Json coupling_schema=object({{"id",id},{"source",motion_ref},{"target",motion_ref},{"ratio",scalar_ref},{"offset",scalar_ref}},
    {"id","source","target","ratio"});
  const Json pose_value=object({{"mate_id",id},{"coordinate",coordinate},{"value",scalar_ref}},{"mate_id","coordinate","value"});
  const Json pose_schema=object({{"id",id},{"values",{{"type","array"},{"items",pose_value},{"maxItems",126}}}},{"id","values"});
  const Json part_schema = object({{"id",id},{"input",id},{"placement",{{"$ref","#/$defs/placement"}}}}, {"id","input"});
  features.push_back(object({{"id",id},{"type",{{"const","sketch"}}},{"workplane",{{"$ref","#/$defs/workplane"}}},{"profile",profile_schema}}, {"id","type","workplane","profile"}));
  auto extrude_schema=object({{"id",id},{"type",{{"const","extrude"}}},{"input",id},{"distance",scalar_ref},
    {"direction",vector_ref},{"both",{{"type","boolean"}}},{"taper_deg",scalar_ref},
    {"until",{{"enum",{"first","last"}}}},{"target",id}}, {"id","type","input"});
  Json distance_mode={{"required",{"distance"}}},target_mode={{"required",{"until","target"}}};
  distance_mode["not"]["anyOf"]=Json::array({Json{{"required",{"until"}}},Json{{"required",{"target"}}}});
  target_mode["not"]["anyOf"]=Json::array({Json{{"required",{"distance"}}},Json{{"required",{"both"}}},Json{{"required",{"taper_deg"}}}});
  extrude_schema["oneOf"]=Json::array({distance_mode,target_mode});
  features.push_back(extrude_schema);
  const Json vertex_selector=object({{"type",{{"const","geometric"}}},{"feature_id",id},
    {"point",vector_ref},{"tolerance",scalar_ref},{"expected_count",{{"type","integer"},{"minimum",1},{"maximum",10000}}}},
    {"type","feature_id","point","tolerance","expected_count"});
  const Json vertices={{"oneOf",Json::array({Json{{"const","all"}},vertex_selector})}};
  features.push_back(object({{"id",id},{"type",{{"enum",{"sketch_cut","sketch_fuse","sketch_intersection"}}}},{"left",id},{"right",id}}, {"id","type","left","right"}));
  features.push_back(object({{"id",id},{"type",{{"const","sketch_hull"}}},{"inputs",{{"type","array"},{"items",id},{"minItems",1},{"maxItems",16},{"uniqueItems",true}}},{"workplane",workplane_schema},{"contact_tolerance",scalar_ref}}, {"id","type","inputs","workplane"}));
  features.push_back(object({{"id",id},{"type",{{"const","sketch_trace"}}},{"input",id},{"workplane",workplane_schema},{"width",scalar_ref}}, {"id","type","input","workplane","width"}));
  features.push_back(object({{"id",id},{"type",{{"const","sketch_full_round"}}},{"input",id},{"edges",{{"$ref","#/$defs/selector"}}},{"invert",{{"type","boolean"}}}}, {"id","type","input","edges"}));
  features.push_back(object({{"id",id},{"type",{{"const","sketch_offset"}}},{"input",id},{"distance",scalar_ref},{"join",{{"enum",{"arc","intersection"}}}}}, {"id","type","input","distance"}));
  features.push_back(object({{"id",id},{"type",{{"const","sketch_fillet"}}},{"input",id},{"radius",scalar_ref},{"vertices",vertices}}, {"id","type","input","radius","vertices"}));
  features.push_back(object({{"id",id},{"type",{{"const","sketch_chamfer"}}},{"input",id},{"distance",scalar_ref},{"vertices",vertices}}, {"id","type","input","distance","vertices"}));
  features.push_back(object({{"id",id},{"type",{{"enum",{"sketch_transform","sketch_instance"}}}},{"input",id},{"translation",vector_ref},{"rotation",rotation_schema}}, {"id","type","input"}));
  features.push_back(object({{"id",id},{"type",{{"enum",{"mirror","sketch_mirror"}}}},{"input",id},{"plane",workplane_schema}}, {"id","type","input","plane"}));
  features.push_back(object({{"id",id},{"type",{{"const","split"}}},{"input",id},{"plane",workplane_schema},{"keep",{{"enum",{"both","top","bottom"}}}}}, {"id","type","input","plane","keep"}));
  features.push_back(object({{"id",id},{"type",{{"const","text_on_path"}}},{"input",id},{"path",id},{"start",scalar_ref},{"offset",scalar_ref},{"reverse",{{"type","boolean"}}}}, {"id","type","input","path"}));
  features.push_back(object({{"id",id},{"type",{{"const","sketch_face"}}},{"input",id},{"faces",{{"$ref","#/$defs/face_selection"}}}}, {"id","type","input","faces"}));
  features.push_back(object({{"id",id},{"type",{{"const","sketch_projection"}}},{"input",id},{"faces",{{"$ref","#/$defs/face_selection"}}},{"workplane",workplane_schema}}, {"id","type","input","faces","workplane"}));
  features.push_back(object({{"id",id},{"type",{{"const","revolve"}}},{"input",id},{"axis",axis_schema},{"angle_deg",scalar_ref}}, {"id","type","input","axis","angle_deg"}));
  auto loft=object({{"id",id},{"type",{{"const","loft"}}},{"sections",{{"type","array"},{"items",id},{"minItems",1},{"maxItems",32}}},
    {"start_vertex",vector_ref},{"end_vertex",vector_ref},{"ruled",{{"type","boolean"}}}}, {"id","type","sections"});
  const Json hole_landmark=object({{"point",vector_ref},{"tolerance",scalar_ref}},{"point","tolerance"});
  loft["properties"]["hole_order"]={{"type","array"},{"minItems",1},{"maxItems",32},{"items",{{"type","array"},{"minItems",1},{"maxItems",16},{"items",hole_landmark}}}};
  loft["anyOf"]=Json::array({Json{{"properties",{{"sections",{{"minItems",2}}}}}},Json{{"required",{"start_vertex"}}},Json{{"required",{"end_vertex"}}}});
  features.push_back(loft);
  const auto sweep_wire=object({{"type",{{"const","wire"}}},{"segments",segments3}},{"type","segments"});
  auto sweep_schema=object({{"id",id},{"type",{{"const","sweep"}}},{"input",id},
    {"sections",{{"type","array"},{"items",id},{"minItems",2},{"maxItems",32}}},{"path",{{"oneOf",Json::array({
    Json{{"type","array"},{"items",vector_ref},{"minItems",2},{"maxItems",64}},
    sweep_wire
  })}}},{"orientation",{{"enum",{"corrected_frenet","frenet","fixed"}}}},
    {"binormal",vector_ref},{"guide",sweep_wire},{"transition",{{"enum",{"transformed","right_corner","round_corner"}}}}}, {"id","type","path"});
  sweep_schema["oneOf"]=Json::array({Json{{"required",{"input"}},{"not",{{"required",{"sections"}}}}},Json{{"required",{"sections"}},{"not",{{"required",{"input"}}}}}});
  sweep_schema["not"]["anyOf"]=Json::array({Json{{"required",{"orientation","binormal"}}},Json{{"required",{"orientation","guide"}}},Json{{"required",{"binormal","guide"}}}});
  features.push_back(sweep_schema);
  features.push_back(object({{"id",id},{"type",{{"enum",{"transform","instance"}}}},{"input",id},{"translation",vector_ref},{"rotation",rotation_schema}}, {"id","type","input"}));
  features.push_back(object({{"id",id},{"type",{{"const","pattern"}}},{"input",id},{"count",{{"$ref","#/$defs/pattern_count"}}},{"step",vector_ref}}, {"id","type","input","count","step"}));
  features.push_back(object({{"id",id},{"type",{{"const","circular_pattern"}}},{"input",id},
    {"count",{{"$ref","#/$defs/pattern_count"}}},{"axis",axis_schema},{"angle_deg",scalar_ref}},
    {"id","type","input","count","axis","angle_deg"}));
  features.push_back(object({{"id",id},{"type",{{"const","hole"}}},{"input",id},{"origin",vector_ref},{"axis",vector_ref},{"radius",scalar_ref},{"depth",scalar_ref}}, {"id","type","input","origin","axis","radius","depth"}));
  features.push_back(object({{"id",id},{"type",{{"const","import_step"}}},{"content",{{"type","string"},{"minLength",1}}},{"sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},
    {"purchase",{{"allOf",Json::array({Json{{"$ref","#/$defs/purchase"}},Json{{"required",{"artifact_sha256"}}}})}}},
    {"solid_indices",{{"type","array"},{"items",{{"type","integer"},{"minimum",1},{"maximum",4096}}},{"minItems",1},{"maxItems",4096},{"uniqueItems",true}}}}, {"id","type","content","sha256"}));
  features.back()["not"]={{"required",{"solid_indices","purchase"}}};
  features.push_back(object({{"id",id},{"type",{{"const","import_step_surface"}}},{"content",{{"type","string"},{"minLength",1}}},{"sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}}}, {"id","type","content","sha256"}));
  features.push_back(object({{"id",id},{"type",{{"const","assembly"}}},
    {"parts",{{"type","array"},{"items",{{"$ref","#/$defs/assembly_part"}}},{"minItems",1},{"maxItems",64}}},
    {"mates",{{"type","array"},{"items",{{"$ref","#/$defs/mate"}}},{"maxItems",63}}},
    {"couplings",{{"type","array"},{"items",{{"$ref","#/$defs/coupling"}}},{"maxItems",126}}},
    {"poses",{{"type","array"},{"items",{{"$ref","#/$defs/pose"}}},{"maxItems",64}}},
    {"bom",{{"type","array"},{"items",{{"$ref","#/$defs/bom_item"}}},{"maxItems",64}}}}, {"id","type","parts"}));
  const Json expression = scalar_expression_schema(scalar_ref);
  const Json operations = Json::array({
    object({{"op", {{"const", "set_parameter"}}}, {"name", id}, {"value", numeric}}, {"op", "name", "value"}),
    object({{"op", {{"const", "add_feature"}}}, {"feature", feature_ref}}, {"op", "feature"}),
    object({{"op", {{"const", "replace_feature"}}}, {"id", id}, {"feature", feature_ref}}, {"op", "id", "feature"}),
    object({{"op", {{"const", "remove_feature"}}}, {"id", id}}, {"op", "id"}),
    object({{"op", {{"const", "set_output"}}}, {"feature_id", id}}, {"op", "feature_id"}),
    object({{"op",{{"const","set_part_placement"}}},{"assembly_id",id},{"part_id",id},{"placement",{{"$ref","#/$defs/placement"}}}}, {"op","assembly_id","part_id","placement"}),
    object({{"op",{{"const","set_mate"}}},{"assembly_id",id},{"mate",{{"$ref","#/$defs/mate"}}}}, {"op","assembly_id","mate"}),
    object({{"op",{{"const","remove_mate"}}},{"assembly_id",id},{"mate_id",id}}, {"op","assembly_id","mate_id"}),
    object({{"op",{{"const","set_joint_value"}}},{"assembly_id",id},{"mate_id",id},{"coordinate",coordinate},{"value",scalar_ref}}, {"op","assembly_id","mate_id","coordinate","value"}),
    object({{"op",{{"const","set_coupling"}}},{"assembly_id",id},{"coupling",{{"$ref","#/$defs/coupling"}}}}, {"op","assembly_id","coupling"}),
    object({{"op",{{"const","remove_coupling"}}},{"assembly_id",id},{"coupling_id",id}}, {"op","assembly_id","coupling_id"}),
    object({{"op",{{"const","set_pose"}}},{"assembly_id",id},{"pose",{{"$ref","#/$defs/pose"}}}}, {"op","assembly_id","pose"}),
    object({{"op",{{"const","remove_pose"}}},{"assembly_id",id},{"pose_id",id}}, {"op","assembly_id","pose_id"}),
    object({{"op",{{"const","apply_pose"}}},{"assembly_id",id},{"pose_id",id}}, {"op","assembly_id","pose_id"}),
    object({{"op",{{"const","set_bom_item"}}},{"assembly_id",id},{"item",{{"$ref","#/$defs/bom_item"}}}}, {"op","assembly_id","item"}),
    object({{"op",{{"const","remove_bom_item"}}},{"assembly_id",id},{"input",id}}, {"op","assembly_id","input"})
  });
  Json definitions = {
    {"model_id", {{"type", "string"}, {"pattern", "^[A-Za-z][A-Za-z0-9_-]{0,63}$"}}},
    {"selector", selector},
    {"face_selector",face_selector},
    {"face_selection",face_selection},
    {"workplane",workplane_schema},{"placement",placement_schema}, {"assembly_part",part_schema}, {"mate",mate_schema}, {"bom_item",bom_item_schema},{"purchase",purchase},
    {"coupling",coupling_schema},{"pose",pose_schema},
    {"scalar", {{"oneOf", Json::array({numeric, object({{"parameter", id}}, {"parameter"}), expression})}}},
    {"pattern_count", {{"oneOf",Json::array({Json{{"type","integer"},{"minimum",2},{"maximum",64}},object({{"parameter",id}},{"parameter"}),expression})}}},
    {"vector3", {{"type", "array"}, {"items", scalar_ref}, {"minItems", 3}, {"maxItems", 3}}},
    {"feature", {{"oneOf", features}}},
    {"operation", {{"oneOf", operations}}},
    {"model", object({{"schema_version", {{"type", "integer"}, {"const", 1}}}, {"units", {{"const", "mm"}}},
      {"parameters", {{"type", "object"}, {"propertyNames", id}, {"additionalProperties", numeric}, {"maxProperties", 128}}},
      {"features", {{"type", "array"}, {"items", feature_ref}, {"minItems", 1}, {"maxItems", 256}}},
      {"output", id}}, {"schema_version", "units", "parameters", "features", "output"})}
  };
  definitions.update(component_definitions());
  definitions["model"]["properties"]["components"]={{"type","array"},{"maxItems",64},{"items",{{"$ref","#/$defs/component"}}}};
  for(const auto* name:{"set_component_operation","detach_component_operation","remove_component_operation"})
    definitions["operation"]["oneOf"].push_back({{"$ref",std::string("#/$defs/")+name}});
  auto bom_output_item=bom_item_schema;
  const auto occurrence=occurrence_schema();
  definitions["assembly_node"]=object({{"id",occurrence},{"input",id},{"assembly_id",id},
    {"kind",{{"enum",{"part","assembly"}}}},{"parent_id",{{"anyOf",Json::array({occurrence,Json{{"const",""}}})}}}},
    {"id","input","assembly_id","kind","parent_id"});
  const Json structure={{"type","array"},{"items",{{"$ref","#/$defs/assembly_node"}}},{"maxItems",8192}};
  auto bom_node=definitions.at("assembly_node");
  bom_node["properties"]["bom"]={{"$ref","#/$defs/bom_item"}};
  const Json bom_structure={{"type","array"},{"items",bom_node},{"maxItems",8192}};
  bom_output_item["properties"]["quantity"]={{"type","integer"},{"minimum",1},{"maximum",assembly_leaf_limit}};
  bom_output_item["properties"]["part_ids"]={{"type","array"},{"items",occurrence},{"minItems",1},{"maxItems",assembly_leaf_limit},{"uniqueItems",true}};
  bom_output_item["required"]={"item_number","input","quantity","part_ids"};
  definitions["bom"]=object({{"assembly_id",id},
    {"items",{{"type","array"},{"items",bom_output_item},{"minItems",1},{"maxItems",256}}},{"structure",bom_structure},
    {"total_quantity",{{"type","integer"},{"minimum",1},{"maximum",assembly_leaf_limit}}}},{"assembly_id","items","total_quantity"});
  const Json real = {{"type","number"}};
  const Json nonnegative = {{"type","number"},{"minimum",0}};
  const Json point_output = {{"type","array"},{"items",real},{"minItems",3},{"maxItems",3}};
  const Json bounds_output = object({{"min",point_output},{"max",point_output}}, {"min","max"});
  const Json line_output={{"type","array"},{"items",point_output},{"minItems",2},{"maxItems",2}};
  definitions["sheet_metal_report"]=object({
    {"mode",{{"enum",{"formed","flat"}}}},{"source_feature_id",id},{"thickness_mm",nonnegative},{"k_factor",{{"type","number"},{"minimum",0},{"maximum",1}}},
    {"neutral_axis_basis",{{"const","caller_supplied_k_factor"}}},{"flat_area_mm2",nonnegative},{"flat_volume_mm3",nonnegative},{"formed_volume_mm3",nonnegative},
    {"volume_preservation_assumed",{{"const",false}}},
    {"bends",{{"type","array"},{"maxItems",32},{"items",object({{"id",id},{"width_mm",nonnegative},{"inside_radius_mm",nonnegative},{"angle_deg",real},
      {"straight_length_mm",nonnegative},{"bend_allowance_mm",nonnegative},{"bend_deduction_mm",{{"type",{"number","null"}}}},
      {"bend_start_line_mm",line_output},{"bend_center_line_mm",line_output},{"bend_end_line_mm",line_output}},
      {"id","width_mm","inside_radius_mm","angle_deg","straight_length_mm","bend_allowance_mm","bend_deduction_mm","bend_start_line_mm","bend_center_line_mm","bend_end_line_mm"})}}}
  },{"mode","source_feature_id","thickness_mm","k_factor","neutral_axis_basis","flat_area_mm2","flat_volume_mm3","formed_volume_mm3","volume_preservation_assumed","bends"});
  definitions["motion"] = object({
    {"dofs",{{"type","array"},{"maxItems",126},{"items",object({{"mate_id",id},{"coordinate",coordinate},
      {"unit",{{"enum",{"deg","mm"}}}},{"value",real},{"minimum",real},{"maximum",real},{"driven",{{"type","boolean"}}},{"coupling_id",id}},
      {"mate_id","coordinate","unit","value","minimum","maximum","driven"})}}},
    {"poses",{{"type","array"},{"items",id},{"maxItems",64}}}},{"dofs","poses"});
  definitions["assembly_summary"] = object({
    {"parts",{{"type","array"},{"minItems",1},{"maxItems",assembly_leaf_limit},{"items",object({
      {"id",occurrence},{"input",id},{"transform",{{"type","array"},{"items",real},{"minItems",16},{"maxItems",16}}},
      {"bounds_mm",bounds_output},{"volume_mm3",nonnegative}}, {"id","input","transform","bounds_mm","volume_mm3"})}}},
    {"mates",{{"type","array"},{"maxItems",63},{"items",object({{"id",id},{"type",{{"enum",{"rigid","revolute","slider","cylindrical"}}}},{"parent",id},{"child",id}}, {"id","type","parent","child"})}}},
    {"motion",{{"$ref","#/$defs/motion"}}},{"tree",structure},
    {"mechanisms",{{"type","array"},{"maxItems",256},{"items",object({{"assembly_id",id},
      {"occurrences",{{"type","array"},{"minItems",1},{"maxItems",8192},{"items",{{"anyOf",Json::array({occurrence,Json{{"const",""}}})}}}}},
      {"motion",{{"$ref","#/$defs/motion"}}}},{"assembly_id","occurrences","motion"})}}}
  }, {"parts","mates"});
  const Json face_id = {{"type","string"},{"pattern","^face-[1-9][0-9]*$"}};
  const Json edge_id = {{"type","string"},{"pattern","^edge-[1-9][0-9]*$"}};
  const Json local_id = {{"type","string"},{"pattern","^(face|edge)-[1-9][0-9]*$"}};
  const Json history_entry = object({
    {"source_feature_id",id},{"source_kind",{{"enum",{"face","edge"}}}},{"source_id",local_id},
    {"relation",{{"enum",{"unchanged","modified","generated","deleted"}}}},
    {"result_kind",{{"enum",{"face","edge"}}}},{"result_id",local_id},
    {"instance_index",{{"type","integer"},{"minimum",0},{"maximum",63}}},{"part_id",occurrence}
  }, {"source_feature_id","source_kind","source_id","relation"});
  const Json provenance = object({
    {"feature_id",id},{"feature_type",{{"type","string"}}},{"content_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},
    {"font_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},{"font_sha256_by_style",{{"type","object"},{"maxProperties",16},{"additionalProperties",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}}}},
    {"dependencies",{{"type","array"},{"items",id},{"maxItems",64}}},
    {"reference_policy",{{"const","geometric_replay"}}},{"history_lifetime",{{"const","evaluation"}}},
    {"history",{{"type","array"},{"items",history_entry},{"maxItems",10000}}},{"history_truncated",{{"type","boolean"}}}
  }, {"feature_id","feature_type","dependencies","reference_policy","history_lifetime","history","history_truncated"});
  const Json face_output = object({
    {"id",face_id},{"surface_kind",{{"enum",{"plane","cylinder","cone","sphere","torus","bezier","bspline","revolution","extrusion","offset","other"}}}},
    {"area_mm2",nonnegative},{"center_mm",point_output},{"bounds_mm",bounds_output},{"normal",point_output},{"part_id",occurrence},
    {"selector",{{"$ref","#/$defs/face_selector"}}}
  }, {"id","surface_kind","area_mm2","center_mm","bounds_mm"});
  const Json edge_output = object({
    {"id",edge_id},{"curve_kind",{{"enum",{"line","circle","ellipse","hyperbola","parabola","bezier","bspline","offset","other"}}}},
    {"length_mm",nonnegative},{"center_mm",point_output},{"bounds_mm",bounds_output},{"direction",point_output},
    {"degenerate",{{"type","boolean"}}},{"closed",{{"type","boolean"}}},{"endpoints_mm",{{"type","array"},{"items",point_output},{"minItems",2},{"maxItems",2}}},{"radius_mm",nonnegative},{"axis",point_output},{"selector",{{"$ref","#/$defs/selector"}}},{"part_id",occurrence}
  }, {"id","curve_kind","length_mm","center_mm","bounds_mm","degenerate"});
  const Json fraction={{"type","number"},{"minimum",0},{"maximum",1}};
  definitions["curve_query"]=object({{"stations",{{"type","array"},{"items",fraction},{"minItems",1},{"maxItems",257}}},{"edge",{{"$ref","#/$defs/selector"}}}},Json::array());
  definitions["curve_samples"]=object({{"feature_id",id},{"units",{{"const","mm"}}},{"parameterization",{{"const","normalized_arc_length"}}},{"length_mm",nonnegative},{"closed",{{"type","boolean"}}},
    {"samples",{{"type","array"},{"items",object({{"fraction",fraction},{"point_mm",point_output},{"tangent",point_output},{"curvature_per_mm",nonnegative}},{"fraction","point_mm","tangent","curvature_per_mm"})},{"minItems",1},{"maxItems",257}}}}, {"feature_id","units","parameterization","length_mm","closed","samples"});
  definitions["face"]=face_output; definitions["edge"]=edge_output;
  definitions["topology"]=object({
    {"schema_version",{{"const",1}}},{"units",{{"const","mm"}}},{"feature_id",id},{"selection_lifetime",{{"const","evaluation"}}},
    {"faces",{{"type","array"},{"items",face_output},{"maxItems",10000}}},
    {"edges",{{"type","array"},{"items",edge_output},{"maxItems",10000}}},{"provenance",provenance}
  }, {"schema_version","units","feature_id","selection_lifetime","faces","edges","provenance"});
  definitions["mesh"]=object({
    {"schema_version",{{"const",1}}},{"units",{{"const","mm"}}},{"feature_id",id},{"selection_lifetime",{{"const","evaluation"}}},
    {"linear_deflection_mm",{{"const",0.1}}},{"angular_deflection_rad",{{"const",0.5}}},
    {"positions",{{"type","array"},{"items",point_output},{"maxItems",200000}}},
    {"triangles",{{"type","array"},{"items",{{"type","array"},{"items",{{"type","integer"},{"minimum",0},{"maximum",199999}}},{"minItems",3},{"maxItems",3}}},{"maxItems",200000}}},
    {"triangle_faces",{{"type","array"},{"items",face_id},{"maxItems",200000}}},
    {"edges",{{"type","array"},{"items",object({{"id",edge_id},{"part_id",occurrence},{"points",{{"type","array"},{"items",point_output},{"maxItems",200000}}}}, {"id","points"})},{"maxItems",10000}}}
  }, {"schema_version","units","feature_id","selection_lifetime","linear_deflection_mm","angular_deflection_rad","positions","triangles","triangle_faces","edges"});
  return definitions;
}

namespace {
void collect_references(const Json& value, std::set<std::string>& names, std::vector<std::string>& pending) {
  static const std::string prefix = "#/$defs/";
  if (value.is_array()) for (const auto& item : value) collect_references(item, names, pending);
  if (!value.is_object()) return;
  for (const auto& [key, item] : value.items()) {
    if (key == "$ref" && item.is_string()) {
      const auto& target = item.get_ref<const std::string&>();
      if (target.rfind(prefix, 0) != 0) throw std::logic_error("Schema reference leaves its document: " + target);
      const auto name = target.substr(prefix.size(), target.find('/', prefix.size()) - prefix.size());
      if (names.insert(name).second) pending.push_back(name);
    } else if ((key == "properties" || key == "patternProperties" || key == "dependentSchemas") && item.is_object()) {
      // These maps contain property names, not schema keywords. A property
      // named const/enum/default can itself contain a real schema reference.
      for (const auto& property : item.items()) collect_references(property.value(), names, pending);
    } else if (key != "$defs" && key != "const" && key != "enum" && key != "default" && key != "examples")
      collect_references(item, names, pending);
  }
}
}

void prune_definitions(Json& schema) {
  if (!schema.is_object() || !schema.contains("$defs")) return;
  const auto available = std::move(schema.at("$defs"));
  schema.erase("$defs");
  std::set<std::string> names; std::vector<std::string> pending;
  collect_references(schema, names, pending);
  // Definitions refer to each other, so close the set transitively.
  while (!pending.empty()) {
    const auto name = pending.back(); pending.pop_back();
    if (!available.contains(name)) throw std::logic_error("Schema references an undefined definition: " + name);
    collect_references(available.at(name), names, pending);
  }
  if (names.empty()) return;
  Json used = Json::object();
  for (const auto& name : names) used[name] = available.at(name);
  schema["$defs"] = std::move(used);
}

std::array<double, 3> vector3(const Json& value, const Json& parameters, const std::string& unit) {
  if (!value.is_array() || value.size() != 3) throw Error("invalid_model", "Expected a 3-element vector");
  return {scalar(value[0], parameters, unit), scalar(value[1], parameters, unit), scalar(value[2], parameters, unit)};
}

namespace {
void validate_selector(const Json& selector, const Json& parameters, const std::string& input) {
  fields(selector, {"type", "feature_id", "curve_kind", "expected_count"}, {"direction", "center", "length","radius","axis","endpoints","closed"});
  if (text_field(selector, "type") != "geometric")
    throw Error("invalid_model", "Only geometric design references can be saved; evaluated selections cannot be persisted");
  if (text_field(selector, "feature_id") != input)
    throw Error("invalid_model", "Selector feature_id must equal the edge-operation input");
  static const std::set<std::string> kinds = {"line", "circle", "ellipse", "hyperbola", "parabola", "bezier", "bspline", "offset", "other"};
  if (!kinds.contains(text_field(selector, "curve_kind")))
    throw Error("invalid_model", "Unsupported selector curve_kind");
  const auto& count = selector.at("expected_count");
  if (!count.is_number_integer() || count < 1 || count > 10000)
    throw Error("invalid_model", "Selector expected_count must be an integer from 1 to 10000");
  if(selector.contains("closed")&&!selector.at("closed").is_boolean())throw Error("invalid_model","Selector closed must be boolean");
  if(selector.contains("radius")){if(selector.at("curve_kind")!="circle")throw Error("invalid_model","Radius selection requires a circular edge");const auto& p=selector.at("radius");fields(p,{"value","tolerance"});if(scalar(p.at("value"),parameters)<=0||scalar(p.at("tolerance"),parameters)<1e-9)throw Error("invalid_model","Radius selector needs positive radius and tolerance");}
  if(selector.contains("axis")){if(selector.at("curve_kind")!="circle"&&selector.at("curve_kind")!="ellipse")throw Error("invalid_model","Axis selection requires circle or ellipse");const auto& p=selector.at("axis");fields(p,{"vector","tolerance"});const auto v=vector3(p.at("vector"),parameters,"dimensionless");const auto t=scalar(p.at("tolerance"),parameters,"rad");if(std::hypot(v[0],v[1],v[2])<1e-12||t<1e-9||t>std::numbers::pi/2)throw Error("invalid_model","Axis selector requires a nonzero vector and tolerance in (0,pi/2]");}
  if(selector.contains("endpoints")){const auto& p=selector.at("endpoints");fields(p,{"points","tolerance"});if(!p.at("points").is_array()||p.at("points").size()!=2||scalar(p.at("tolerance"),parameters)<1e-9)throw Error("invalid_model","Endpoint selection requires two points and positive tolerance");for(const auto& point:p.at("points"))vector3(point,parameters);}
  for (const auto* key : {"direction", "center", "length"}) {
    if (!selector.contains(key)) continue;
    const auto& predicate = selector.at(key);
    const std::string name = key;
    if (name == "direction") fields(predicate, {"vector", "tolerance"});
    else if (name == "center") fields(predicate, {"point", "tolerance"});
    else fields(predicate, {"value", "tolerance"});
    const double tolerance = scalar(predicate.at("tolerance"), parameters, name == "direction" ? "rad" : "mm");
    if (tolerance < 1e-9 || (name == "direction" && tolerance > std::numbers::pi / 2))
      throw Error("invalid_model", "Selector tolerances must be positive; direction tolerance cannot exceed pi/2 radians");
    if (name == "direction") {
      if (selector.at("curve_kind") != "line")
        throw Error("invalid_model", "Direction predicates currently require curve_kind line");
      const auto vector = vector3(predicate.at("vector"), parameters, "dimensionless");
      if (std::hypot(vector[0], vector[1], vector[2]) < 1e-12)
        throw Error("invalid_model", "Selector direction must be a nonzero vector");
    } else if (name == "center") vector3(predicate.at("point"), parameters);
    else if (scalar(predicate.at("value"), parameters) < 0)
      throw Error("invalid_model", "Selector edge length must not be negative");
  }
}
std::array<double,3> unit_vector(const Json& value, const Json& parameters) {
  auto result = vector3(value, parameters, "dimensionless");
  const auto magnitude = std::hypot(result[0], result[1], result[2]);
  if (magnitude < 1e-12) throw Error("invalid_model", "Direction must be a nonzero vector");
  for (auto& item : result) item /= magnitude;
  return result;
}
void workplane(const Json& value, const Json& parameters) {
  fields(value, {"origin", "normal", "x_direction"});
  vector3(value.at("origin"), parameters);
  const auto normal = unit_vector(value.at("normal"), parameters);
  const auto x = unit_vector(value.at("x_direction"), parameters);
  if (std::abs(normal[0]*x[0] + normal[1]*x[1] + normal[2]*x[2]) > 1e-9)
    throw Error("invalid_model", "Workplane normal and x_direction must be perpendicular");
}
void axis(const Json& value, const Json& parameters, const char* direction_field) {
  fields(value, {"origin", direction_field});
  vector3(value.at("origin"), parameters);
  unit_vector(value.at(direction_field), parameters);
}
void placement(const Json& value, const Json& parameters) {
  fields(value, {}, {"translation", "rotation"});
  if (value.contains("translation")) vector3(value.at("translation"), parameters);
  if (value.contains("rotation")) {
    const auto& rotation = value.at("rotation");
    fields(rotation, {"origin", "axis", "angle_deg"});
    vector3(rotation.at("origin"), parameters);
    unit_vector(rotation.at("axis"), parameters);
    scalar(rotation.at("angle_deg"), parameters, "deg");
  }
}
void curve_segments(const Json& segments,const Json& parameters,std::size_t dimensions,const std::string& coordinate_unit="mm") {
  if (!segments.is_array() || segments.empty() || segments.size()>64)
    throw Error("invalid_model","A curve wire needs 1 to 64 ordered segments");
  const auto point=[&](const Json& value,const std::string& unit="") {
    if (!value.is_array() || value.size()!=dimensions)
      throw Error("invalid_model","Curve point/vector has the wrong coordinate count");
    double squared=0;
    for (const auto& component:value) {const double v=scalar(component,parameters,unit.empty()?coordinate_unit:unit);squared+=v*v;}
    return squared;
  };
  for (std::size_t i=0;i<segments.size();++i) {
    try {
      const auto& segment=segments[i]; const auto type=text_field(segment,"type");
      if (type=="line" || type=="arc"||type=="tangent_arc") {
        if (type=="line") fields(segment,{"type","start","end"});
        else if(type=="arc"){fields(segment,{"type","start","mid","end"});point(segment.at("mid"));}
        else {fields(segment,{"type","start","end","tangent"});if(point(segment.at("tangent"),"dimensionless")<1e-24)throw Error("invalid_model","Arc tangent must be nonzero");}
        point(segment.at("start"));point(segment.at("end"));
      } else if (type=="bezier" || type=="spline") {
        if (type=="bezier") fields(segment,{"type","points"},{"weights"});
        else fields(segment,{"type","points"},{"periodic","start_tangent","end_tangent"});
        const auto& points=segment.at("points");
        const auto maximum=type=="bezier"?26u:64u;
        if (!points.is_array() || points.size()<2 || points.size()>maximum)
          throw Error("invalid_model","Curve point count is outside its supported bounds",{{"maximum",maximum}});
        for (const auto& p:points) point(p);
        if(type=="bezier"&&segment.contains("weights")){const auto& weights=segment.at("weights");if(!weights.is_array()||weights.size()!=points.size())throw Error("invalid_model","Bezier weights must match its control points");for(const auto& weight:weights){const auto value=scalar(weight,parameters,"dimensionless");if(value<1e-8||value>1e6)throw Error("invalid_model","Bezier weights must lie between 1e-8 and 1e6");}}
        if (type=="spline") {
          if (segment.contains("periodic") && !segment.at("periodic").is_boolean())
            throw Error("invalid_model","Spline periodic must be boolean");
          const bool periodic=segment.value("periodic",false);
          if (periodic && points.size()<3) throw Error("invalid_model","A periodic spline needs at least three distinct points");
          if (segment.contains("start_tangent")!=segment.contains("end_tangent"))
            throw Error("invalid_model","Spline endpoint tangents must be supplied together");
          if (segment.contains("start_tangent")) {
            if (periodic) throw Error("invalid_model","Periodic splines cannot have endpoint tangents");
            if (point(segment.at("start_tangent"),"dimensionless")<1e-24 || point(segment.at("end_tangent"),"dimensionless")<1e-24)
              throw Error("invalid_model","Spline endpoint tangents must be nonzero");
          }
        }
      } else throw Error("invalid_model","Unsupported curve segment: "+type);
    } catch (const Error& e) {
      auto details=e.details;details["segment_index"]=i;throw Error(e.code,e.what(),details);
    }
  }
}
void assembly(const Json& feature, const Json& parameters, const std::map<std::string,std::string>& types) {
  fields(feature, {"id", "type", "parts"}, {"mates","bom","couplings","poses"});
  const auto& parts = feature.at("parts");
  if (!parts.is_array() || parts.empty() || parts.size() > 64)
    throw Error("invalid_model", "An assembly needs 1 to 64 parts");
  std::set<std::string> part_ids, placed, source_inputs;
  for (const auto& part : parts) {
    const auto part_id = text_field(part,"id");
    try {
      fields(part, {"id", "input"}, {"placement"});
      model_identifier(part_id);
      if (!part_ids.insert(part_id).second) throw Error("invalid_model", "Duplicate assembly part: " + part_id);
      const auto input = text_field(part,"input");
      if (!types.contains(input) || (is_sketch_feature_type(types.at(input))||is_surface_feature_type(types.at(input))||is_curve_feature_type(types.at(input))))
        throw Error("invalid_model", "Assembly part input must name an earlier solid or assembly feature", {{"source_feature_id",input}});
      source_inputs.insert(input);
      if (part.contains("placement")) { placement(part.at("placement"),parameters); placed.insert(part_id); }
    } catch (const Error& e) {
      auto details=e.details; details["part_id"]=part_id;
      throw Error(e.code,e.what(),details);
    }
  }
  if (feature.contains("bom")) {
    const auto& bom=feature.at("bom");
    if (!bom.is_array() || bom.size()>64) throw Error("invalid_model","Assembly BOM metadata permits at most 64 items");
    std::set<std::string> inputs;std::set<int> numbers;
    for (const auto& item:bom) {
      fields(item,{"input"},{"item_number","part_number","description","material","purchase"});
      const auto input=text_field(item,"input");
      if (!source_inputs.contains(input)) throw Error("invalid_model","BOM metadata input must be used by an assembly part",{{"source_feature_id",input}});
      if (!inputs.insert(input).second) throw Error("invalid_model","Duplicate BOM metadata source input",{{"source_feature_id",input}});
      if (item.contains("item_number")) {
        const auto& number=item.at("item_number");
        if (!number.is_number_integer() || number<1 || number>999) throw Error("invalid_model","BOM item_number must be an integer from 1 to 999",{{"source_feature_id",input}});
        if (!numbers.insert(number.get<int>()).second) throw Error("invalid_model","BOM item numbers must be unique",{{"source_feature_id",input}});
      }
      for (const auto* key:{"part_number","description","material"}) if (item.contains(key)) {
        const auto value=text_field(item,key);const std::size_t maximum=std::string(key)=="description"?120:64;
        if (value.size()>maximum) throw Error("invalid_model",std::string("BOM ")+key+" exceeds its text limit",{{"source_feature_id",input}});
        for (const unsigned char c:value) if (c<32 || c>126)
          throw Error("invalid_model","BOM metadata must contain only printable ASCII",{{"source_feature_id",input}});
      }
      if(item.contains("purchase")) {
        try{validate_purchase(item.at("purchase"));}
        catch(const Error& error){auto details=error.details;details["source_feature_id"]=input;throw Error(error.code,error.what(),details);}
      }
    }
  }
  if (!feature.contains("mates")) {assembly_motion(feature,parameters);return;}
  const auto& mates = feature.at("mates");
  if (!mates.is_array() || mates.size() > 63) throw Error("invalid_model", "An assembly permits at most 63 mates");
  std::set<std::string> mate_ids;
  std::map<std::string,std::string> parents;
  for (const auto& mate : mates) {
    const auto mate_id = text_field(mate,"id");
    try {
      const auto type=text_field(mate,"type");
      if (type=="rigid") fields(mate, {"id","type","parent","child","parent_frame","child_frame"}, {"offset","angle_deg"});
      else if (type=="revolute") fields(mate,{"id","type","parent","child","parent_frame","child_frame","angle_limits_deg"},{"offset","angle_deg"});
      else if (type=="slider") fields(mate,{"id","type","parent","child","parent_frame","child_frame","travel_limits_mm"},{"offset","angle_deg","travel_mm"});
      else if (type=="cylindrical") fields(mate,{"id","type","parent","child","parent_frame","child_frame","angle_limits_deg","travel_limits_mm"},{"offset","angle_deg","travel_mm"});
      else throw Error("invalid_model","Mate type must be rigid, revolute, slider or cylindrical");
      model_identifier(mate_id);
      if (!mate_ids.insert(mate_id).second) throw Error("invalid_model", "Duplicate assembly mate: " + mate_id);
      const auto parent=text_field(mate,"parent"), child=text_field(mate,"child");
      if (!part_ids.contains(parent) || !part_ids.contains(child)) throw Error("invalid_model", "Mate must name existing assembly parts");
      if (parent == child) throw Error("invalid_model", "A part cannot mate to itself");
      if (!parents.emplace(child,parent).second) throw Error("invalid_model", "A part permits only one incoming mate", {{"part_id",child}});
      if (placed.contains(child)) throw Error("invalid_model", "A mated child cannot have an explicit placement", {{"part_id",child}});
      workplane(mate.at("parent_frame"),parameters);
      workplane(mate.at("child_frame"),parameters);
      if (mate.contains("offset")) vector3(mate.at("offset"),parameters);
      if (mate.contains("angle_deg")) scalar(mate.at("angle_deg"),parameters,"deg");
    } catch (const Error& e) {
      auto details=e.details; details["mate_id"]=mate_id;
      throw Error(e.code,e.what(),details);
    }
  }
  for (const auto& part_id : part_ids) {
    std::set<std::string> path;
    auto current=part_id;
    while (parents.contains(current)) {
      if (!path.insert(current).second) throw Error("invalid_model", "Assembly mate graph contains a cycle", {{"part_id",current}});
      current=parents.at(current);
    }
  }
  assembly_motion(feature,parameters);
}
}

void validate_face_selector(const Json& selector,const Json& parameters,const std::string& input) {
  fields(selector,{"type","feature_id","surface_kind","expected_count"},{"normal","center","area"});
  if(text_field(selector,"type")!="geometric")throw Error("invalid_model","Only geometric face references can be persisted");
  if(text_field(selector,"feature_id")!=input)throw Error("invalid_model","Face selector feature_id must equal its input feature");
  static const std::set<std::string> kinds={"plane","cylinder","cone","sphere","torus","bezier","bspline","revolution","extrusion","offset","other"};
  if(!kinds.contains(text_field(selector,"surface_kind")))throw Error("invalid_model","Unsupported face selector surface_kind");
  const auto& count=selector.at("expected_count");
  if(!count.is_number_integer()||count<1||count>10000)throw Error("invalid_model","Face selector expected_count must be between 1 and 10000");
  for(const auto* key:{"normal","center","area"}) {
    if(!selector.contains(key))continue;
    const auto& predicate=selector.at(key);const std::string name=key;
    const auto unit=name=="normal"?"rad":name=="area"?"mm2":"mm";
    if(name=="normal")fields(predicate,{"vector","tolerance"});
    else if(name=="center")fields(predicate,{"point","tolerance"});
    else fields(predicate,{"value","tolerance"});
    const auto tolerance=scalar(predicate.at("tolerance"),parameters,unit);
    if(tolerance<1e-9||(name=="normal"&&tolerance>std::numbers::pi/2))
      throw Error("invalid_model","Face selector tolerances must be positive; normal tolerance cannot exceed pi/2 radians");
    if(name=="normal") {
      if(selector.at("surface_kind")!="plane")throw Error("invalid_model","A normal predicate requires a planar face");
      unit_vector(predicate.at("vector"),parameters);
    } else if(name=="center")vector3(predicate.at("point"),parameters);
    else if(scalar(predicate.at("value"),parameters,"mm2")<=0)throw Error("invalid_model","Selected face area must be positive");
  }
}
void validate_face_selection(const Json& selection,const Json& parameters,const std::string& input) {
  if(selection.is_array()) {
    if(selection.empty()||selection.size()>64)throw Error("invalid_model","Face selection requires 1–64 geometric selectors");
    for(const auto& selector:selection)validate_face_selector(selector,parameters,input);
  } else validate_face_selector(selection,parameters,input);
}
void validate_curve_query(const Json& query,const Json& parameters,const std::string& feature_id) {
  fields(query,{}, {"stations","edge"});
  if(query.contains("stations")){const auto& stations=query.at("stations");if(!stations.is_array()||stations.empty()||stations.size()>257)throw Error("invalid_argument","Curve query requires 1–257 stations");for(const auto& value:stations)if(!value.is_number()||number(value)<0||number(value)>1)throw Error("invalid_argument","Curve query stations must be numeric fractions in [0,1]");}
  if(query.contains("edge")){validate_selector(query.at("edge"),parameters,feature_id);if(query.at("edge").at("expected_count")!=1)throw Error("invalid_argument","Curve query edge must select exactly one edge");}
}

void validate_model(const Json& model) {
  fields(model, {"schema_version", "units", "parameters", "features", "output"}, {"components"});
  validate_payload_size(model);
  if (!model.at("schema_version").is_number_integer() || model.at("schema_version") != 1)
    throw Error("unsupported_schema", "Only model schema_version 1 is supported");
  if (text_field(model, "units") != "mm") throw Error("invalid_model", "Only millimeters are supported");
  const auto& parameters = model.at("parameters");
  if (!parameters.is_object() || parameters.size() > 128)
    throw Error("invalid_model", "parameters must be an object with at most 128 entries");
  for (const auto& [key, value] : parameters.items()) { model_identifier(key); number(value); }
  const auto& features = model.at("features");
  if (!features.is_array() || features.empty() || features.size() > 256)
    throw Error("invalid_model", "A model needs 1–256 features");
  std::set<std::string> prior;
  std::map<std::string,std::string> types;
  std::map<std::string,std::size_t> leaf_counts, depths;
  std::size_t assembly_parts = 0;
  std::size_t expanded_parts = 0;
  for (const auto& feature : features) {
    const auto id = text_field(feature, "id");
    try {
    model_identifier(id);
    if (prior.contains(id)) throw Error("invalid_model", "Duplicate feature: " + id);
    const auto type = text_field(feature, "type");
    auto positive = [&](const Json& v) {
      if (scalar(v, parameters) < 1e-5) throw Error("invalid_model", "Dimensions must be at least 0.00001 mm", {{"feature_id", id}});
    };
    auto dependency = [&](const char* key) {
      const auto target = text_field(feature, key);
      if (!prior.contains(target)) throw Error("invalid_model", "Feature must refer to an earlier feature: " + target, {{"feature_id", id}});
      if (is_sketch_feature_type(types.at(target))||is_surface_feature_type(types.at(target))||is_curve_feature_type(types.at(target))) throw Error("invalid_model", "This operation requires a solid input", {{"feature_id", id}});
      if (types.at(target) == "assembly") throw Error("invalid_model", "Edit assembly source parts before assembling; solid operations cannot consume assemblies", {{"feature_id", id}});
    };
    auto sketch_dependency = [&](const std::string& target) {
      if (!prior.contains(target) || !is_sketch_feature_type(types.at(target)))
        throw Error("invalid_model", "Profile reference must name an earlier sketch", {{"feature_id", id}, {"source_feature_id", target}});
    };
    if(type=="surface_bezier"||type=="surface_bspline") {
      if(type=="surface_bezier")fields(feature,{"id","type","control_points"},{"weights"});
      else fields(feature,{"id","type","control_points","degree_u","degree_v","knots_u","knots_v","multiplicities_u","multiplicities_v"},{"weights"});
      const auto& points=feature.at("control_points");
      if(!points.is_array()||points.size()<2||points.size()>26)throw Error("invalid_model","Surface control grid needs 2–26 rows and columns");
      const auto columns=points[0].size();if(!points[0].is_array()||columns<2||columns>26)throw Error("invalid_model","Surface control grid needs 2–26 rows and columns");
      for(const auto& row:points) {
        if(!row.is_array()||row.size()!=columns)throw Error("invalid_model","Surface control grid must be rectangular");
        for(const auto& point:row)vector3(point,parameters);
      }
      if(feature.contains("weights")) {
        const auto& weights=feature.at("weights");if(!weights.is_array()||weights.size()!=points.size())throw Error("invalid_model","Surface weights must match the control grid");
        for(const auto& row:weights) {
          if(!row.is_array()||row.size()!=columns)throw Error("invalid_model","Surface weights must match the control grid");
          for(const auto& value:row)if(scalar(value,parameters,"dimensionless")<1e-8)throw Error("invalid_model","Surface weights must be positive and at least 0.00000001");
        }
      }
      if(type=="surface_bspline")for(const auto* suffix:{"u","v"}) {
        const auto degree_name=std::string("degree_")+suffix,knot_name=std::string("knots_")+suffix,mult_name=std::string("multiplicities_")+suffix;
        const auto& degree_value=feature.at(degree_name);
        if(!degree_value.is_number_integer()||degree_value<1||degree_value>25)throw Error("invalid_model","B-spline surface degrees must be integers from 1 to 25");
        const auto degree=degree_value.get<int>();const auto& knots=feature.at(knot_name);const auto& mults=feature.at(mult_name);
        if(!knots.is_array()||knots.size()<2||knots.size()>128||!mults.is_array()||mults.size()!=knots.size())throw Error("invalid_model","Surface knot and multiplicity arrays must match with 2–128 distinct knots");
        int sum=0;double previous=-std::numeric_limits<double>::infinity();
        for(std::size_t k=0;k<knots.size();++k) {
          const auto knot=scalar(knots[k],parameters,"dimensionless");if(knot<=previous)throw Error("invalid_model","Surface knots must be strictly increasing");previous=knot;
          const auto maximum=(k==0||k+1==knots.size())?degree+1:degree;
          if(!mults[k].is_number_integer()||mults[k]<1||mults[k]>maximum)throw Error("invalid_model","B-spline surface multiplicity exceeds its degree bound");sum+=mults[k].get<int>();
        }
        const auto pole_count=std::string(suffix)=="u"?points.size():columns;
        if(sum-degree-1!=static_cast<int>(pole_count))throw Error("invalid_model","B-spline surface multiplicities must sum to pole count plus degree plus one");
      }
    } else if(type=="surface_trim") {
      fields(feature,{"id","type","input"},{"u_range","v_range","boundary","holes"});const auto input=text_field(feature,"input");
      const bool rectangle=feature.contains("u_range")||feature.contains("v_range");
      if(rectangle?(!feature.contains("u_range")||!feature.contains("v_range")||feature.contains("boundary")||feature.contains("holes")):!feature.contains("boundary"))throw Error("invalid_model","Surface trim requires either both UV ranges or a boundary with optional holes");
      if(!rectangle){curve_segments(feature.at("boundary"),parameters,2,"dimensionless");if(feature.contains("holes")){if(!feature.at("holes").is_array()||feature.at("holes").size()>16)throw Error("invalid_model","Surface trim permits at most 16 holes");for(const auto& hole:feature.at("holes"))curve_segments(hole,parameters,2,"dimensionless");}}
      if(!prior.contains(input)||!is_surface_feature_type(types.at(input))||types.at(input)=="surface_shell")throw Error("invalid_model","Surface trim requires one earlier parametric surface patch");
      if(rectangle)for(const auto* name:{"u_range","v_range"}) {
        const auto& range=feature.at(name);if(!range.is_array()||range.size()!=2)throw Error("invalid_model","Surface UV ranges require two scalar bounds");
        if(scalar(range[1],parameters,"dimensionless")-scalar(range[0],parameters,"dimensionless")<1e-9)throw Error("invalid_model","Surface UV bounds must increase by at least 0.000000001");
      }
    } else if(type=="curve") {
      fields(feature,{"id","type","path"});fields(feature.at("path"),{"type","segments"});
      if(feature.at("path").at("type")!="wire")throw Error("invalid_model","Curve path must be a world-coordinate exact wire");
      curve_segments(feature.at("path").at("segments"),parameters,3);
    } else if(type=="curve_helix") {
      fields(feature,{"id","type","frame","radius","pitch","turns"},{"handedness"});workplane(feature.at("frame"),parameters);
      const auto radius=scalar(feature.at("radius"),parameters),pitch=scalar(feature.at("pitch"),parameters),turns=scalar(feature.at("turns"),parameters,"dimensionless");
      if(radius<1e-5||pitch<1e-5||turns<1e-4||turns>256||pitch*turns>1e6||std::hypot(2*std::numbers::pi*radius,pitch)*turns>1e7)throw Error("invalid_model","Helix requires positive radius/pitch, 0.0001–256 turns, height <=1e6 mm and length <=1e7 mm");
      if(feature.contains("handedness")&&feature.at("handedness")!="left"&&feature.at("handedness")!="right")throw Error("invalid_model","Helix handedness must be left or right");
    } else if(type=="curve_trim"||type=="curve_tangent_line"||type=="curve_tangent_arc") {
      if(type=="curve_trim")fields(feature,{"id","type","input","start","end"});else if(type=="curve_tangent_line")fields(feature,{"id","type","input","position","length"},{"reverse"});else fields(feature,{"id","type","input","position","end"},{"reverse"});
      const auto input=text_field(feature,"input");if(!prior.contains(input)||!is_curve_feature_type(types.at(input)))throw Error("invalid_model","Curve operation requires an earlier exact curve");
      const auto station=[&](const char* key){const auto value=scalar(feature.at(key),parameters,"dimensionless");if(value<0||value>1)throw Error("invalid_model","Curve positions must be normalized arc-length fractions in [0,1]");return value;};
      if(type=="curve_trim"){if(station("end")-station("start")<1e-9)throw Error("invalid_model","Curve trim end must exceed start");}else {station("position");if(feature.contains("reverse")&&!feature.at("reverse").is_boolean())throw Error("invalid_model","Curve reverse must be boolean");if(type=="curve_tangent_line")positive(feature.at("length"));else vector3(feature.at("end"),parameters);}
    } else if(type=="curve_constrained_line"||type=="curve_constrained_arc") {
      if(type=="curve_constrained_line")fields(feature,{"id","type","workplane","constraints","solution"});
      else {fields(feature,{"id","type","workplane","constraints","solution","radius"});positive(feature.at("radius"));}
      workplane(feature.at("workplane"),parameters);const auto& constraints=feature.at("constraints");
      if(!constraints.is_array()||constraints.size()!=2)throw Error("invalid_model","Tangency needs exactly two qualified constraints");
      int edges=0;for(const auto& constraint:constraints){if(constraint.contains("point")){fields(constraint,{"point"});vector3(constraint.at("point"),parameters);}
        else {fields(constraint,{"edge"},{"qualifier"});const auto& edge=constraint.at("edge");const auto input=text_field(edge,"feature_id");
          if(!prior.contains(input)||types.at(input)=="assembly")throw Error("invalid_model","Tangency edge must name an earlier nonassembly feature");
          validate_selector(edge,parameters,input);if(edge.at("expected_count")!=1)throw Error("invalid_model","Each tangency constraint selects exactly one edge");
          const auto qualifier=constraint.value("qualifier",std::string("unqualified"));if(qualifier!="unqualified"&&qualifier!="enclosed"&&qualifier!="enclosing"&&qualifier!="outside")throw Error("invalid_model","Unknown tangency qualifier");++edges;}}
      if(type=="curve_constrained_line"&&edges==0)throw Error("invalid_model","A constrained tangent line requires a curve constraint");
      const auto& solution=feature.at("solution");fields(solution,{"point","tolerance"});vector3(solution.at("point"),parameters);
      const auto tolerance=scalar(solution.at("tolerance"),parameters);if(tolerance<1e-7||tolerance>1e3)throw Error("invalid_model","Solution midpoint tolerance must be between 1e-7 and 1000 mm");
    } else if(type=="curve_extract") {
      fields(feature,{"id","type","input","edges"});const auto input=text_field(feature,"input");if(!prior.contains(input)||types.at(input)=="assembly")throw Error("invalid_model","Curve extraction requires an earlier nonassembly feature");validate_selector(feature.at("edges"),parameters,input);if(feature.at("edges").at("expected_count")!=1)throw Error("invalid_model","Curve extraction requires exactly one edge");
    } else if(type=="surface_project"||type=="curve_project") {
      fields(feature,{"id","type","input","target","faces","direction"});const auto input=text_field(feature,"input"),target=text_field(feature,"target");
      if(!prior.contains(input)||(!is_sketch_feature_type(types.at(input))&&!is_curve_feature_type(types.at(input))))throw Error("invalid_model","Projection requires an earlier sketch or curve");
      if(!prior.contains(target)||is_sketch_feature_type(types.at(target))||is_curve_feature_type(types.at(target))||types.at(target)=="assembly")throw Error("invalid_model","Projection target must be an earlier surface or solid");
      validate_face_selector(feature.at("faces"),parameters,target);if(feature.at("faces").at("expected_count")!=1)throw Error("invalid_model","Projection requires exactly one selected target face");unit_vector(feature.at("direction"),parameters);
    } else if(type=="surface_fill") {
      fields(feature,{"id","type","boundaries","tolerance"},{"points","angular_tolerance","curvature_tolerance"});
      const auto& boundaries=feature.at("boundaries");if(!boundaries.is_array()||boundaries.size()<2||boundaries.size()>32)throw Error("invalid_model","Filling requires 2–32 ordered boundaries");
      for(const auto& b:boundaries){fields(b,{"continuity"},{"curve","input","edge","face","reverse"});const auto continuity=text_field(b,"continuity");if(continuity!="C0"&&continuity!="G1"&&continuity!="G2")throw Error("invalid_model","Filling continuity must be C0, G1 or G2");
        if(b.contains("reverse")&&!b.at("reverse").is_boolean())throw Error("invalid_model","Boundary reverse must be boolean");
        if(b.contains("curve")){if(b.contains("input")||b.contains("edge")||b.contains("face")||continuity!="C0")throw Error("invalid_model","Raw boundary curves support positional C0 constraints; G1/G2 require an explicit support face");curve_segments(Json::array({b.at("curve")}),parameters,3);}
        else {const auto input=text_field(b,"input");if(!prior.contains(input))throw Error("invalid_model","Filling boundary input must be an earlier feature");validate_selector(b.at("edge"),parameters,input);if(b.at("edge").at("expected_count")!=1)throw Error("invalid_model","Filling boundary must select exactly one edge");if(continuity!="C0"&&!b.contains("face"))throw Error("invalid_model","G1/G2 continuity requires an explicit support face");if(b.contains("face")){validate_face_selector(b.at("face"),parameters,input);if(b.at("face").at("expected_count")!=1)throw Error("invalid_model","Filling support must select exactly one face");}}
      }
      if(feature.contains("points")){const auto& points=feature.at("points");if(!points.is_array()||points.size()>64)throw Error("invalid_model","Filling allows at most 64 internal point constraints");for(const auto& p:points)vector3(p,parameters);}
      for(const auto* key:{"tolerance","angular_tolerance","curvature_tolerance"})if(feature.contains(key)){const auto value=number(feature.at(key));if(value<1e-7||value>1e-3)throw Error("invalid_model","Filling tolerances must be between 1e-7 and 1e-3");}
    } else if(type=="surface_gordon") {
      fields(feature,{"id","type","u_curves","v_curves","u_parameters","v_parameters","tolerance"});
      for(const auto* axis:{"u","v"}){const auto curves=std::string(axis)+"_curves",stations=std::string(axis)+"_parameters";const auto& family=feature.at(curves);if(!family.is_array()||family.size()<2||family.size()>16)throw Error("invalid_model","Gordon networks require 2–16 curves in each direction");for(const auto& c:family){curve_segments(Json::array({c}),parameters,3);if(c.at("type")=="arc"||c.at("type")=="tangent_arc"||c.contains("weights")||c.value("periodic",false))throw Error("invalid_model","Gordon networks require nonperiodic polynomial lines, Bezier or interpolating spline curves");}
        const auto& values=feature.at(stations);const auto count=feature.at(std::string(axis)=="u"?"v_curves":"u_curves").size();if(!values.is_array()||values.size()!=count)throw Error("invalid_model","Gordon station counts must match the transverse curve family");double prior=-1;for(const auto& v:values){const double x=scalar(v,parameters,"dimensionless");if(x<0||x>1||x-prior<1e-6)throw Error("invalid_model","Gordon stations must strictly increase in [0,1]");prior=x;}if(scalar(values.front(),parameters,"dimensionless")!=0||scalar(values.back(),parameters,"dimensionless")!=1)throw Error("invalid_model","Gordon network boundaries must occur at stations 0 and 1");}
      const auto tolerance=number(feature.at("tolerance"));if(tolerance<1e-7||tolerance>1e-3)throw Error("invalid_model","Gordon crossing tolerance must be between 1e-7 and 1e-3");
    } else if(type=="surface_shell") {
      fields(feature,{"id","type","inputs","tolerance","closed"});const auto& inputs=feature.at("inputs");std::set<std::string> used;
      if(!inputs.is_array()||inputs.empty()||inputs.size()>64)throw Error("invalid_model","Surface shell needs 1–64 distinct surface inputs");
      for(const auto& value:inputs) {
        if(!value.is_string())throw Error("invalid_model","Surface shell input references must be strings");const auto input=value.get<std::string>();
        if(!prior.contains(input)||!is_surface_feature_type(types.at(input))||!used.insert(input).second)throw Error("invalid_model","Surface shell inputs must name distinct earlier patches or shells");
      }
      if(!feature.at("tolerance").is_number()||!std::isfinite(feature.at("tolerance").get<double>())||feature.at("tolerance")<1e-9||feature.at("tolerance")>1e-3)throw Error("invalid_model","Sewing tolerance must be numeric from 0.000000001 to 0.001 mm");
      if(!feature.at("closed").is_boolean())throw Error("invalid_model","Surface shell closed must be boolean");
    } else if(type=="surface_solid") {
      fields(feature,{"id","type","input"},{"reverse"});const auto input=text_field(feature,"input");
      if(!prior.contains(input)||(types.at(input)!="surface_shell"&&types.at(input)!="import_step_surface"))throw Error("invalid_model","Surface solid requires an earlier explicitly closed shell");
      if(feature.contains("reverse")&&!feature.at("reverse").is_boolean())throw Error("invalid_model","Surface solid reverse must be boolean");
    } else if (type == "box") {
      fields(feature, {"id", "type", "size"}, {"origin"});
      vector3(feature.at("size"), parameters);
      for (const auto& value : feature.at("size")) positive(value);
    } else if (type == "cylinder") {
      fields(feature, {"id", "type", "radius", "height"}, {"origin"});
      positive(feature.at("radius")); positive(feature.at("height"));
    } else if (type == "external_thread") {
      fields(feature,{"id","type","major_diameter","pitch","length"},{"origin","handedness"});
      const auto diameter=scalar(feature.at("major_diameter"),parameters), pitch=scalar(feature.at("pitch"),parameters), length=scalar(feature.at("length"),parameters);
      // Decimal dimensions at the exact ratio limits can differ by an ulp
      // after multiplication (for example 0.3 versus 3 * 0.1).
      constexpr double ratio_slack=1e-12;
      if (pitch<0.1 || diameter>200 || diameter<(3-ratio_slack)*pitch || diameter>(100+ratio_slack)*pitch || length<(1-ratio_slack)*pitch || length>(16+ratio_slack)*pitch)
        throw Error("invalid_model","External threads require pitch >= 0.1 mm, diameter <= 200 mm, diameter/pitch between 3 and 100, and 1–16 turns");
      if (feature.contains("handedness") && text_field(feature,"handedness")!="right" && text_field(feature,"handedness")!="left")
        throw Error("invalid_model","Thread handedness must be right or left");
    } else if (type == "cut" || type == "fuse" || type == "intersection") {
      fields(feature, {"id", "type", "left", "right"});
      dependency("left"); dependency("right");
    } else if (type == "fillet" || type == "chamfer") {
      const auto dimension=type=="fillet"?"radius":"distance";
      if(type=="chamfer")fields(feature, {"id", "type", "input", dimension, "edges"},{"distance2","angle_deg","reference_face"});
      else fields(feature, {"id", "type", "input", dimension, "edges"});
      dependency("input"); positive(feature.at(dimension));
      if(type=="chamfer") {
        const bool asymmetric=feature.contains("distance2")||feature.contains("angle_deg");
        if(feature.contains("distance2")&&feature.contains("angle_deg"))throw Error("invalid_model","Chamfer distance2 and angle_deg are mutually exclusive");
        if(asymmetric!=feature.contains("reference_face"))throw Error("invalid_model","Asymmetric or angle chamfer requires reference_face; symmetric chamfer omits it");
        if(feature.contains("distance2"))positive(feature.at("distance2"));
        if(feature.contains("angle_deg")) {const double angle=scalar(feature.at("angle_deg"),parameters,"deg");if(angle<=0||angle>=90)throw Error("invalid_model","Chamfer angle must be between 0 and 90 degrees");}
        if(asymmetric) {validate_face_selector(feature.at("reference_face"),parameters,text_field(feature,"input"));if(feature.at("reference_face").at("expected_count")!=1)throw Error("invalid_model","Chamfer reference_face must select exactly one adjacent face");}
      }
      const auto& edges = feature.at("edges");
      if (edges.is_string()) {
        if (edges != "all") throw Error("invalid_model", "Edges must be all or a geometric selector");
      } else validate_selector(edges, parameters, text_field(feature, "input"));
    } else if(type=="sheet_metal") {
      fields(feature,{"id","type","input","thickness","k_factor","flanges"});sketch_dependency(text_field(feature,"input"));
      positive(feature.at("thickness"));const auto k=scalar(feature.at("k_factor"),parameters,"dimensionless");
      if(k<0||k>1)throw Error("invalid_model","Sheet-metal k_factor must be between zero and one");
      const auto& flanges=feature.at("flanges");if(!flanges.is_array()||flanges.size()>32)throw Error("invalid_model","Sheet metal allows at most 32 ordered bends");
      std::set<std::string> flange_ids;
      for(const auto& flange:flanges) {
        const auto flange_id=text_field(flange,"id");
        try {
          fields(flange,{"id","inside_radius","angle_deg","length"},{"edge","parent","attachment","fold_line","start_gap","end_gap","hem","relief","miter","cuts"});model_identifier(flange_id);
          if(flange_ids.contains(flange_id))throw Error("invalid_model","Sheet-metal flange IDs must be unique");
          if(static_cast<int>(flange.contains("edge"))+static_cast<int>(flange.contains("parent"))+static_cast<int>(flange.contains("fold_line"))!=1)throw Error("invalid_model","A flange needs exactly one edge, parent or fold_line attachment");
          if(flange.contains("edge")) {
            validate_selector(flange.at("edge"),parameters,text_field(feature,"input"));
            if(flange.at("edge").at("curve_kind")!="line"||flange.at("edge").at("expected_count")!=1)throw Error("invalid_model","A flange must select exactly one straight outer profile edge");
          }
          if(flange.contains("parent")&&!flange_ids.contains(text_field(flange,"parent")))throw Error("invalid_model","Flange parent must name an earlier flange");
          if(flange.contains("attachment")&&(!flange.contains("parent")||(flange.at("attachment")!="tip"&&flange.at("attachment")!="start"&&flange.at("attachment")!="end")))throw Error("invalid_model","Attachment must be tip, start or end of a named parent");
          if(flange.contains("fold_line")){const auto& line=flange.at("fold_line");if(!line.is_array()||line.size()!=2)throw Error("invalid_model","A fold line has two endpoints");for(const auto& point:line)vector3(point,parameters);}
          flange_ids.insert(flange_id);
          positive(flange.at("inside_radius"));positive(flange.at("length"));
          const auto angle=std::abs(scalar(flange.at("angle_deg"),parameters,"deg"));
          if(flange.contains("hem")&&!flange.at("hem").is_boolean())throw Error("invalid_model","Hem is a boolean");
          const bool hem=flange.value("hem",false);
          if(angle<0.01||(hem?std::abs(angle-180)>1e-9:angle>=179.99))throw Error("invalid_model","Hem bends require exactly 180 degrees; other bends require magnitude at least 0.01 and below 179.99 degrees");
          for(const auto* gap:{"start_gap","end_gap"})if(flange.contains(gap)&&scalar(flange.at(gap),parameters)<0)throw Error("invalid_model","Flange endpoint gaps must be nonnegative");
          if(flange.contains("relief")){const auto& relief=flange.at("relief");fields(relief,{"width","depth"});positive(relief.at("width"));positive(relief.at("depth"));if(flange.contains("parent"))throw Error("invalid_model","Endpoint relief belongs to base-edge or internal folds");}
          if(flange.contains("miter")){const auto& miter=flange.at("miter");fields(miter,{"start_deg","end_deg"});for(const auto* key:{"start_deg","end_deg"}){const auto angle=scalar(miter.at(key),parameters,"deg");if(angle<0||angle>=89)throw Error("invalid_model","Miter endpoint angles must be in [0,89) degrees");}}
          if(flange.contains("cuts")){const auto& cuts=flange.at("cuts");if(!cuts.is_array()||cuts.size()>32)throw Error("invalid_model","A flange allows at most 32 developed rectangular cuts");for(const auto& cut:cuts){fields(cut,{"offset","width","from","to"});positive(cut.at("width"));if(scalar(cut.at("offset"),parameters)<0||scalar(cut.at("from"),parameters)<0||scalar(cut.at("to"),parameters)-scalar(cut.at("from"),parameters)<1e-5)throw Error("invalid_model","Cut ranges must be nonnegative with length at least 0.00001 mm");}}
        }catch(const Error& e){auto details=e.details;details["flange_id"]=flange_id;throw Error(e.code,e.what(),details);}
      }
    } else if(type=="sheet_unfold") {
      fields(feature,{"id","type","input"});dependency("input");
      if(types.at(text_field(feature,"input"))!="sheet_metal")throw Error("invalid_model","Unfold requires preserved sheet_metal intent; edited or imported arbitrary solids cannot be unfolded");
    } else if(type=="shell"||type=="offset"||type=="thicken") {
      const auto dimension=type=="offset"?"distance":"thickness";
      if(type=="shell")fields(feature,{"id","type","input",dimension,"faces"},{"join"});
      else if(type=="offset")fields(feature,{"id","type","input",dimension},{"join"});
      else fields(feature,{"id","type","input",dimension},{"faces","join"});
      if(std::abs(scalar(feature.at(dimension),parameters))<1e-5)throw Error("invalid_model","Shell, offset and thicken dimensions must have magnitude at least 0.00001 mm");
      if(feature.contains("join")&&feature.at("join")!="arc"&&feature.at("join")!="intersection")throw Error("invalid_model","Offset join must be arc or intersection");
      const auto input=text_field(feature,"input");
      if(type=="thicken"&&prior.contains(input)&&is_sketch_feature_type(types.at(input))) {
        if(feature.contains("faces"))throw Error("invalid_model","Thickening a sketch uses its complete profile; omit faces");
      } else if(type=="thicken"&&prior.contains(input)&&is_surface_feature_type(types.at(input))) {
        // Surface patches and shells are explicit open geometry inputs. The kernel
        // validates a single connected patch before thickening the selected faces.
      } else {
        dependency("input");
        if(type=="thicken"&&!feature.contains("faces"))throw Error("invalid_model","Thickening solid surfaces requires explicit faces");
      }
      if(feature.contains("faces")&&!(type=="shell"&&feature.at("faces").is_array()&&feature.at("faces").empty()))validate_face_selection(feature.at("faces"),parameters,input);
    } else if(type=="sketch_hull") {
      fields(feature,{"id","type","inputs","workplane"},{"contact_tolerance"});const auto tolerance=scalar(feature.value("contact_tolerance",Json(1e-5)),parameters);if(tolerance<1e-7||tolerance>1e-2)throw Error("invalid_model","Hull contact_tolerance must lie between 1e-7 and 0.01 mm");workplane(feature.at("workplane"),parameters);const auto& inputs=feature.at("inputs");if(!inputs.is_array()||inputs.empty()||inputs.size()>16)throw Error("invalid_model","Sketch hull requires 1–16 earlier sketches or curves");std::set<std::string> seen;for(const auto& value:inputs){if(!value.is_string())throw Error("invalid_model","Hull inputs must be feature names");const auto input=value.get<std::string>();if(!seen.insert(input).second||!prior.contains(input)||(!is_sketch_feature_type(types.at(input))&&!is_curve_feature_type(types.at(input))))throw Error("invalid_model","Hull inputs must name distinct earlier sketches or curves");}
    } else if(type=="sketch_trace") {
      fields(feature,{"id","type","input","workplane","width"});workplane(feature.at("workplane"),parameters);positive(feature.at("width"));const auto input=text_field(feature,"input");if(!prior.contains(input)||!is_curve_feature_type(types.at(input)))throw Error("invalid_model","Trace requires an earlier exact curve");
    } else if(type=="sketch_full_round") {
      fields(feature,{"id","type","input","edges"},{"invert"});if(feature.contains("invert")&&!feature.at("invert").is_boolean())throw Error("invalid_model","Full-round invert must be boolean");const auto input=text_field(feature,"input");sketch_dependency(input);validate_selector(feature.at("edges"),parameters,input);if(feature.at("edges").at("expected_count")!=1||feature.at("edges").at("curve_kind")!="line")throw Error("invalid_model","Full round selects exactly one straight outer edge");
    } else if(type=="text_on_path") {
      fields(feature,{"id","type","input","path"},{"start","offset","reverse"});const auto input=text_field(feature,"input"),path=text_field(feature,"path");sketch_dependency(input);
      bool captured=false;for(const auto& candidate:features)if(candidate.at("id")==input)captured=candidate.at("type")=="sketch"&&candidate.at("profile").at("type")=="text";
      if(!captured)throw Error("invalid_model","Text-on-path input must be an earlier captured text sketch");if(!prior.contains(path)||!is_curve_feature_type(types.at(path)))throw Error("invalid_model","Text-on-path requires an earlier exact curve");
      if(scalar(feature.value("start",Json(0)),parameters)<0)throw Error("invalid_model","Text path start must be nonnegative");scalar(feature.value("offset",Json(0)),parameters);if(feature.contains("reverse")&&!feature.at("reverse").is_boolean())throw Error("invalid_model","Text path reverse must be boolean");
    } else if (type == "sketch") {
      fields(feature, {"id", "type", "workplane", "profile"});
      workplane(feature.at("workplane"), parameters);
      const auto& profile = feature.at("profile");
      const auto kind = text_field(profile, "type");
      if (kind == "rectangle") {
        fields(profile, {"type", "width", "height"});
        positive(profile.at("width")); positive(profile.at("height"));
      } else if (kind == "circle") {
        fields(profile, {"type", "radius"}); positive(profile.at("radius"));
      } else if (kind == "polygon") {
        fields(profile, {"type", "points"});
        const auto& points = profile.at("points");
        if (!points.is_array() || points.size() < 3 || points.size() > 128)
          throw Error("invalid_model", "A polygon needs 3 to 128 points");
        for (const auto& point : points) {
          if (!point.is_array() || point.size() != 2) throw Error("invalid_model", "Polygon points require two coordinates");
          scalar(point[0], parameters); scalar(point[1], parameters);
        }
      } else if (kind=="wire") {
        fields(profile,{"type","segments"},{"holes"});
        curve_segments(profile.at("segments"),parameters,2);
        if (profile.contains("holes")) {
          const auto& holes=profile.at("holes");
          if (!holes.is_array() || holes.size()>16) throw Error("invalid_model","A profile permits at most 16 interior boundaries");
          for (std::size_t i=0;i<holes.size();++i) {
            try {curve_segments(holes[i],parameters,2);}
            catch (const Error& e) {auto details=e.details;details["hole_index"]=i;throw Error(e.code,e.what(),details);}
          }
        }
      } else if(kind=="text"||kind=="svg"||kind=="dxf")validate_authoring_profile(profile,parameters);
      else throw Error("invalid_model", "Unsupported sketch profile");
    } else if (type=="sketch_cut" || type=="sketch_fuse" || type=="sketch_intersection") {
      fields(feature,{"id","type","left","right"});
      sketch_dependency(text_field(feature,"left")); sketch_dependency(text_field(feature,"right"));
    } else if (type=="sketch_offset") {
      fields(feature,{"id","type","input","distance"},{"join"});sketch_dependency(text_field(feature,"input"));
      if(std::abs(scalar(feature.at("distance"),parameters))<1e-5)throw Error("invalid_model","Sketch offset magnitude must be at least 0.00001 mm");
      if(feature.contains("join")&&text_field(feature,"join")!="arc"&&text_field(feature,"join")!="intersection")throw Error("invalid_model","Sketch offset join must be arc or intersection");
    } else if (type=="sketch_fillet" || type=="sketch_chamfer") {
      const auto dimension=type=="sketch_fillet"?"radius":"distance";
      fields(feature,{"id","type","input",dimension,"vertices"});sketch_dependency(text_field(feature,"input"));positive(feature.at(dimension));
      const auto& vertices=feature.at("vertices");
      if(vertices.is_string()) {if(vertices!="all")throw Error("invalid_model","Sketch vertices must be all or a geometric point selector");}
      else {
        fields(vertices,{"type","feature_id","point","tolerance","expected_count"});
        if(text_field(vertices,"type")!="geometric"||text_field(vertices,"feature_id")!=text_field(feature,"input"))throw Error("invalid_model","Sketch vertex selector must qualify its input feature");
        vector3(vertices.at("point"),parameters);if(scalar(vertices.at("tolerance"),parameters)<=0)throw Error("invalid_model","Sketch vertex tolerance must be positive");
        if(!vertices.at("expected_count").is_number_integer()||vertices.at("expected_count")<1||vertices.at("expected_count")>10000)throw Error("invalid_model","Sketch vertex expected_count must be between 1 and 10000");
      }
    } else if (type=="sketch_transform" || type=="sketch_instance") {
      fields(feature,{"id","type","input"},{"translation","rotation"});sketch_dependency(text_field(feature,"input"));
      if(feature.contains("translation"))vector3(feature.at("translation"),parameters);
      if(feature.contains("rotation")) {
        const auto& rotation=feature.at("rotation");fields(rotation,{"origin","axis","angle_deg"});
        vector3(rotation.at("origin"),parameters);unit_vector(rotation.at("axis"),parameters);scalar(rotation.at("angle_deg"),parameters,"deg");
      }
    } else if (type=="mirror" || type=="sketch_mirror") {
      fields(feature,{"id","type","input","plane"});
      if(type=="sketch_mirror")sketch_dependency(text_field(feature,"input"));else dependency("input");
      workplane(feature.at("plane"),parameters);
    } else if (type=="split") {
      fields(feature,{"id","type","input","plane","keep"});dependency("input");workplane(feature.at("plane"),parameters);
      const auto keep=text_field(feature,"keep");if(keep!="both"&&keep!="top"&&keep!="bottom")throw Error("invalid_model","Split keep must be both, top or bottom");
    } else if (type=="sketch_face" || type=="sketch_projection") {
      if(type=="sketch_projection")fields(feature,{"id","type","input","faces","workplane"});else fields(feature,{"id","type","input","faces"});
      dependency("input");validate_face_selection(feature.at("faces"),parameters,text_field(feature,"input"));
      if(type=="sketch_projection")workplane(feature.at("workplane"),parameters);
    } else if (type == "extrude") {
      fields(feature, {"id", "type", "input"}, {"distance","direction","both","taper_deg","until","target"});
      sketch_dependency(text_field(feature,"input"));
      if(feature.contains("direction"))unit_vector(feature.at("direction"),parameters);
      if(feature.contains("until") || feature.contains("target")) {
        if(!feature.contains("until")||!feature.contains("target")||feature.contains("distance")||feature.contains("both")||feature.contains("taper_deg"))
          throw Error("invalid_model","Target extrusion requires until and target, without distance, both or taper_deg");
        const auto until=text_field(feature,"until");
        if(until!="first"&&until!="last")throw Error("invalid_model","Extrusion until must be first or last");
        dependency("target");
      } else {
        if(!feature.contains("distance")||std::abs(scalar(feature.at("distance"),parameters))<1e-5)
          throw Error("invalid_model", "Extrusion distance magnitude must be at least 0.00001 mm");
        if(feature.contains("both")&&!feature.at("both").is_boolean())throw Error("invalid_model","Extrusion both must be boolean");
        if(feature.contains("taper_deg")&&std::abs(scalar(feature.at("taper_deg"),parameters,"deg"))>=89)
          throw Error("invalid_model","Extrusion taper angle magnitude must be less than 89 degrees");
      }
    } else if (type == "revolve") {
      fields(feature, {"id", "type", "input", "axis", "angle_deg"});
      sketch_dependency(text_field(feature,"input")); axis(feature.at("axis"), parameters, "direction");
      const auto angle = scalar(feature.at("angle_deg"),parameters,"deg");
      if (angle <= 0 || angle > 360) throw Error("invalid_model", "Revolve angle must be greater than zero and at most 360 degrees");
    } else if (type == "loft") {
      fields(feature, {"id", "type", "sections"}, {"ruled","start_vertex","end_vertex","hole_order"});
      const auto& sections = feature.at("sections");
      if (!sections.is_array() || sections.empty() || sections.size() > 32 || (sections.size()==1&&!feature.contains("start_vertex")&&!feature.contains("end_vertex")))
        throw Error("invalid_model", "A loft needs 1 to 32 sketches and at least two sections including optional endpoint vertices");
      for(const auto* key:{"start_vertex","end_vertex"})if(feature.contains(key))vector3(feature.at(key),parameters);
      if(feature.contains("hole_order")) {
        const auto& order=feature.at("hole_order");if(!order.is_array()||order.size()!=sections.size())throw Error("invalid_model","Loft hole_order requires one landmark list per sketch section");
        for(const auto& row:order) {if(!row.is_array()||row.empty()||row.size()>16)throw Error("invalid_model","Loft hole_order requires 1–16 hole landmarks per section");
          for(const auto& landmark:row) {fields(landmark,{"point","tolerance"});vector3(landmark.at("point"),parameters);if(scalar(landmark.at("tolerance"),parameters)<=0)throw Error("invalid_model","Hole landmark tolerance must be positive");}}
      }
      for (const auto& section : sections) {
        if (!section.is_string()) throw Error("invalid_model", "Loft section references must be strings");
        sketch_dependency(section.get<std::string>());
      }
      if (feature.contains("ruled") && !feature.at("ruled").is_boolean()) throw Error("invalid_model", "ruled must be boolean");
    } else if (type == "sweep") {
      fields(feature, {"id", "type", "path"}, {"input","sections","orientation","binormal","guide","transition"});
      if(feature.contains("input")==feature.contains("sections"))throw Error("invalid_model","Sweep requires exactly one of input or sections");
      if(feature.contains("input"))sketch_dependency(text_field(feature,"input"));
      else {
        const auto& sections=feature.at("sections");
        if(!sections.is_array()||sections.size()<2||sections.size()>32)throw Error("invalid_model","Sweep permits 2–32 section sketches");
        for(const auto& section:sections) {if(!section.is_string())throw Error("invalid_model","Sweep sections must name sketch features");sketch_dependency(section.get<std::string>());}
      }
      const auto& path = feature.at("path");
      if (path.is_object()) {
        fields(path,{"type","segments"});
        if (text_field(path,"type")!="wire") throw Error("invalid_model","A curved sweep path must have type wire");
        curve_segments(path.at("segments"),parameters,3);
      } else {
        if (!path.is_array() || path.size() < 2 || path.size() > 64) throw Error("invalid_model", "A sweep path needs 2 to 64 points");
        for (const auto& p : path) vector3(p,parameters);
      }
      if(static_cast<int>(feature.contains("orientation"))+static_cast<int>(feature.contains("binormal"))+static_cast<int>(feature.contains("guide"))>1)
        throw Error("invalid_model","Sweep orientation, binormal and guide are mutually exclusive");
      if(feature.contains("orientation")) {const auto v=text_field(feature,"orientation");if(v!="corrected_frenet"&&v!="frenet"&&v!="fixed")throw Error("invalid_model","Unsupported sweep orientation");}
      if(feature.contains("binormal"))unit_vector(feature.at("binormal"),parameters);
      if(feature.contains("guide")) {fields(feature.at("guide"),{"type","segments"});if(feature.at("guide").at("type")!="wire")throw Error("invalid_model","Sweep guide must be an exact wire");curve_segments(feature.at("guide").at("segments"),parameters,3);}
      if(feature.contains("transition")) {const auto v=text_field(feature,"transition");if(v!="transformed"&&v!="right_corner"&&v!="round_corner")throw Error("invalid_model","Unsupported sweep transition");}
    } else if(type=="scale") {
      fields(feature,{"id","type","input","origin","factors"});dependency("input");vector3(feature.at("origin"),parameters);
      const auto& factors=feature.at("factors");
      const auto check_factor=[&](const Json& value){const double n=scalar(value,parameters,"dimensionless");if(n<1e-6||n>1e6)throw Error("invalid_model","Scale factors must be positive from 0.000001 through 1000000; use mirror for reflection");};
      if(factors.is_array()) {if(factors.size()!=3)throw Error("invalid_model","Nonuniform scale needs three factors");for(const auto& factor:factors)check_factor(factor);}
      else check_factor(factors);
    } else if(type=="draft") {
      fields(feature,{"id","type","input","faces","angle_deg","direction","neutral_plane"});dependency("input");
      validate_face_selection(feature.at("faces"),parameters,text_field(feature,"input"));unit_vector(feature.at("direction"),parameters);workplane(feature.at("neutral_plane"),parameters);
      const double angle=scalar(feature.at("angle_deg"),parameters,"deg");if(std::abs(angle)<1e-5||std::abs(angle)>=89)throw Error("invalid_model","Draft angle magnitude must be at least 0.00001 and less than 89 degrees");
    } else if(type=="twist_extrude") {
      fields(feature,{"id","type","input","distance","angle_deg"},{"center"});sketch_dependency(text_field(feature,"input"));
      if(std::abs(scalar(feature.at("distance"),parameters))<1e-5)throw Error("invalid_model","Twist extrusion distance magnitude must be at least 0.00001 mm");
      if(std::abs(scalar(feature.at("angle_deg"),parameters,"deg"))>5760)throw Error("invalid_model","Twist extrusion is bounded to 16 turns");
      if(feature.contains("center"))vector3(feature.at("center"),parameters);
    } else if (type == "transform" || type == "instance") {
      fields(feature, {"id", "type", "input"}, {"translation", "rotation"}); dependency("input");
      if (is_sketch_feature_type(types.at(text_field(feature,"input")))) throw Error("invalid_model", "Transform and instance currently require solid inputs");
      if (feature.contains("translation")) vector3(feature.at("translation"), parameters);
      if (feature.contains("rotation")) {
        const auto& rotation = feature.at("rotation");
        fields(rotation, {"origin", "axis", "angle_deg"});
        vector3(rotation.at("origin"),parameters); unit_vector(rotation.at("axis"),parameters);
        scalar(rotation.at("angle_deg"),parameters,"deg");
      }
    } else if (type == "pattern" || type == "circular_pattern") {
      if (type=="pattern") fields(feature, {"id", "type", "input", "count", "step"});
      else fields(feature,{"id","type","input","count","axis","angle_deg"});
      dependency("input");
      if (is_sketch_feature_type(types.at(text_field(feature,"input")))) throw Error("invalid_model", "Patterns currently require solid inputs");
      const auto copies=pattern_count(feature.at("count"),parameters);
      if (type=="pattern") {
        const auto step = vector3(feature.at("step"),parameters);
        if (std::hypot(step[0],step[1],step[2]) < 1e-5) throw Error("invalid_model", "Pattern step must be nonzero");
      } else {
        axis(feature.at("axis"),parameters,"direction");
        const auto angle=scalar(feature.at("angle_deg"),parameters,"deg");
        if (std::abs(angle)<1e-5 || std::abs(angle)>=360 || std::abs(angle)*(copies-1)>=360-1e-9)
          throw Error("invalid_model","Circular pattern angle is the signed step; instances must span less than 360 degrees");
      }
    } else if (type == "hole") {
      fields(feature, {"id", "type", "input", "origin", "axis", "radius", "depth"}); dependency("input");
      vector3(feature.at("origin"),parameters); unit_vector(feature.at("axis"),parameters);
      positive(feature.at("radius")); positive(feature.at("depth"));
    } else if (type == "assembly") {
      assembly(feature,parameters,types);
      assembly_parts += feature.at("parts").size();
      if (assembly_parts > 256) throw Error("limit_exceeded", "A model permits at most 256 assembly parts across all assembly features");
      std::size_t leaves=0,depth=1;
      for (const auto& part:feature.at("parts")) {
        const auto input=text_field(part,"input");
        leaves+=leaf_counts.contains(input)?leaf_counts.at(input):1;
        depth=std::max(depth,depths.contains(input)?depths.at(input)+1:std::size_t(1));
      }
      if (leaves>assembly_leaf_limit || depth>assembly_depth_limit)
        throw Error("limit_exceeded","An assembly permits at most 1024 leaf occurrences and eight levels",{{"leaf_count",leaves},{"depth",depth}});
      expanded_parts+=leaves;
      if (expanded_parts>4096) throw Error("limit_exceeded","A document permits at most 4096 expanded assembly leaf occurrences");
      leaf_counts[id]=leaves;depths[id]=depth;
    } else if (type == "import_step"||type=="import_step_surface") {
      if(type=="import_step_surface")fields(feature,{"id","type","content","sha256"});else fields(feature, {"id", "type", "content", "sha256"},{"purchase","solid_indices"});
      if(feature.contains("solid_indices")){validate_step_solid_indices(feature.at("solid_indices"));if(feature.contains("purchase"))throw Error("invalid_model","Subset imports cannot claim an unchanged purchased artifact");}
      const auto content = text_field(feature,"content");
      const auto digest = text_field(feature,"sha256");
      if (content.empty()) throw Error("limit_exceeded", "Embedded STEP content must not be empty");
      if (digest.size() != 64 || digest.find_first_not_of("0123456789abcdef") != std::string::npos || sha256(content) != digest)
        throw Error("invalid_model", "Embedded STEP content does not match its SHA-256 identity");
      if(feature.contains("purchase")) {
        validate_purchase(feature.at("purchase"),true);
        if(feature.at("purchase").at("artifact_sha256")!=digest)
          throw Error("invalid_model","Purchased artifact identity differs from the embedded STEP bytes");
      }
    } else throw Error("unsupported_feature", "Unsupported feature type: " + type, {{"feature_id", id}});
    if (feature.contains("origin")) vector3(feature.at("origin"), parameters);
    prior.insert(id);
    types.emplace(id,type);
    } catch (const Error& e) {
      auto details=e.details; details["feature_id"]=id;
      throw Error(e.code,e.what(),details);
    }
  }
  if (!prior.contains(text_field(model, "output"))) throw Error("invalid_model", "output must name a feature");
  if (is_sketch_feature_type(types.at(text_field(model,"output")))) throw Error("invalid_model", "The model output must be solid geometry, not an intermediate sketch");
  for(const auto& feature:model.at("features"))if(feature.at("type")=="assembly"&&feature.contains("bom"))
    for(const auto& item:feature.at("bom"))if(item.contains("purchase")) {
      const auto* source=imported_step_source(model,text_field(item,"input"));
      if(source&&source->contains("purchase")&&item.at("purchase")!=source->at("purchase"))
        throw Error("invalid_model","BOM purchasing metadata conflicts with the verified imported source",{{"feature_id",feature.at("id")},{"source_feature_id",item.at("input")}});
    }
  validate_components(model);
}

Json assembly_structure(const Json& model,const std::string& assembly_id) {
  validate_model(model);
  const auto selected=assembly_id.empty()?text_field(model,"output"):assembly_id;
  std::map<std::string,const Json*> features;
  for (const auto& feature:model.at("features")) features.emplace(text_field(feature,"id"),&feature);
  if (!features.contains(selected) || features.at(selected)->at("type")!="assembly")
    throw Error("invalid_argument","Feature must name an assembly",{{"feature_id",selected}});
  Json result=Json::array();
  std::function<void(const std::string&,const std::string&)> visit=[&](const std::string& source,const std::string& parent) {
    for (const auto& part:features.at(source)->at("parts")) {
      const auto local=text_field(part,"id"),id=parent.empty()?local:parent+"/"+local,input=text_field(part,"input");
      const bool nested=features.at(input)->at("type")=="assembly";
      result.push_back({{"id",id},{"input",input},{"assembly_id",source},{"parent_id",parent},{"kind",nested?"assembly":"part"}});
      if (nested) visit(input,id);
    }
  };
  visit(selected,"");return result;
}

namespace {
// A batch may reference what an earlier operation in it added, before the
// final validate_model. Lookups therefore tolerate malformed entries, and an
// added feature must carry its string id when it enters the candidate.
bool named(const Json& item, const char* key, const std::string& value) {
  return item.is_object() && item.contains(key) && item.at(key).is_string() && item.at(key).get_ref<const std::string&>() == value;
}
void added_feature(const Json& feature) {
  if (!feature.is_object() || !feature.contains("id") || !feature.at("id").is_string())
    throw Error("invalid_model", "An added feature must be an object with a string id");
}
Json& member_array(Json& feature, const char* key, const std::string& assembly_id, bool create) {
  if (!feature.contains(key)) {
    if (!create) throw Error("invalid_model", std::string("Assembly has no ") + key, {{"feature_id", assembly_id}});
    feature[key] = Json::array();
  }
  auto& result = feature.at(key);
  if (!result.is_array()) throw Error("invalid_model", std::string("Assembly ") + key + " must be an array", {{"feature_id", assembly_id}});
  return result;
}
Json& find_assembly(Json& features,const std::string& id) {
  for (auto& feature:features) if (named(feature,"id",id) && named(feature,"type","assembly")) return feature;
  throw Error("invalid_argument","assembly_id must name an assembly feature",{{"feature_id",id}});
}
void set_joint_coordinate(Json& feature,const Json& edit) {
  const auto id=text_field(edit,"mate_id"),coordinate=text_field(edit,"coordinate");
  auto& mates=member_array(feature,"mates",text_field(feature,"id"),false);
  for (auto& mate:mates) if (named(mate,"id",id)) {
    const auto type=text_field(mate,"type");
    if (!((coordinate=="angle_deg" && (type=="revolute" || type=="cylindrical")) ||
          (coordinate=="travel_mm" && (type=="slider" || type=="cylindrical"))))
      throw Error("invalid_argument","Coordinate does not belong to this moving mate",{{"mate_id",id},{"coordinate",coordinate}});
    mate[coordinate]=edit.at("value");return;
  }
  throw Error("invalid_argument","Unknown moving mate: "+id,{{"mate_id",id}});
}
}

Json apply_operations(const Json& model, const Json& operations, const ComponentResolver& resolve) {
  if (!operations.is_array() || operations.empty() || operations.size() > 256)
    throw Error("invalid_argument", "operations must contain 1–256 edits");
  auto candidate = model;
  for (std::size_t index = 0; index < operations.size(); ++index) {
   const auto& operation = operations[index];
   try {
    const auto op = text_field(operation, "op");
    auto& features = candidate.at("features");
    if (apply_component_operation(candidate,operation,resolve)) {
      // Captured dependencies remain ordinary editable local features.
    } else if (op == "set_parameter") {
      fields(operation, {"op", "name", "value"});
      const auto name = text_field(operation, "name");
      model_identifier(name); number(operation.at("value"));
      candidate["parameters"][name] = operation.at("value");
    } else if (op == "add_feature") {
      fields(operation, {"op", "feature"});
      added_feature(operation.at("feature"));
      features.push_back(operation.at("feature"));
    } else if (op == "replace_feature" || op == "remove_feature") {
      if (op == "replace_feature") fields(operation, {"op", "id", "feature"});
      else fields(operation, {"op", "id"});
      const auto id = text_field(operation, "id");
      auto found = features.end();
      for (auto it = features.begin(); it != features.end(); ++it)
        if (named(*it, "id", id)) { found = it; break; }
      if (found == features.end()) throw Error("invalid_argument", "Unknown feature: " + id);
      if (op == "remove_feature") features.erase(found);
      else {
        if (text_field(operation.at("feature"), "id") != id)
          throw Error("invalid_argument", "Replacement must preserve the feature id");
        *found = operation.at("feature");
      }
    } else if (op == "set_output") {
      fields(operation, {"op", "feature_id"});
      candidate["output"] = text_field(operation, "feature_id");
    } else if (op=="set_joint_value" || op=="apply_pose" || op=="set_pose" || op=="remove_pose" || op=="set_coupling" || op=="remove_coupling") {
      if (op=="set_joint_value") fields(operation,{"op","assembly_id","mate_id","coordinate","value"});
      else if (op=="apply_pose" || op=="remove_pose") fields(operation,{"op","assembly_id","pose_id"});
      else if (op=="set_pose") fields(operation,{"op","assembly_id","pose"});
      else if (op=="set_coupling") fields(operation,{"op","assembly_id","coupling"});
      else fields(operation,{"op","assembly_id","coupling_id"});
      const auto assembly_id=text_field(operation,"assembly_id");auto& feature=find_assembly(features,assembly_id);
      if (op=="set_joint_value") set_joint_coordinate(feature,operation);
      else if (op=="apply_pose") {
        const auto id=text_field(operation,"pose_id");auto& poses=member_array(feature,"poses",assembly_id,false);
        const Json* selected=nullptr;
        for (const auto& pose:poses) if (named(pose,"id",id)) {selected=&pose;break;}
        if (!selected) throw Error("invalid_argument","Unknown named pose: "+id,{{"feature_id",assembly_id},{"pose_id",id}});
        if (!selected->contains("values") || !selected->at("values").is_array()) throw Error("invalid_model","Pose values must be an array");
        for (const auto& value:selected->at("values")) set_joint_coordinate(feature,value);
      } else {
        const bool pose=op=="set_pose" || op=="remove_pose",remove=op=="remove_pose" || op=="remove_coupling";
        const auto key=pose?"pose":"coupling",id_key=pose?"pose_id":"coupling_id",array_key=pose?"poses":"couplings";
        const auto id=remove?text_field(operation,id_key):text_field(operation.at(key),"id");
        auto& items=member_array(feature,array_key,assembly_id,true);auto found=items.end();
        for (auto it=items.begin();it!=items.end();++it) if (named(*it,"id",id)) {found=it;break;}
        if (remove) {
          if (found==items.end()) throw Error("invalid_argument",std::string("Unknown ")+key+": "+id,{{"feature_id",assembly_id},{id_key,id}});
          items.erase(found);
        } else if (found==items.end()) items.push_back(operation.at(key));
        else *found=operation.at(key);
      }
    } else if (op == "set_part_placement" || op == "set_mate" || op == "remove_mate" || op == "set_bom_item" || op == "remove_bom_item") {
      if (op == "set_part_placement") fields(operation, {"op","assembly_id","part_id","placement"});
      else if (op == "set_mate") fields(operation, {"op","assembly_id","mate"});
      else if (op == "remove_mate") fields(operation, {"op","assembly_id","mate_id"});
      else if (op == "set_bom_item") fields(operation, {"op","assembly_id","item"});
      else fields(operation, {"op","assembly_id","input"});
      const auto assembly_id=text_field(operation,"assembly_id");
      auto found=features.end();
      for (auto it=features.begin(); it!=features.end(); ++it)
        if (named(*it, "id", assembly_id)) { found=it; break; }
      if (found == features.end() || !named(*found,"type","assembly"))
        throw Error("invalid_argument", "assembly_id must name an assembly feature", {{"feature_id",assembly_id}});
      if (op == "set_part_placement") {
        const auto part_id=text_field(operation,"part_id");
        auto& parts=member_array(*found,"parts",assembly_id,false);
        auto part=parts.end();
        for (auto it=parts.begin(); it!=parts.end(); ++it)
          if (named(*it, "id", part_id)) { part=it; break; }
        if (part == parts.end()) throw Error("invalid_argument", "Unknown assembly part: " + part_id, {{"feature_id",assembly_id},{"part_id",part_id}});
        (*part)["placement"]=operation.at("placement");
      } else if (op == "set_bom_item" || op == "remove_bom_item") {
        auto& bom=member_array(*found,"bom",assembly_id,true);
        const auto input=op == "set_bom_item" ? text_field(operation.at("item"),"input") : text_field(operation,"input");
        auto item=bom.end();
        for (auto it=bom.begin();it!=bom.end();++it) if (named(*it,"input",input)) {item=it;break;}
        if (op == "remove_bom_item") {
          if (item==bom.end()) throw Error("invalid_argument","Unknown assembly BOM input: "+input,{{"feature_id",assembly_id},{"source_feature_id",input}});
          bom.erase(item);
        } else if (item==bom.end()) bom.push_back(operation.at("item"));
        else *item=operation.at("item");
      } else {
        auto& mates=member_array(*found,"mates",assembly_id,true);
        const auto mate_id=op == "set_mate" ? text_field(operation.at("mate"),"id") : text_field(operation,"mate_id");
        auto mate=mates.end();
        for (auto it=mates.begin(); it!=mates.end(); ++it)
          if (named(*it, "id", mate_id)) { mate=it; break; }
        if (op == "remove_mate") {
          if (mate == mates.end()) throw Error("invalid_argument", "Unknown assembly mate: " + mate_id, {{"feature_id",assembly_id},{"mate_id",mate_id}});
          mates.erase(mate);
        } else if (mate == mates.end()) mates.push_back(operation.at("mate"));
        else *mate=operation.at("mate");
      }
    } else throw Error("invalid_argument", "Unknown edit operation: " + op);
   } catch (const Error& e) {
    auto details = e.details; details["operation_index"] = index;
    throw Error(e.code, e.what(), details);
   } catch (const Json::exception& e) {
    throw Error("invalid_model", std::string("Malformed operation: ") + e.what(), {{"operation_index", index}});
   }
  }
  validate_model(candidate);
  return candidate;
}
}
