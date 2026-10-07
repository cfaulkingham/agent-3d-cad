#include "agentcad/bom.hpp"
#include "agentcad/drawing.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <thread>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message) {++checks;if(!value) throw std::runtime_error(message);}
void near(double a,double b) {require(std::abs(a-b)<1e-6,"Expected "+std::to_string(b)+", got "+std::to_string(a));}
void fails(const std::string& code,const std::function<void()>& action) {
  try {action();} catch(const Error& e) {require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what());return;}
  throw std::runtime_error("Expected "+code);
}
struct Temp {
  fs::path path;
  Temp():path(temporary_file(fs::temp_directory_path())) {fs::remove(path);directory(path);}
  ~Temp() {std::error_code ignored;fs::remove_all(path,ignored);}
};
Json parameter(const std::string& name) {return {{"parameter",name}};}
Json model() {
  Json parts=Json::array();
  parts.push_back({{"id","base"},{"input","plate"}});
  parts.push_back({{"id","left"},{"input","post"},{"placement",{{"translation",Json::array({8,3,parameter("thickness")})}}}});
  parts.push_back({{"id","right"},{"input","post"},{"placement",{{"translation",Json::array({14,3,parameter("thickness")})}}}});
  Json features=Json::array();
  features.push_back({{"id","plate"},{"type","box"},{"size",Json::array({20,10,parameter("thickness")})}});
  features.push_back({{"id","post"},{"type","box"},{"size",Json::array({4,4,parameter("post_height")})}});
  features.push_back({{"id","assembly"},{"type","assembly"},{"parts",parts},{"bom",Json::array({
    {{"input","plate"},{"item_number",7},{"part_number","PLT-01"},{"description","Plate, \"A\""},{"material","Aluminum"}},
    {{"input","post"},{"part_number","POST-01"},{"description","Square spacer"}}
  })}});
  return {{"schema_version",1},{"units","mm"},{"parameters",{{"thickness",2},{"post_height",6},{"gap",10}}},
    {"features",features},{"output","assembly"}};
}
Json recipe() {
  const Json anchor=Json::array({2,0,Json{{"expression",{{"op","multiply"},{"args",Json::array({parameter("post_height"),0.5})},{"unit","mm"}}}}});
  return {{"bom",true},{"sheet","A3"},{"layout","grid"},{"views",Json::array({{{"id","front"},{"orientation","front"}}})},
    {"balloons",Json::array({
      {{"view","front"},{"part_id","base"},{"anchor",{3,0,1}},{"label",{-15,1}}},
      {{"view","front"},{"part_id","left"},{"anchor",anchor},{"label",{-15,15}}},
      {{"view","front"},{"part_id","right"},{"anchor",anchor},{"label",{35,15}}}
    })}};
}
Json identity() {return {{"document_id","assembly"},{"revision",1},{"kernel_version",kernel_version()}};}
Json render(const Json& source,const Json& drawing) {
  BuiltModel built(source);const auto normalized=normalize_drawing(drawing,source);
  return render_drawing(built.drawing({{"views",normalized.at("views")},{"hidden_lines",normalized.at("hidden_lines")}}),normalized,identity());
}
Json worker_request(const Json& source,const Json& drawing) {
  return {{"kind","drawing"},{"drawing",normalize_drawing(drawing,source)},{"identity",identity()}};
}
std::string content(const Json& rendered,const std::string& filename) {
  for(const auto& file:rendered.at("files")) if(file.at("name")==filename) return file.at("content");
  throw std::runtime_error("Missing artifact "+filename);
}
const Json& balloon(const Json& result,const std::string& part) {
  for(const auto& item:result.at("balloons")) if(item.at("part_id")==part) return item;
  throw std::runtime_error("Missing balloon "+part);
}
Json call(Service& service,const std::string& tool,const Json& args) {
  for(int i=0;;++i) try {return service.call(tool,args);} catch(const Error& e) {
    if(e.code!="workspace_busy" || i==1000) throw;std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}
void drawing_tests() {
  const auto source=model(), drawing=recipe();const auto result=render(source,drawing);
  require(result.at("bom")==build_bom(source),"Drawing and independent BOM share grouping and numbers");
  require(result.at("bom").at("total_quantity")==3 && result.at("bom").at("items").size()==2,"Repeated source instances are counted in a single row");
  require(result.at("bom").at("items")[0].at("quantity")==2,"Spacer quantity is two");
  require(balloon(result,"base").at("item_number")==7 && balloon(result,"left").at("item_number")==1 && balloon(result,"right").at("item_number")==1,"Every instance uses its BOM item number");
  require(balloon(result,"base").at("anchor_mm")==Json::array({3,1}),"Source front-face anchor projects to world X/Z");
  require(balloon(result,"left").at("anchor_mm")==Json::array({10,5}),"Source-local anchor follows placed part");
  for(const auto& filename:{"drawing.pdf","drawing.svg"}) {
    require(content(result,filename).find("BILL OF MATERIALS")!=std::string::npos,std::string(filename)+" contains the BOM table");
    require(content(result,filename).find("PLT-01")!=std::string::npos,std::string(filename)+" contains provided metadata");
  }
  require(content(result,"front.dxf").find("\n8\nBALLOONS\n")!=std::string::npos,"DXF emits balloon entities on BALLOONS");
  const auto json=parse_json(content(result,"bom.json"));
  require(json.at("bom")==result.at("bom") && json.at("revision")==1 && json.at("document_id")=="assembly","BOM JSON sidecar preserves identity and table data");
  require(content(result,"bom.csv").find("\"Plate, \"\"A\"\"\"")!=std::string::npos,"CSV quotes commas and embedded quotes");
  require(source==model() && drawing==recipe(),"Rendering preserves model and parameterized recipe");
  auto exploded=drawing;exploded["views"][0]["explode"]=Json::array({{{"part_id","left"},{"translation",Json::array({0,0,parameter("gap")})}}});
  const auto separated=render(source,exploded);near(balloon(separated,"left").at("anchor_mm")[1],15);
  near(balloon(separated,"right").at("anchor_mm")[1],5);
  auto formats=drawing;formats["formats"]={"dxf"};const auto dxf_only=render(source,formats);
  require(dxf_only.at("files").size()==3,"DXF-only drawing still provides independent JSON/CSV BOM tables");
}
void invalid_tests() {
  const auto source=model(), drawing=recipe();auto bad=drawing;bad["bom"]=false;
  fails("invalid_drawing",[&]{normalize_drawing(bad,source);});
  bad=drawing;bad["balloons"].push_back(bad["balloons"][0]);
  fails("invalid_drawing",[&]{normalize_drawing(bad,source);});
  bad=drawing;bad["balloons"][0]["part_id"]="missing";
  fails("invalid_drawing",[&]{normalize_drawing(bad,source);});
  bad=drawing;bad["balloons"][0]["view"]="missing";
  fails("invalid_drawing",[&]{normalize_drawing(bad,source);});
  bad=drawing;bad["views"][0]["orientation"]="section";bad["views"][0]["section"]={{"axis","z"},{"offset",1}};
  fails("invalid_drawing",[&]{normalize_drawing(bad,source);});
  bad=drawing;bad["balloons"][0]["anchor"]={3,1,1};
  fails("selection_missing",[&]{render(source,bad);});
  bad=drawing;bad["balloons"][0]["anchor"]={3,10,1};
  fails("invalid_drawing",[&]{render(source,bad);});
  auto coincident=source;coincident["features"][2]["parts"][2]["placement"]=coincident["features"][2]["parts"][1]["placement"];
  fails("selection_ambiguous",[&]{render(coincident,drawing);});
  bad=drawing;bad["balloons"][1]["label"]=bad["balloons"][0]["label"];
  fails("invalid_drawing",[&]{render(source,bad);});
  bad=drawing;bad["balloons"][0]["label"]={3,1};
  fails("invalid_drawing",[&]{render(source,bad);});
  bad=drawing;bad["scale"]=1000;
  fails("invalid_drawing",[&]{render(source,bad);});
  auto single=source;single["output"]="plate";
  fails("invalid_drawing",[&]{normalize_drawing(drawing,single);});
}
void dimension_collision_test() {
  const auto source=model();
  Json drawing={{"bom",true},{"sheet","A3"},{"layout","grid"},{"scale",2},
    {"views",Json::array({{{"id","top"},{"orientation","top"}}})},
    {"balloons",Json::array({{{"view","top"},{"part_id","base"},{"anchor",{3,3,2}},{"label",{-15,0}}}})}};
  // The top anchor is visible and the circle lies well clear of geometry. A
  // 270-degree, 15-mm-radius dimension around (0,0) crosses its center (-15,0).
  require(render(source,drawing).at("balloons").size()==1,"Balloon alone fits outside the geometry bounds");
  const Json angle={{"view","top"},{"kind","angular"},{"sweep","major"},{"arc_radius",15},
    {"lines",Json::array({{{"from",{0,0}},{"to",{20,0}}},{{"from",{0,0}},{"to",{0,10}}}})}};
  drawing["dimensions"]=Json::array({angle});auto dimension_only=drawing;dimension_only.erase("balloons");
  near(render(source,dimension_only).at("dimensions")[0].at("value_deg"),270);
  fails("invalid_drawing",[&]{render(source,drawing);});
}
void cache_tests() {
  Temp temp;const auto source=model();auto drawing=recipe();Json diagnostic;
  const auto cold=evaluate_model(temp.path,source,worker_request(source,drawing),&diagnostic);
  require(!diagnostic.at("geometry_hit") && !diagnostic.at("projection_hit"),"First BOM drawing builds and projects");
  require(evaluate_model(temp.path,source,worker_request(source,drawing),&diagnostic)==cold,"Warm BOM drawings preserve exact bytes");
  require(diagnostic.at("geometry_hit") && diagnostic.at("projection_hit"),"Repeated BOM drawing hits both caches");
  drawing["balloons"][0]["label"]={-20,1};evaluate_model(temp.path,source,worker_request(source,drawing),&diagnostic);
  require(diagnostic.at("projection_hit"),"Balloon label movement rerenders existing exact projection");
  drawing["balloons"][0]["anchor"]={4,0,1};const auto changed=evaluate_model(temp.path,source,worker_request(source,drawing),&diagnostic);
  require(diagnostic.at("geometry_hit") && !diagnostic.at("projection_hit"),"Anchor edits invalidate visibility and attachment projections");
  near(balloon(changed.at("drawing"),"base").at("anchor_mm")[0],4);
}
void service_tests() {
  Temp temp;Service service(temp.path);call(service,"cad_create",{{"document_id","assembly"},{"model",model()}});
  const auto original=call(service,"cad_read",{{"document_id","assembly"}});
  const auto first=call(service,"cad_drawing",{{"document_id","assembly"},{"revision",1},{"drawing",recipe()}});
  const auto saved=parse_json(read_text(path_from_utf8(first.at("recipe_path"))));
  require(saved.at("drawing")==recipe(),"Saved balloon recipe retains parameterized source anchors");
  std::map<fs::path,std::string> files;for(const auto& a:first.at("artifacts")) {const auto p=path_from_utf8(a.at("path"));files[p]=read_text(p);}
  const auto independent=call(service,"cad_bom",{{"document_id","assembly"},{"revision",1}});
  require(independent.at("bom")==first.at("bom"),"Standalone BOM and drawing table match at the same revision");
  require(independent.at("artifacts").size()==2,"Standalone BOM exports JSON and CSV");
  call(service,"cad_apply",{{"document_id","assembly"},{"expected_revision",1},{"operations",Json::array({
    {{"op","set_parameter"},{"name","post_height"},{"value",10}},
    {{"op","set_part_placement"},{"assembly_id","assembly"},{"part_id","left"},{"placement",{{"translation",{6,3,4}}}}},
    {{"op","set_bom_item"},{"assembly_id","assembly"},{"item",{{"input","plate"},{"item_number",2},{"description","Edited plate"}}}}
  })}});
  Service reopened(temp.path);const auto regenerated=call(reopened,"cad_drawing",{{"document_id","assembly"},{"revision",2},{"drawing",saved.at("drawing")}});
  near(balloon(regenerated,"left").at("anchor_mm")[0],8);near(balloon(regenerated,"left").at("anchor_mm")[1],9);
  require(balloon(regenerated,"base").at("item_number")==2,"Metadata item edits regenerate balloon numbers");
  require(regenerated.at("bom").at("items")[1].at("description")=="Edited plate","Regenerated table includes edited metadata");
  for(const auto& [path,bytes]:files) require(read_text(path)==bytes,"Historical BOM drawings and sidecars remain unchanged");
  require(call(reopened,"cad_read",{{"document_id","assembly"},{"revision",1}})==original,"Historical source remains unchanged");
  auto candidate=call(reopened,"cad_read",{{"document_id","assembly"}}).at("model").at("features")[2];candidate["parts"].erase(2);
  call(reopened,"cad_apply",{{"document_id","assembly"},{"expected_revision",2},{"operations",Json::array({
    {{"op","replace_feature"},{"id","assembly"},{"feature",candidate}},
    {{"op","remove_bom_item"},{"assembly_id","assembly"},{"input","plate"}}
  })}});
  const auto edited=call(reopened,"cad_bom",{{"document_id","assembly"},{"revision",3}}).at("bom");
  require(edited.at("total_quantity")==2 && edited.at("items")[1].at("quantity")==1,"Membership changes derive new quantities");
  require(edited.at("items")[0].at("input")=="plate" && edited.at("items")[0].at("item_number")==1 && !edited.at("items")[0].contains("description"),"Removing metadata restores automatic numbering without removing the row");
  call(reopened,"cad_job",{{"action","submit"},{"request_id","bom_drawing"},{"tool","cad_drawing"},{"arguments",{{"document_id","assembly"},{"revision",2},{"drawing",saved.at("drawing")}}}});
  Json done;for(int i=0;i<3000;++i) {done=call(reopened,"cad_job",{{"action","get"},{"job_id","bom_drawing"}});if(done.at("state")=="succeeded" || done.at("state")=="failed") break;std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  require(done.at("state")=="succeeded","BOM/balloon drawing succeeds through durable asynchronous jobs");
  require(done.at("result").at("bom")==regenerated.at("bom"),"Asynchronous historical revision uses its original table");
}
void example_test() {
  const auto source=parse_json(read_text(fs::path(CAD_SOURCE_DIR)/"examples/assembly.create.json")).at("model");
  const auto drawing=parse_json(read_text(fs::path(CAD_SOURCE_DIR)/"examples/assembly-bom.drawing.json")).at("drawing");
  const auto result=render(source,drawing);
  require(result.at("bom").at("total_quantity")==4 && result.at("bom").at("items").size()==2,"Example groups four instances into plate/spacer rows");
  require(result.at("balloons").size()==4,"Example labels each exploded part instance");
  require(content(result,"drawing.pdf").find("BILL OF MATERIALS")!=std::string::npos,"Example native PDF includes table");
}
}
int main() {
  try {configure_kernel_logging();set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));drawing_tests();invalid_tests();dimension_collision_test();cache_tests();service_tests();example_test();
    std::cout<<"bom_drawing: "<<checks<<" checks passed\n";return 0;
  } catch(const std::exception& e) {std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;}
}
