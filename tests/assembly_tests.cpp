#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <set>
#include <thread>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message) { ++checks; if(!value) throw std::runtime_error(message); }
void near(double actual,double expected,const std::string& message) { require(std::abs(actual-expected)<1e-6,message); }
struct Temp {
  fs::path path=temporary_file(fs::temp_directory_path());
  Temp() {fs::remove(path);directory(path);}
  ~Temp() {std::error_code ignored;fs::remove_all(path,ignored);}
};
Json call(Service& service,const std::string& tool,const Json& args) {
  for(int i=0;;++i) {
    try{return service.call(tool,args);}
    catch(const Error& error){if(error.code!="workspace_busy"||i==1000)throw;}
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}
void fails(const std::string& code,const std::function<void()>& action) {
  try{action();}catch(const Error& error){require(error.code==code,"Expected "+code+", got "+error.code);return;}
  throw std::runtime_error("Expected "+code);
}
Json fixture() {return parse_json(R"({"schema_version":1,"units":"mm","parameters":{"height":4},"features":[
  {"id":"plate","type":"box","size":[20,10,{"parameter":"height"}]},
  {"id":"pin","type":"box","size":[2,3,5]},
  {"id":"fixture","type":"assembly","parts":[{"id":"base","input":"plate"},{"id":"post","input":"pin"}],
   "mates":[{"id":"seat","type":"rigid","parent":"base","child":"post",
    "parent_frame":{"origin":[4,3,{"parameter":"height"}],"normal":[0,0,1],"x_direction":[1,0,0]},
    "child_frame":{"origin":[0,0,0],"normal":[0,0,1],"x_direction":[1,0,0]}}]}],"output":"fixture"})");}
