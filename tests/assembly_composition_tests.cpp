#include "agentcad/bom.hpp"
#include "agentcad/drawing.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include <STEPControl_Reader.hxx>
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include <functional>
#include <iostream>
#include <set>

using namespace agentcad;
namespace {
int checks=0;
void check(bool value,const std::string& message) {++checks;if(!value)throw std::runtime_error(message);}
void near(double value,double expected) {check(std::abs(value-expected)<1e-6,"Expected "+std::to_string(expected)+", got "+std::to_string(value));}
void fails(const std::string& code,const std::function<void()>& run) {
  try {run();}catch(const Error& e){check(e.code==code,"Expected "+code+", got "+e.code+": "+e.what());return;}
  throw std::runtime_error("Expected "+code);
}
struct Temp {
  fs::path path=temporary_file(fs::temp_directory_path());
  Temp(){fs::remove(path);directory(path);}~Temp(){std::error_code error;fs::remove_all(path,error);}
};
Json fixture() {
  return parse_json(R"({"schema_version":1,"units":"mm","parameters":{"angle":90},"features":[
    {"id":"block","type":"box","size":[10,4,2]},
    {"id":"rod","type":"box","size":[6,2,2]},
    {"id":"module","type":"assembly","parts":[{"id":"foot","input":"block"},{"id":"link","input":"rod"}],
      "mates":[{"id":"pivot","type":"revolute","parent":"foot","child":"link","angle_deg":{"parameter":"angle"},"angle_limits_deg":[0,180],
        "parent_frame":{"origin":[10,0,2],"normal":[0,0,1],"x_direction":[1,0,0]},
        "child_frame":{"origin":[0,0,0],"normal":[0,0,1],"x_direction":[1,0,0]}}],
      "bom":[{"input":"block","item_number":7,"part_number":"BLOCK"},{"input":"rod","item_number":2,"part_number":"ROD"}]},
    {"id":"machine","type":"assembly","parts":[
      {"id":"left","input":"module","placement":{"translation":[20,0,0],"rotation":{"origin":[0,0,0],"axis":[0,0,1],"angle_deg":90}}},
      {"id":"right","input":"module","placement":{"translation":[60,0,0]}},
      {"id":"spare","input":"block","placement":{"translation":[100,0,0]}}],
      "bom":[{"input":"block","item_number":5,"part_number":"BLOCK"}]}
  ],"output":"machine"})");
}
const Json& part(const Json& summary,const std::string& id) {
  for(const auto& item:summary.at("assembly").at("parts"))if(item.at("id")==id)return item;
  throw std::runtime_error("Missing part: "+id);
}
void geometry(const fs::path& root) {
  auto model=fixture();BuiltModel built(model);const auto summary=built.summary();
  check(summary.at("solid_count")==5,"Repeated nested occurrences stay independent solids");
  near(summary.at("volume_mm3"),288);
  check(summary.at("assembly").at("tree")==assembly_structure(model),"Kernel and document hierarchy agree");
  check(assembly_structure(model).size()==7,"Two subassemblies and five leaves are exposed");
  const auto left=part(summary,"left/link").at("transform");
  near(left[0],-1);near(left[5],-1);near(left[3],20);near(left[7],10);near(left[11],2);
  const auto right=part(summary,"right/link").at("transform");near(right[3],70);near(right[7],0);near(right[11],2);
  std::set<std::string> owners;
  const auto topology=built.topology();
  for(const auto& face:topology.at("faces"))owners.insert(face.at("part_id"));
  check(owners==std::set<std::string>({"left/foot","left/link","right/foot","right/link","spare"}),"Every face names its exact occurrence");
  for(const auto& edge:topology.at("edges"))check(owners.contains(edge.at("part_id")),"Every edge has a leaf owner");
  BuiltModel cached(model,built.snapshot());check(cached.summary()==summary,"Snapshot rebuild preserves all hierarchy and transforms");
  check(cached.topology().at("faces").size()==topology.at("faces").size(),"Cached nested topology remains complete");
  const auto path=root/"nested.step";built.export_file(path,"step");STEPControl_Reader reader;
  check(reader.ReadFile(path_to_utf8(path).c_str())==IFSelect_RetDone && reader.TransferRoots()>0,"Independent STEP readback succeeds");
  check(BRepCheck_Analyzer(reader.OneShape()).IsValid(),"Exported nested STEP is valid");
  GProp_GProps volume;BRepGProp::VolumeProperties(reader.OneShape(),volume);near(volume.Mass(),288);
  model["features"].push_back({{"id","station"},{"type","assembly"},{"parts",Json::array({
    {{"id","setup"},{"input","machine"},{"placement",{{"translation",{0,0,20}}}}}})}});model["output"]="station";
  const auto deep=BuiltModel(model).summary();near(part(deep,"setup/left/link").at("transform")[11],22);
  check(deep.at("assembly").at("tree")==assembly_structure(model),"Three-level hierarchy is stable");
  model=fixture();model["features"][3]["parts"][1]["placement"]=model["features"][3]["parts"][0]["placement"];
  check(BuiltModel(model).summary().at("face_count")==30,"Coincident subassemblies retain distinct topology");
}
void limits() {
  auto model=fixture();
  for(int level=3;level<=8;++level) {
    const auto prior=text_field(model,"output"),id="level"+std::to_string(level);
    model["features"].push_back({{"id",id},{"type","assembly"},{"parts",Json::array({{{"id","child"},{"input",prior}}})}});model["output"]=id;
  }
  validate_model(model);++checks;
  model["features"].push_back({{"id","level9"},{"type","assembly"},{"parts",Json::array({{{"id","child"},{"input","level8"}}})}});
  fails("limit_exceeded",[&]{validate_model(model);});
  model=fixture();model["features"][3]["parts"]=Json::array();
  for(int i=0;i<64;++i)model["features"][3]["parts"].push_back({{"id","item"+std::to_string(i)},{"input","module"}});
  model["features"][3].erase("bom");
  validate_model(model);++checks; // 128 leaves; direct and expanded budgets differ.
  Json repeated=Json::array();for(int i=0;i<9;++i)repeated.push_back({{"id","item"+std::to_string(i)},{"input","machine"}});
  model["features"].push_back({{"id","too_wide"},{"type","assembly"},{"parts",repeated}});
  fails("limit_exceeded",[&]{validate_model(model);});
  for(const auto* bad:{"/left","left/","left//link","left/../link","left.link"}) fails("invalid_argument",[&]{validate_occurrence_path(bad);});
  validate_occurrence_path("left/link");++checks;
}
void bom_and_drawing() {
  auto model=fixture();
  model["features"][3]["bom"].push_back({{"input","module"},{"item_number",9},{"part_number","MODULE"}});
  const auto bom=build_bom(model);
  check(bom.at("total_quantity")==5 && bom.at("items").size()==2,"BOM rolls up repeated leaf sources");
  auto structure=bom.at("structure");
  check(structure[0].at("bom").at("part_number")=="MODULE","BOM retains subassembly purchasing metadata");
  check(structure[1].at("bom").at("item_number")==7,"Hierarchy retains child-local item numbers");
  for (auto& node:structure) node.erase("bom");
  check(structure==assembly_structure(model),"BOM retains the hierarchy");
  for(const auto& item:bom.at("items")) {
    const bool block=item.at("input")=="block";
    check(item.at("quantity")== (block?3:2),"BOM quantities multiply through repeated subassemblies");
    check(item.at("part_number")== (block?"BLOCK":"ROD"),"Child metadata reaches rolled-up leaves");
    if(block)check(item.at("item_number")==5,"Explicit root item number retained");
  }
  const auto request=parse_json(R"({"views":[{"id":"front","orientation":"front","explode":[
    {"part_id":"left","translation":[0,0,20]},{"part_id":"left/link","translation":[0,0,3]}]}]})");
  const auto normalized=normalize_drawing(request,model);
  const auto& moves=normalized.at("views")[0].at("explode");
  check(moves.size()==2,"A group explosion expands only its own leaves");
  for(const auto& move:moves)near(move.at("translation")[2],move.at("part_id")=="left/link"?23:20);
  BuiltModel built(model);check(!built.drawing({{"views",normalized.at("views")},{"hidden_lines",normalized.at("hidden_lines")}}).at("views").empty(),"Nested exploded drawing uses exact geometry");
  const auto frames=built.robot_frames(model);
  check(frames.at("links").size()==5,"Nested robot uses only physical leaves without invented assembly bodies");
  check(frames.at("motion").at("dofs").size()==2,"Both occurrences retain their child joint");
}
void transactions(const fs::path& root) {
  set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));Service service(root/"service");
  const auto model=fixture();service.call("cad_create",{{"document_id","nested"},{"model",model}});
  const Json edits=Json::array({{{"op","set_joint_value"},{"assembly_id","module"},{"mate_id","pivot"},{"coordinate","angle_deg"},{"value",0}}});
  const auto preview=service.call("cad_preview",{{"document_id","nested"},{"expected_revision",1},{"kind","mesh"},{"operations",edits}});
  near(part(preview.at("summary"),"right/link").at("bounds_mm").at("max")[0],76);
  check(service.call("cad_read",{{"document_id","nested"}}).at("revision")==1,"Nested pose preview preserves HEAD");
  service.call("cad_apply",{{"document_id","nested"},{"expected_revision",1},{"operations",edits}});
  auto bad=edits;bad[0]["value"]=200;
  fails("invalid_model",[&]{service.call("cad_apply",{{"document_id","nested"},{"expected_revision",2},{"operations",bad}});});
  Service reopened(root/"service");check(reopened.call("cad_read",{{"document_id","nested"}}).at("revision")==2,"Failed nested motion preserves HEAD after reopen");
  check(reopened.call("cad_read",{{"document_id","nested"},{"revision",1}}).at("model")==model,"Historical component definitions remain intact");
  const auto current=reopened.call("cad_query",{{"document_id","nested"},{"revision",2}}).at("summary");
  near(part(current,"right/link").at("bounds_mm").at("max")[0],76);
}
}
int main(){try{configure_kernel_logging();Temp temp;geometry(temp.path);limits();bom_and_drawing();transactions(temp.path);
  std::cout<<checks<<" assembly composition checks passed\n";return 0;
}catch(const Error& e){std::cerr<<e.code<<": "<<e.what()<<" "<<e.details.dump()<<'\n';return 1;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
