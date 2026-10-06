#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <numbers>
#include <set>
using namespace agentcad;
namespace {
int checks = 0;
void require(bool condition, const std::string& message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
void error(const std::string& code, const std::function<void()>& function) {
  try { function(); } catch (const Error& e) {
    require(e.code == code, "Expected " + code + ", got " + e.code + ": " + e.what());
    return;
  }
  throw std::runtime_error("Expected " + code);
}
Json box() {
  return {{"schema_version",1},{"units","mm"},{"parameters",{{"height",10}}},
    {"features",Json::array({{{"id","base"},{"type","box"},{"size",Json::array({20,30,Json{{"parameter","height"}}})}}})},{"output","base"}};
}
Json vertical() {
  return {{"type","geometric"},{"feature_id","base"},{"curve_kind","line"},{"expected_count",4},
    {"direction",{{"vector",{0,0,1}},{"tolerance",1e-6}}}};
}
Json fillet(Json selector) {
  auto model = box();
  model["features"].push_back({{"id","rounded"},{"type","fillet"},{"input","base"},{"radius",2},{"edges",selector}});
  model["output"] = "rounded";
  return model;
}
void pattern_history_tests() {
  auto model=box();
  model["features"].push_back({{"id","source"},{"type","transform"},{"input","base"},{"translation",{100,0,0}},
    {"rotation",{{"origin",{0,0,0}},{"axis",{0,0,1}},{"angle_deg",90}}}});
  model["features"].push_back({{"id","copies"},{"type","pattern"},{"input","source"},{"count",3},{"step",{50,10,5}}});
  model["output"]="copies";
  BuiltModel built(model);
  const auto source=built.topology("source"), result=built.topology();
  const auto& provenance=result.at("provenance");
  require(provenance.at("dependencies")==Json::array({"source"}),"pattern identifies its reusable source");
  require(provenance.at("history_lifetime")=="evaluation" && provenance.at("history_truncated")==false,"pattern history is complete evaluation evidence");
  std::map<std::string,Json> source_entities,result_entities;
  for (const auto* kind : {"faces","edges"}) {
    for (const auto& entity : source.at(kind)) source_entities.emplace(entity.at("id"),entity);
    for (const auto& entity : result.at(kind)) result_entities.emplace(entity.at("id"),entity);
  }
  std::map<int,std::set<std::string>> represented_sources;
  std::set<std::string> represented_results;
  for (const auto& entry : provenance.at("history")) {
    require(entry.at("source_feature_id")=="source","pattern lineage is qualified by its input feature");
    const auto instance=entry.at("instance_index").get<int>();
    require(instance>=0 && instance<3,"pattern lineage identifies the copy");
    require(entry.at("relation")=="modified" && entry.at("source_kind")==entry.at("result_kind"),"transformed copies retain face and edge lineage");
    const auto source_id=entry.at("source_id").get<std::string>(), result_id=entry.at("result_id").get<std::string>();
    require(source_entities.contains(source_id) && result_entities.contains(result_id),"pattern lineage resolves against input and completed output topology");
    require(represented_sources[instance].insert(source_id).second,"each source entity has one transformed descendant per copy");
    require(represented_results.insert(result_id).second,"each transformed descendant belongs to one copy");
    const auto& before=source_entities.at(source_id);
    const auto& after=result_entities.at(result_id);
    const std::array<double,3> step{50,10,5};
    for (std::size_t axis=0;axis<step.size();++axis)
      require(std::abs(after.at("center_mm")[axis].get<double>()-before.at("center_mm")[axis].get<double>()-instance*step[axis])<1e-6,"lineage points to the geometrically correct translated entity");
    const auto measure=entry.at("source_kind")=="face" ? "area_mm2" : "length_mm";
    require(std::abs(after.at(measure).get<double>()-before.at(measure).get<double>())<1e-6,"copy lineage preserves area or length");
  }
  require(represented_sources.size()==3,"all copies have lineage including the unshifted copy");
  for (const auto& [instance,entities] : represented_sources)
    require(entities.size()==source_entities.size(),"each copy maps every source face and edge");
  require(represented_results.size()==result_entities.size(),"lineage covers every entity in the completed pattern");
  const auto mesh=built.mesh();
  for (const auto& face : mesh.at("triangle_faces")) require(represented_results.contains(face),"pattern mesh face shares lineage topology IDs");
  for (const auto& edge : mesh.at("edges")) require(represented_results.contains(edge.at("id")),"pattern mesh edge shares lineage topology IDs");
}
void topology_tests() {
  pattern_history_tests();
  auto model = fillet(vertical());
  BuiltModel built(model);
  const double expected = 6000 - (16 - 4*std::numbers::pi)*10;
  require(std::abs(built.summary().at("volume_mm3").get<double>()-expected) < 1e-6, "analytic four vertical edge fillet volume");
  auto selector = vertical(); selector["direction"]["vector"] = {0,0,-7};
  require(std::abs(BuiltModel(fillet(selector)).summary().at("volume_mm3").get<double>()-expected) < 1e-6, "direction is normalized and unoriented");
  const auto topo = built.topology("base");
  const auto mesh = built.mesh("base");
  require(topo.at("feature_id") == "base" && topo.at("faces").size() == 6 && topo.at("edges").size() == 12, "feature topology retained");
  require(mesh.at("triangles").size() == 12 && mesh.at("triangle_faces").size() == 12, "box tessellation");
  std::set<std::string> faces, edges, represented_faces, represented_edges;
  for (const auto& face : topo.at("faces")) {
    faces.insert(face.at("id"));
    require(face.at("surface_kind") == "plane" && face.contains("normal"), "face surface descriptor");
  }
  for (const auto& edge : topo.at("edges")) {
    edges.insert(edge.at("id"));
    require(edge.contains("selector"), "unique edge pick has selector suggestion");
  }
  for (const auto& face : mesh.at("triangle_faces")) {
    require(faces.contains(face.get<std::string>()), "triangle face id exists in topology");
    represented_faces.insert(face);
  }
  for (const auto& triangle : mesh.at("triangles")) for (const auto& vertex : triangle)
    require(vertex.get<std::size_t>() < mesh.at("positions").size(), "triangle index bounds");
  for (const auto& edge : mesh.at("edges")) {
    require(edges.contains(edge.at("id").get<std::string>()), "polyline edge id exists in topology");
    require(edge.at("points").size() == 2, "straight edge has endpoints");
    represented_edges.insert(edge.at("id"));
  }
  require(represented_faces == faces && represented_edges == edges, "all topology represented in mesh");
  require(built.topology("base") == topo, "meshing preserves evaluation topology mapping");
  require(built.topology().at("feature_id") == "rounded", "default output topology");
  const auto rounded_topology = built.topology();
  require(rounded_topology.at("provenance").at("dependencies") == Json::array({"base"}), "feature graph provenance");
  std::set<std::string> rounded_ids;
  for (const auto& face : rounded_topology.at("faces")) rounded_ids.insert(face.at("id"));
  for (const auto& edge : rounded_topology.at("edges")) rounded_ids.insert(edge.at("id"));
  require(!rounded_topology.at("provenance").at("history").empty(), "fillet OCCT history is retained");
  for (const auto& relation : rounded_topology.at("provenance").at("history")) {
    require(relation.at("source_feature_id") == "base", "history source is feature scoped");
    if (relation.contains("result_id")) require(rounded_ids.contains(relation.at("result_id")), "history target belongs to evaluated output");
  }
  Json picked_selector;
  for (const auto& edge : topo.at("edges")) {
    const auto& center=edge.at("center_mm");
    if (std::abs(center[0].get<double>())<1e-6 && std::abs(center[1].get<double>())<1e-6 && std::abs(center[2].get<double>()-5)<1e-6) picked_selector=edge.at("selector");
  }
  require(picked_selector.is_object(), "picked unique vertical edge suggests design rule");
  auto evolving=fillet(picked_selector);
  evolving["features"][0]["size"][0]=25;
  require(std::abs(BuiltModel(evolving).summary().at("volume_mm3").get<double>()-(7500-(4-std::numbers::pi)*10))<1e-6, "same geometric reference survives unrelated width edit");
  evolving["parameters"]["height"]=12;
  error("selection_missing",[&]{BuiltModel invalid(evolving);});
  error("not_found", [&]{ built.topology("missing"); });
  QueryLimits limits;
  limits.topology_entities = 17;
  error("limit_exceeded", [&]{ built.topology("base",limits); });
  limits = {}; limits.mesh_vertices = 23;
  error("limit_exceeded", [&]{ built.mesh("base",limits); });
  limits = {}; limits.mesh_triangles = 11;
  error("limit_exceeded", [&]{ built.mesh("base",limits); });
  limits = {}; limits.edge_points = 23;
  error("limit_exceeded", [&]{ built.mesh("base",limits); });
  selector = vertical(); selector["expected_count"] = 1;
  error("selection_ambiguous", [&]{ BuiltModel invalid(fillet(selector)); });
  selector["expected_count"] = 5;
  error("selection_count_mismatch", [&]{ BuiltModel invalid(fillet(selector)); });
  selector = vertical(); selector["length"] = {{"value",10},{"tolerance",1e-6}};
  auto exact_length = fillet(selector);
  BuiltModel valid(exact_length);
  exact_length["parameters"]["height"] = 12;
  error("selection_missing", [&]{ BuiltModel invalid(exact_length); });
  selector = {{"type","geometric"},{"feature_id","base"},{"curve_kind","line"},{"expected_count",4},
    {"length",{{"value",20},{"tolerance",1e-6}}}};
  auto ambiguous_edit = fillet(selector);
  BuiltModel initially_unique(ambiguous_edit);
  ambiguous_edit["parameters"]["height"] = 20;
  error("selection_ambiguous", [&]{ BuiltModel invalid(ambiguous_edit); });
  selector = vertical(); selector["feature_id"] = "other";
  error("invalid_model", [&]{ validate_model(fillet(selector)); });
  selector = vertical(); selector["evaluation_id"] = "old-evaluation";
  error("invalid_argument", [&]{ validate_model(fillet(selector)); });
  selector = {{"type","evaluated"},{"feature_id","base"},{"curve_kind","line"},{"expected_count",1}};
  error("invalid_model", [&]{ validate_model(fillet(selector)); });
  selector = vertical(); selector["expected_count"] = 0;
  error("invalid_model", [&]{ validate_model(fillet(selector)); });
  selector["expected_count"] = 10001;
  error("invalid_model", [&]{ validate_model(fillet(selector)); });
  selector = vertical(); selector["direction"]["vector"] = {0,0,0};
  error("invalid_model", [&]{ validate_model(fillet(selector)); });
  auto cylinder = box();
  cylinder["features"][0] = {{"id","base"},{"type","cylinder"},{"radius",5},{"height",10}};
  BuiltModel round(cylinder);
  auto ct = round.topology();
  auto cm = round.mesh();
  require(ct.at("faces").size() == 3 && ct.at("edges").size() == 3, "cylinder topology");
  require(cm.at("triangles").size() > 12, "curved surface tessellation");
  for (const auto& edge : ct.at("edges")) if (edge.at("curve_kind") == "circle") {
    require(std::abs(edge.at("length_mm").get<double>()-10*std::numbers::pi) < 1e-6, "circle circumference");
    require(std::abs(edge.at("radius_mm").get<double>()-5) < 1e-6, "circle radius");
  }
  require(model_definitions().at("selector").at("properties").contains("expected_count"), "selector schema exposed");
}
}
int main() {
  configure_kernel_logging();
  try { topology_tests(); std::cout << "topology: " << checks << " checks passed\n"; return 0; }
  catch (const std::exception& e) { std::cerr << "FAILED: " << e.what() << '\n'; return 1; }
}
