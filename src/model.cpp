#include "agentcad/model.hpp"
#include "agentcad/hash.hpp"
#include <set>
#include <map>
#include <cmath>
#include <numbers>

namespace agentcad {
Json model_definitions() {
  const Json id = {{"type", "string"}, {"pattern", "^[A-Za-z][A-Za-z0-9_-]{0,63}$"}};
  const Json numeric = {{"type", "number"}, {"minimum", -1e6}, {"maximum", 1e6}};
  auto object = [](Json properties, Json required) {
    return Json{{"type", "object"}, {"properties", properties}, {"required", required}, {"additionalProperties", false}};
  };
  const Json scalar_ref = {{"$ref", "#/$defs/scalar"}};
  const Json vector_ref = {{"$ref", "#/$defs/vector3"}};
  const Json feature_ref = {{"$ref", "#/$defs/feature"}};
  const Json selector = object({
    {"type", {{"const", "geometric"}}}, {"feature_id", id},
    {"curve_kind", {{"enum", {"line", "circle", "ellipse", "hyperbola", "parabola", "bezier", "bspline", "offset", "other"}}}},
    {"expected_count", {{"type", "integer"}, {"minimum", 1}, {"maximum", 10000}}},
    {"direction", object({{"vector", vector_ref}, {"tolerance", scalar_ref}}, {"vector", "tolerance"})},
    {"center", object({{"point", vector_ref}, {"tolerance", scalar_ref}}, {"point", "tolerance"})},
    {"length", object({{"value", scalar_ref}, {"tolerance", scalar_ref}}, {"value", "tolerance"})}
  }, {"type", "feature_id", "curve_kind", "expected_count"});
  Json features = Json::array({
    object({{"id", id}, {"type", {{"const", "box"}}}, {"size", vector_ref}, {"origin", vector_ref}}, {"id", "type", "size"}),
    object({{"id", id}, {"type", {{"const", "cylinder"}}}, {"radius", scalar_ref}, {"height", scalar_ref}, {"origin", vector_ref}}, {"id", "type", "radius", "height"}),
    object({{"id",id},{"type",{{"const","external_thread"}}},{"major_diameter",scalar_ref},{"pitch",scalar_ref},{"length",scalar_ref},{"origin",vector_ref},{"handedness",{{"enum",{"right","left"}}}}}, {"id","type","major_diameter","pitch","length"}),
    object({{"id", id}, {"type", {{"enum", {"cut", "fuse"}}}}, {"left", id}, {"right", id}}, {"id", "type", "left", "right"}),
    object({{"id", id}, {"type", {{"const", "fillet"}}}, {"input", id}, {"radius", scalar_ref}, {"edges", {{"oneOf", Json::array({Json{{"const", "all"}}, Json{{"$ref", "#/$defs/selector"}}})}}}}, {"id", "type", "input", "radius", "edges"})
  });
  const Json workplane_schema = object({{"origin",vector_ref},{"normal",vector_ref},{"x_direction",vector_ref}}, {"origin","normal","x_direction"});
  const Json profile_schema = {{"oneOf", Json::array({
    object({{"type",{{"const","rectangle"}}},{"width",scalar_ref},{"height",scalar_ref}}, {"type","width","height"}),
    object({{"type",{{"const","circle"}}},{"radius",scalar_ref}}, {"type","radius"}),
    object({{"type",{{"const","polygon"}}},{"points",{{"type","array"},{"minItems",3},{"maxItems",128},{"items",{{"type","array"},{"items",scalar_ref},{"minItems",2},{"maxItems",2}}}}}}, {"type","points"})
  })}};
  const Json axis_schema = object({{"origin",vector_ref},{"direction",vector_ref}}, {"origin","direction"});
  const Json rotation_schema = object({{"origin",vector_ref},{"axis",vector_ref},{"angle_deg",scalar_ref}}, {"origin","axis","angle_deg"});
  const Json placement_schema = object({{"translation",vector_ref},{"rotation",rotation_schema}}, Json::array());
  const auto bom_text=[](int maximum) {return Json{{"type","string"},{"maxLength",maximum},{"not",{{"pattern","[^ -~]"}}}};};
  const Json bom_item_schema=object({{"input",id},{"item_number",{{"type","integer"},{"minimum",1},{"maximum",999}}},
    {"part_number",bom_text(64)},{"description",bom_text(120)},{"material",bom_text(64)}},{"input"});
  const Json mate_schema = object({{"id",id},{"type",{{"const","rigid"}}},{"parent",id},{"child",id},
    {"parent_frame",workplane_schema},{"child_frame",workplane_schema},{"offset",vector_ref},{"angle_deg",scalar_ref}},
    {"id","type","parent","child","parent_frame","child_frame"});
  const Json part_schema = object({{"id",id},{"input",id},{"placement",placement_schema}}, {"id","input"});
  features.push_back(object({{"id",id},{"type",{{"const","sketch"}}},{"workplane",workplane_schema},{"profile",profile_schema}}, {"id","type","workplane","profile"}));
  features.push_back(object({{"id",id},{"type",{{"const","extrude"}}},{"input",id},{"distance",scalar_ref}}, {"id","type","input","distance"}));
  features.push_back(object({{"id",id},{"type",{{"const","revolve"}}},{"input",id},{"axis",axis_schema},{"angle_deg",scalar_ref}}, {"id","type","input","axis","angle_deg"}));
  features.push_back(object({{"id",id},{"type",{{"const","loft"}}},{"sections",{{"type","array"},{"items",id},{"minItems",2},{"maxItems",32}}},{"ruled",{{"type","boolean"}}}}, {"id","type","sections"}));
  features.push_back(object({{"id",id},{"type",{{"const","sweep"}}},{"input",id},{"path",{{"type","array"},{"items",vector_ref},{"minItems",2},{"maxItems",64}}}}, {"id","type","input","path"}));
  features.push_back(object({{"id",id},{"type",{{"enum",{"transform","instance"}}}},{"input",id},{"translation",vector_ref},{"rotation",rotation_schema}}, {"id","type","input"}));
  features.push_back(object({{"id",id},{"type",{{"const","pattern"}}},{"input",id},{"count",{{"type","integer"},{"minimum",2},{"maximum",64}}},{"step",vector_ref}}, {"id","type","input","count","step"}));
  features.push_back(object({{"id",id},{"type",{{"const","hole"}}},{"input",id},{"origin",vector_ref},{"axis",vector_ref},{"radius",scalar_ref},{"depth",scalar_ref}}, {"id","type","input","origin","axis","radius","depth"}));
  features.push_back(object({{"id",id},{"type",{{"const","import_step"}}},{"content",{{"type","string"},{"minLength",1},{"maxLength",524288}}},{"sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}}}, {"id","type","content","sha256"}));
  features.push_back(object({{"id",id},{"type",{{"const","assembly"}}},
    {"parts",{{"type","array"},{"items",part_schema},{"minItems",1},{"maxItems",64}}},
    {"mates",{{"type","array"},{"items",mate_schema},{"maxItems",63}}},
    {"bom",{{"type","array"},{"items",bom_item_schema},{"maxItems",64}}}}, {"id","type","parts"}));
  const Json expression = object({{"expression", object({
    {"op",{{"enum",{"add","subtract","multiply","divide"}}}},
    {"args",{{"type","array"},{"items",scalar_ref},{"minItems",2},{"maxItems",2}}},
    {"unit",{{"enum",{"mm","deg","rad","dimensionless"}}}}
  }, {"op","args","unit"})}}, {"expression"});
  const Json operations = Json::array({
    object({{"op", {{"const", "set_parameter"}}}, {"name", id}, {"value", numeric}}, {"op", "name", "value"}),
    object({{"op", {{"const", "add_feature"}}}, {"feature", feature_ref}}, {"op", "feature"}),
    object({{"op", {{"const", "replace_feature"}}}, {"id", id}, {"feature", feature_ref}}, {"op", "id", "feature"}),
    object({{"op", {{"const", "remove_feature"}}}, {"id", id}}, {"op", "id"}),
    object({{"op", {{"const", "set_output"}}}, {"feature_id", id}}, {"op", "feature_id"}),
    object({{"op",{{"const","set_part_placement"}}},{"assembly_id",id},{"part_id",id},{"placement",placement_schema}}, {"op","assembly_id","part_id","placement"}),
    object({{"op",{{"const","set_mate"}}},{"assembly_id",id},{"mate",mate_schema}}, {"op","assembly_id","mate"}),
    object({{"op",{{"const","remove_mate"}}},{"assembly_id",id},{"mate_id",id}}, {"op","assembly_id","mate_id"}),
    object({{"op",{{"const","set_bom_item"}}},{"assembly_id",id},{"item",bom_item_schema}}, {"op","assembly_id","item"}),
    object({{"op",{{"const","remove_bom_item"}}},{"assembly_id",id},{"input",id}}, {"op","assembly_id","input"})
  });
  Json definitions = {
    {"selector", selector},
    {"placement",placement_schema}, {"assembly_part",part_schema}, {"mate",mate_schema}, {"bom_item",bom_item_schema},
    {"scalar", {{"oneOf", Json::array({numeric, object({{"parameter", id}}, {"parameter"}), expression})}}},
    {"vector3", {{"type", "array"}, {"items", scalar_ref}, {"minItems", 3}, {"maxItems", 3}}},
    {"feature", {{"oneOf", features}}},
    {"operation", {{"oneOf", operations}}},
    {"model", object({{"schema_version", {{"type", "integer"}, {"const", 1}}}, {"units", {{"const", "mm"}}},
      {"parameters", {{"type", "object"}, {"propertyNames", id}, {"additionalProperties", numeric}, {"maxProperties", 128}}},
      {"features", {{"type", "array"}, {"items", feature_ref}, {"minItems", 1}, {"maxItems", 256}}},
      {"output", id}}, {"schema_version", "units", "parameters", "features", "output"})}
  };
  auto bom_output_item=bom_item_schema;
  bom_output_item["properties"]["quantity"]={{"type","integer"},{"minimum",1},{"maximum",64}};
  bom_output_item["properties"]["part_ids"]={{"type","array"},{"items",id},{"minItems",1},{"maxItems",64},{"uniqueItems",true}};
  bom_output_item["required"]={"item_number","input","quantity","part_ids"};
  definitions["bom"]=object({{"assembly_id",id},
    {"items",{{"type","array"},{"items",bom_output_item},{"minItems",1},{"maxItems",64}}},
    {"total_quantity",{{"type","integer"},{"minimum",1},{"maximum",64}}}},{"assembly_id","items","total_quantity"});
  const Json real = {{"type","number"}};
  const Json nonnegative = {{"type","number"},{"minimum",0}};
  const Json point_output = {{"type","array"},{"items",real},{"minItems",3},{"maxItems",3}};
  const Json bounds_output = object({{"min",point_output},{"max",point_output}}, {"min","max"});
  definitions["assembly_summary"] = object({
    {"parts",{{"type","array"},{"minItems",1},{"maxItems",64},{"items",object({
      {"id",id},{"input",id},{"transform",{{"type","array"},{"items",real},{"minItems",16},{"maxItems",16}}},
      {"bounds_mm",bounds_output},{"volume_mm3",nonnegative}}, {"id","input","transform","bounds_mm","volume_mm3"})}}},
    {"mates",{{"type","array"},{"maxItems",63},{"items",object({{"id",id},{"type",{{"const","rigid"}}},{"parent",id},{"child",id}}, {"id","type","parent","child"})}}}
  }, {"parts","mates"});
  const Json face_id = {{"type","string"},{"pattern","^face-[1-9][0-9]*$"}};
  const Json edge_id = {{"type","string"},{"pattern","^edge-[1-9][0-9]*$"}};
  const Json local_id = {{"type","string"},{"pattern","^(face|edge)-[1-9][0-9]*$"}};
  const Json history_entry = object({
    {"source_feature_id",id},{"source_kind",{{"enum",{"face","edge"}}}},{"source_id",local_id},
    {"relation",{{"enum",{"unchanged","modified","generated","deleted"}}}},
    {"result_kind",{{"enum",{"face","edge"}}}},{"result_id",local_id},
    {"instance_index",{{"type","integer"},{"minimum",0},{"maximum",63}}},{"part_id",id}
  }, {"source_feature_id","source_kind","source_id","relation"});
  const Json provenance = object({
    {"feature_id",id},{"feature_type",{{"type","string"}}},{"content_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},
    {"dependencies",{{"type","array"},{"items",id},{"maxItems",64}}},
    {"reference_policy",{{"const","geometric_replay"}}},{"history_lifetime",{{"const","evaluation"}}},
    {"history",{{"type","array"},{"items",history_entry},{"maxItems",10000}}},{"history_truncated",{{"type","boolean"}}}
  }, {"feature_id","feature_type","dependencies","reference_policy","history_lifetime","history","history_truncated"});
  const Json face_output = object({
    {"id",face_id},{"surface_kind",{{"enum",{"plane","cylinder","cone","sphere","torus","bezier","bspline","revolution","extrusion","offset","other"}}}},
    {"area_mm2",nonnegative},{"center_mm",point_output},{"bounds_mm",bounds_output},{"normal",point_output},{"part_id",id}
  }, {"id","surface_kind","area_mm2","center_mm","bounds_mm"});
  const Json edge_output = object({
    {"id",edge_id},{"curve_kind",{{"enum",{"line","circle","ellipse","hyperbola","parabola","bezier","bspline","offset","other"}}}},
    {"length_mm",nonnegative},{"center_mm",point_output},{"bounds_mm",bounds_output},{"direction",point_output},
    {"degenerate",{{"type","boolean"}}},{"radius_mm",nonnegative},{"axis",point_output},{"selector",{{"$ref","#/$defs/selector"}}},{"part_id",id}
  }, {"id","curve_kind","length_mm","center_mm","bounds_mm","degenerate"});
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
    {"edges",{{"type","array"},{"items",object({{"id",edge_id},{"part_id",id},{"points",{{"type","array"},{"items",point_output},{"maxItems",200000}}}}, {"id","points"})},{"maxItems",10000}}}
  }, {"schema_version","units","feature_id","selection_lifetime","linear_deflection_mm","angular_deflection_rad","positions","triangles","triangle_faces","edges"});
  return definitions;
}

namespace {
double evaluate_scalar(const Json& value, const Json& parameters, const std::string& unit, int depth, int& nodes) {
  if (++nodes > 128 || depth > 16) throw Error("limit_exceeded", "Expressions permit at most 128 nodes and 16 levels");
  if (value.is_number()) return number(value);
  if (value.is_object() && value.contains("expression")) {
    fields(value, {"expression"});
    const auto& expression = value.at("expression");
    fields(expression, {"op", "args", "unit"});
    if (text_field(expression, "unit") != unit)
      throw Error("invalid_model", "Expression unit does not match its argument context", {{"expected_unit", unit}});
    const auto op = text_field(expression, "op");
    const auto& args = expression.at("args");
    if (!args.is_array() || args.size() != 2) throw Error("invalid_model", "Arithmetic expressions require two arguments");
    if (op != "add" && op != "subtract" && op != "multiply" && op != "divide")
      throw Error("invalid_model", "Unsupported arithmetic expression operation");
    const auto a = evaluate_scalar(args[0], parameters, unit, depth+1, nodes);
    // Multiplication/division scale a dimensional value by a dimensionless
    // factor; they do not implicitly invent compound units.
    const auto b = evaluate_scalar(args[1], parameters, op == "multiply" || op == "divide" ? "dimensionless" : unit, depth+1, nodes);
    if (op == "divide" && b == 0) throw Error("invalid_model", "Expression division by zero");
    return number(op == "add" ? a+b : op == "subtract" ? a-b : op == "multiply" ? a*b : a/b);
  }
  fields(value, {"parameter"});
  const auto name = text_field(value, "parameter");
  if (!parameters.contains(name)) throw Error("invalid_model", "Unknown parameter: " + name);
  return number(parameters.at(name));
}
}
double scalar(const Json& value, const Json& parameters, const std::string& unit) {
  int nodes = 0;
  return evaluate_scalar(value, parameters, unit, 0, nodes);
}

std::array<double, 3> vector3(const Json& value, const Json& parameters, const std::string& unit) {
  if (!value.is_array() || value.size() != 3) throw Error("invalid_model", "Expected a 3-element vector");
  return {scalar(value[0], parameters, unit), scalar(value[1], parameters, unit), scalar(value[2], parameters, unit)};
}

namespace {
void validate_selector(const Json& selector, const Json& parameters, const std::string& input) {
  fields(selector, {"type", "feature_id", "curve_kind", "expected_count"}, {"direction", "center", "length"});
  if (text_field(selector, "type") != "geometric")
    throw Error("invalid_model", "Only geometric design references can be saved; evaluated selections cannot be persisted");
  if (text_field(selector, "feature_id") != input)
    throw Error("invalid_model", "Selector feature_id must equal the fillet input");
  static const std::set<std::string> kinds = {"line", "circle", "ellipse", "hyperbola", "parabola", "bezier", "bspline", "offset", "other"};
  if (!kinds.contains(text_field(selector, "curve_kind")))
    throw Error("invalid_model", "Unsupported selector curve_kind");
  const auto& count = selector.at("expected_count");
  if (!count.is_number_integer() || count < 1 || count > 10000)
    throw Error("invalid_model", "Selector expected_count must be an integer from 1 to 10000");
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
void assembly(const Json& feature, const Json& parameters, const std::map<std::string,std::string>& types) {
  fields(feature, {"id", "type", "parts"}, {"mates","bom"});
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
      if (!types.contains(input) || types.at(input) == "sketch" || types.at(input) == "assembly")
        throw Error("invalid_model", "Assembly part input must name an earlier solid feature, not a sketch or assembly", {{"source_feature_id",input}});
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
      fields(item,{"input"},{"item_number","part_number","description","material"});
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
    }
  }
  if (!feature.contains("mates")) return;
  const auto& mates = feature.at("mates");
  if (!mates.is_array() || mates.size() > 63) throw Error("invalid_model", "An assembly permits at most 63 mates");
  std::set<std::string> mate_ids;
  std::map<std::string,std::string> parents;
  for (const auto& mate : mates) {
    const auto mate_id = text_field(mate,"id");
    try {
      fields(mate, {"id","type","parent","child","parent_frame","child_frame"}, {"offset","angle_deg"});
      model_identifier(mate_id);
      if (!mate_ids.insert(mate_id).second) throw Error("invalid_model", "Duplicate assembly mate: " + mate_id);
      if (text_field(mate,"type") != "rigid") throw Error("invalid_model", "Only rigid assembly mates are supported");
      const auto parent=text_field(mate,"parent"), child=text_field(mate,"child");
      if (!part_ids.contains(parent) || !part_ids.contains(child)) throw Error("invalid_model", "Mate must name existing assembly parts");
      if (parent == child) throw Error("invalid_model", "A part cannot mate to itself");
      if (!parents.emplace(child,parent).second) throw Error("invalid_model", "A part permits only one incoming rigid mate", {{"part_id",child}});
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
}
}

void validate_model(const Json& model) {
  fields(model, {"schema_version", "units", "parameters", "features", "output"});
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
  std::size_t assembly_parts = 0;
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
      if (types.at(target) == "sketch") throw Error("invalid_model", "This operation requires a solid input, not an intermediate sketch", {{"feature_id", id}});
      if (types.at(target) == "assembly") throw Error("invalid_model", "Edit assembly source parts before assembling; solid operations cannot consume assemblies", {{"feature_id", id}});
    };
    auto sketch_dependency = [&](const std::string& target) {
      if (!prior.contains(target) || types.at(target) != "sketch")
        throw Error("invalid_model", "Profile reference must name an earlier sketch", {{"feature_id", id}, {"source_feature_id", target}});
    };
    if (type == "box") {
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
    } else if (type == "cut" || type == "fuse") {
      fields(feature, {"id", "type", "left", "right"});
      dependency("left"); dependency("right");
    } else if (type == "fillet") {
      fields(feature, {"id", "type", "input", "radius", "edges"});
      dependency("input"); positive(feature.at("radius"));
      const auto& edges = feature.at("edges");
      if (edges.is_string()) {
        if (edges != "all") throw Error("invalid_model", "Fillet edges must be all or a geometric selector");
      } else validate_selector(edges, parameters, text_field(feature, "input"));
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
      } else throw Error("invalid_model", "Unsupported sketch profile");
    } else if (type == "extrude") {
      fields(feature, {"id", "type", "input", "distance"});
      sketch_dependency(text_field(feature,"input"));
      if (std::abs(scalar(feature.at("distance"),parameters)) < 1e-5)
        throw Error("invalid_model", "Extrusion distance magnitude must be at least 0.00001 mm");
    } else if (type == "revolve") {
      fields(feature, {"id", "type", "input", "axis", "angle_deg"});
      sketch_dependency(text_field(feature,"input")); axis(feature.at("axis"), parameters, "direction");
      const auto angle = scalar(feature.at("angle_deg"),parameters,"deg");
      if (angle <= 0 || angle > 360) throw Error("invalid_model", "Revolve angle must be greater than zero and at most 360 degrees");
    } else if (type == "loft") {
      fields(feature, {"id", "type", "sections"}, {"ruled"});
      const auto& sections = feature.at("sections");
      if (!sections.is_array() || sections.size() < 2 || sections.size() > 32)
        throw Error("invalid_model", "A loft needs 2 to 32 sketch sections");
      for (const auto& section : sections) {
        if (!section.is_string()) throw Error("invalid_model", "Loft section references must be strings");
        sketch_dependency(section.get<std::string>());
      }
      if (feature.contains("ruled") && !feature.at("ruled").is_boolean()) throw Error("invalid_model", "ruled must be boolean");
    } else if (type == "sweep") {
      fields(feature, {"id", "type", "input", "path"}); sketch_dependency(text_field(feature,"input"));
      const auto& path = feature.at("path");
      if (!path.is_array() || path.size() < 2 || path.size() > 64) throw Error("invalid_model", "A sweep path needs 2 to 64 points");
      for (const auto& p : path) vector3(p,parameters);
    } else if (type == "transform" || type == "instance") {
      fields(feature, {"id", "type", "input"}, {"translation", "rotation"}); dependency("input");
      if (types.at(text_field(feature,"input")) == "sketch") throw Error("invalid_model", "Transform and instance currently require solid inputs");
      if (feature.contains("translation")) vector3(feature.at("translation"), parameters);
      if (feature.contains("rotation")) {
        const auto& rotation = feature.at("rotation");
        fields(rotation, {"origin", "axis", "angle_deg"});
        vector3(rotation.at("origin"),parameters); unit_vector(rotation.at("axis"),parameters);
        scalar(rotation.at("angle_deg"),parameters,"deg");
      }
    } else if (type == "pattern") {
      fields(feature, {"id", "type", "input", "count", "step"}); dependency("input");
      if (types.at(text_field(feature,"input")) == "sketch") throw Error("invalid_model", "Patterns currently require solid inputs");
      if (!feature.at("count").is_number_integer() || feature.at("count") < 2 || feature.at("count") > 64)
        throw Error("invalid_model", "Pattern count must be an integer from 2 to 64");
      const auto step = vector3(feature.at("step"),parameters);
      if (std::hypot(step[0],step[1],step[2]) < 1e-5) throw Error("invalid_model", "Pattern step must be nonzero");
    } else if (type == "hole") {
      fields(feature, {"id", "type", "input", "origin", "axis", "radius", "depth"}); dependency("input");
      vector3(feature.at("origin"),parameters); unit_vector(feature.at("axis"),parameters);
      positive(feature.at("radius")); positive(feature.at("depth"));
    } else if (type == "assembly") {
      assembly(feature,parameters,types);
      assembly_parts += feature.at("parts").size();
      if (assembly_parts > 256) throw Error("limit_exceeded", "A model permits at most 256 assembly parts across all assembly features");
    } else if (type == "import_step") {
      fields(feature, {"id", "type", "content", "sha256"});
      const auto content = text_field(feature,"content");
      const auto digest = text_field(feature,"sha256");
      if (content.empty() || content.size() > 512*1024) throw Error("limit_exceeded", "Embedded STEP content must contain 1 to 524288 bytes");
      if (digest.size() != 64 || digest.find_first_not_of("0123456789abcdef") != std::string::npos || sha256(content) != digest)
        throw Error("invalid_model", "Embedded STEP content does not match its SHA-256 identity");
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
  if (types.at(text_field(model,"output")) == "sketch") throw Error("invalid_model", "The model output must be solid geometry, not an intermediate sketch");
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
}

Json apply_operations(const Json& model, const Json& operations) {
  if (!operations.is_array() || operations.empty() || operations.size() > 256)
    throw Error("invalid_argument", "operations must contain 1–256 edits");
  auto candidate = model;
  for (std::size_t index = 0; index < operations.size(); ++index) {
   const auto& operation = operations[index];
   try {
    const auto op = text_field(operation, "op");
    auto& features = candidate.at("features");
    if (op == "set_parameter") {
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
