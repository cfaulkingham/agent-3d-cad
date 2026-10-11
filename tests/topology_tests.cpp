#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include <algorithm>
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
// Failing fillet selectors report descriptors from the input feature's own
// evaluated enumeration: the same IDs and geometry that topology(input) lists.
void fillet_diagnostic_tests(const Json& input_topology) {
  std::map<std::string,Json> input_edges;
  for (const auto& edge : input_topology.at("edges")) {
    auto descriptor=edge; descriptor.erase("selector");
    input_edges.emplace(edge.at("id"),descriptor);
  }
  const auto failure=[](const Json& selector) {
    try { BuiltModel invalid(fillet(selector)); } catch (const Error& e) { return e; }
    throw std::runtime_error("Expected a selector failure");
  };
  Json selector={{"type","geometric"},{"feature_id","base"},{"curve_kind","circle"},{"expected_count",4}};
  auto e=failure(selector);
  require(e.code=="selection_missing" && e.details.at("source_feature_id")=="base","missing selector reports its input feature, got "+e.code+": "+e.what());
  require(e.details.at("candidates").size()==input_edges.size() && e.details.at("candidates_truncated")==false,"every input edge is a candidate");
  for (const auto& candidate : e.details.at("candidates")) {
    const auto found=input_edges.find(candidate.at("id"));
    require(found!=input_edges.end() && found->second==candidate,"candidate "+candidate.at("id").get<std::string>()+" is the input feature's evaluated edge");
  }
  selector=vertical(); selector["expected_count"]=1;
  e=failure(selector);
  require(e.code=="selection_ambiguous" && e.details.at("matches").size()==4,"ambiguous selector reports every match");
  for (const auto& match : e.details.at("matches")) {
    const auto found=input_edges.find(match.at("id"));
    require(found!=input_edges.end() && found->second==match,"match "+match.at("id").get<std::string>()+" is the input feature's evaluated edge");
  }
}
// Independent all-pairs oracle: the spatial index may remove work, never a
// competing geometric match. Compare both suggested presence and exact rules.
void suggestion_oracle(const Json& topology) {
  for(const auto* collection:{"faces","edges"}) {
    const bool face=std::string(collection)=="faces";
    const auto kind=face?"surface_kind":"curve_kind",measure=face?"area_mm2":"length_mm";
    const auto orientation=face?"normal":"direction";
    for(const auto& entity:topology.at(collection)) {
      const auto center=entity.at("center_mm").get<std::array<double,3>>();
      const bool eligible=(!entity.value("degenerate",false))&&entity.at(measure).get<double>()<=1e6&&
        std::all_of(center.begin(),center.end(),[](double value){return std::abs(value)<=1e6;});
      int matching=0;
      for(const auto& candidate:topology.at(collection)) {
        if(candidate.value("degenerate",false)||candidate.at(kind)!=entity.at(kind))continue;
        const auto other=candidate.at("center_mm").get<std::array<double,3>>();
        if(std::hypot(center[0]-other[0],center[1]-other[1],center[2]-other[2])>1e-5||
          std::abs(candidate.at(measure).get<double>()-entity.at(measure).get<double>())>1e-5)continue;
        if(entity.contains(orientation)) {
          if(!candidate.contains(orientation))continue;
          const auto a=entity.at(orientation).get<std::array<double,3>>(),b=candidate.at(orientation).get<std::array<double,3>>();
          double dot=(a[0]*b[0]+a[1]*b[1]+a[2]*b[2])/std::hypot(a[0],a[1],a[2]);
          if(!face)dot=std::abs(dot);
          if(dot<std::cos(1e-6))continue;
        }
        ++matching;
      }
      const bool expected=eligible&&matching==1;
      require(entity.contains("selector")==expected,"selector suggestions preserve independent all-pairs uniqueness");
      if(!expected)continue;
      Json rule={{"type","geometric"},{"feature_id",topology.at("feature_id")},{kind,entity.at(kind)},{"expected_count",1},
        {"center",{{"point",entity.at("center_mm")},{"tolerance",1e-5}}},
        {face?"area":"length",{{"value",entity.at(measure)},{"tolerance",1e-5}}}};
      if(entity.contains(orientation))rule[orientation]={{"vector",entity.at(orientation)},{"tolerance",1e-6}};
      require(entity.at("selector")==rule,"selector optimization preserves exact emitted geometric rules");
    }
  }
}
void suggestion_candidate_tests() {
  for(int axis=0;axis<3;++axis)for(const double separation:{1e-5,1.0001e-5,40.0}) {
    auto model=box();model["features"][0]["size"]={1,1,1};
    Json step=Json::array({0.0,0.0,0.0});step[axis]=separation;
    model["features"].push_back({{"id","copies"},{"type","pattern"},{"input","base"},{"count",3},{"step",step}});
    model["output"]="copies";
    suggestion_oracle(BuiltModel(model).topology());
    // Reversing the same step creates exactly coincident retained occurrences;
    // they must suppress suggestions instead of becoming arbitrary unique picks.
    step[axis]=-separation;
    model["features"].push_back({{"id","overlap"},{"type","pattern"},{"input","copies"},{"count",2},{"step",step}});
    model["output"]="overlap";
    const auto duplicated=BuiltModel(model).topology();suggestion_oracle(duplicated);
    require(std::any_of(duplicated.at("faces").begin(),duplicated.at("faces").end(),[](const Json& item){return !item.contains("selector");}),"coincident native faces remain ambiguous");
  }
  for(const Json step:{Json::array({8e-6,6.001e-6,0}),Json::array({8e-6,5.999e-6,8e-6}),Json::array({-50,30,20})}) {
    auto model=box();model["features"][0]["origin"]={-200,-300,-400};
    model["features"].push_back({{"id","copies"},{"type","pattern"},{"input","base"},{"count",3},{"step",step}});model["output"]="copies";
    suggestion_oracle(BuiltModel(model).topology());
  }
  for(bool cone:{false,true}) {
    auto model=box();model["features"][0]={{"id","base"},{"type","cylinder"},{"radius",2},{"height",6}};
    if(cone)model["features"]=Json::array({
      Json{{"id","profile"},{"type","sketch"},{"profile",{{"type","circle"},{"radius",2}}},
        {"workplane",{{"origin",{0,0,0}},{"normal",{0,0,1}},{"x_direction",{1,0,0}}}}},
      Json{{"id","base"},{"type","loft"},{"sections",{"profile"}},{"end_vertex",{0,0,6}},{"ruled",true}}});
    model["features"].push_back({{"id","copies"},{"type","pattern"},{"input","base"},{"count",3},{"step",{0,0,12}}});model["output"]="copies";
    suggestion_oracle(BuiltModel(model).topology());
  }
}
void topology_tests() {
  suggestion_candidate_tests();
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
  fillet_diagnostic_tests(topo);
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