Json ready(Service& service) {
  for(int i=0;i<2000;++i) {
    auto result=call(service,"cad_viewer",{{"action","sync"},{"view_id","assembly"}});
    if(result.at("state")=="ready")return result;
    require(result.at("state")=="loading","Live assembly should load successfully");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  throw std::runtime_error("Live assembly did not finish");
}
}
int main() {try {
  set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));Temp temp;Service service(temp.path);
  const auto model=fixture();
  auto first=call(service,"cad_create",{{"document_id","assembly"},{"model",model},{"request_id","create_once"}});
  near(first.at("summary").at("volume_mm3"),830,"Assembly volume sums independent solids");
  require(first.at("summary").at("solid_count")==2,"Assembly retains two solids");
  require(first.at("summary").at("assembly").at("parts").size()==2,"Part inventory is returned");
  near(first.at("summary").at("bounds_mm").at("max")[2],9,"Mate places child on plate");
  require(call(service,"cad_create",{{"document_id","assembly"},{"model",model},{"request_id","create_once"}})==first,"Assembly request deduplication retains revision");
  Service reopened(temp.path);
  require(call(reopened,"cad_read",{{"document_id","assembly"}}).at("model")==model,"All editable intent survives reopening");
  auto query=call(reopened,"cad_query",{{"document_id","assembly"},{"revision",1},{"kind","mesh"}});
  std::set<std::string> parts;
  for(const auto& face:query.at("topology").at("faces"))parts.insert(face.at("part_id"));
  require(parts==std::set<std::string>{"base","post"},"Exact faces retain instance ownership");
  for(const auto& edge:query.at("topology").at("edges"))require(edge.contains("part_id")&&!edge.contains("selector"),"Assembly edges expose ownership without an invalid edit selector");
  Json pick={{"document_id","assembly"},{"revision",1},{"evaluation_id",query.at("evaluation_id")},{"feature_id","fixture"},{"kind","face"},{"entity_id",query.at("topology").at("faces")[0].at("id")}};
  auto selected=call(reopened,"cad_resolve_selection",pick);
  require(selected.at("geometry").contains("part_id"),"Resolved selection includes instance identity");
  const Json change=Json::array({{{"op","set_parameter"},{"name","height"},{"value",7}}});
  const auto preview=call(reopened,"cad_preview",{{"document_id","assembly"},{"expected_revision",1},{"operations",change}});
  near(preview.at("summary").at("bounds_mm").at("max")[2],12,"Draft rebuild follows datum parameter");
  require(call(reopened,"cad_read",{{"document_id","assembly"}}).at("revision")==1,"Assembly preview leaves HEAD unchanged");
  auto invalid_mate=model.at("features")[2].at("mates")[0];invalid_mate["parent"]="absent";
  fails("invalid_model",[&]{call(reopened,"cad_apply",{{"document_id","assembly"},{"expected_revision",1},{"operations",Json::array({{{"op","set_mate"},{"assembly_id","fixture"},{"mate",invalid_mate}}})}});});
  require(call(reopened,"cad_read",{{"document_id","assembly"}}).at("model")==model,"Invalid mate preserves source and HEAD");
  auto edited=call(reopened,"cad_apply",{{"document_id","assembly"},{"expected_revision",1},{"operations",change}});
  near(edited.at("summary").at("volume_mm3"),1430,"Source part edit changes the assembly geometry");
  near(edited.at("summary").at("bounds_mm").at("max")[2],12,"Source part edit updates the child placement");
  fails("stale_selection",[&]{call(reopened,"cad_resolve_selection",pick);});
  fails("revision_conflict",[&]{call(reopened,"cad_apply",{{"document_id","assembly"},{"expected_revision",1},{"operations",change}});});
  require(call(reopened,"cad_read",{{"document_id","assembly"},{"revision",1}}).at("model")==model,"Historical part/mate intent remains unchanged");
  auto delta=call(reopened,"cad_compare",{{"document_id","assembly"},{"from_revision",1},{"to_revision",2}});
  near(delta.at("volume_delta_mm3"),600,"Revision comparison measures assembly changes");
  // A mate can be detached and its part explicitly positioned in one transaction.
  edited=call(reopened,"cad_apply",{{"document_id","assembly"},{"expected_revision",2},{"operations",Json::array({
    {{"op","set_part_placement"},{"assembly_id","fixture"},{"part_id","post"},{"placement",{{"translation",{30,0,0}}}}},
    {{"op","remove_mate"},{"assembly_id","fixture"},{"mate_id","seat"}}})}});
  near(edited.at("summary").at("bounds_mm").at("max")[0],32,"Detached instance moves independently");
  const auto step=call(reopened,"cad_export",{{"document_id","assembly"},{"revision",3},{"format","step"}});
  const auto imported=call(reopened,"cad_import",{{"document_id","readback"},{"path",step.at("path")}});
  require(imported.at("summary").at("solid_count")==2,"Independent STEP retains both placed solids");
  near(imported.at("summary").at("volume_mm3"),1430,"STEP readback preserves total volume");
  near(imported.at("summary").at("bounds_mm").at("max")[0],32,"STEP readback preserves placement");
  auto stl=call(reopened,"cad_export",{{"document_id","assembly"},{"revision",3},{"format","stl"}});
  require(fs::file_size(path_from_utf8(stl.at("path")))>84,"Independent STL exported");
  // Live transfer/context round-trip also retains the owning part.
  call(reopened,"cad_open",{{"document_id","assembly"},{"view_id","assembly"}});
  const auto live=ready(reopened);
  require(live.at("summary").at("assembly").at("parts").size()==2,"Live summary includes parts");
  Json offset=0;std::string bytes;
  do {
    const auto chunk=call(reopened,"cad_viewer",{{"action","mesh"},{"view_id","assembly"},{"evaluation_id",live.at("evaluation_id")},{"offset",offset}});
    bytes+=chunk.at("data").get<std::string>();offset=chunk.at("next_offset");
  }while(!offset.is_null());
  const auto payload=parse_json(bytes,64*1024*1024);
  pick["revision"]=3;pick["evaluation_id"]=live.at("evaluation_id");pick["entity_id"]=payload.at("topology").at("faces")[0].at("id");
  const auto context=call(reopened,"cad_viewer",{{"action","context"},{"view_id","assembly"},{"evaluation_id",live.at("evaluation_id")},{"selection",pick}});
  require(context.at("resolved_selection").at("geometry").contains("part_id"),"Live selected part survives context publication");
  const auto restored=call(reopened,"cad_restore",{{"document_id","assembly"},{"expected_revision",3},{"source_revision",1}});
  require(restored.at("revision")==4&&restored.at("model")==model,"Restore recreates editable assembly as new revision");
  auto job=call(reopened,"cad_job",{{"action","submit"},{"request_id","assembly_edit_job"},{"tool","cad_apply"},
    {"arguments",{{"document_id","assembly"},{"expected_revision",4},{"operations",change}}}});
  for(int i=0;i<2000&&job.at("state")!="succeeded";++i) {
    require(job.at("state")=="queued"||job.at("state")=="running","Async assembly edit should succeed: "+job.dump());
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    job=call(reopened,"cad_job",{{"action","get"},{"job_id","assembly_edit_job"}});
  }
  require(job.at("state")=="succeeded"&&job.at("result").at("revision")==5,"Durable worker commits assembly edit");
  near(job.at("result").at("summary").at("volume_mm3"),1430,"Async assembly result is measured");
  std::cout<<checks<<" assembly service checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAILED: "<<error.what()<<'\n';return 1;}}
