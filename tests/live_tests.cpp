#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/app.hpp"
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>

using namespace agentcad;
namespace {
int checks = 0;
void require(bool condition,const std::string& message) { ++checks;if(!condition)throw std::runtime_error(message); }
void fails(const std::string& code,const std::function<void()>& operation) {
  try { operation(); } catch(const Error& error) { require(error.code==code,"Expected "+code+", got "+error.code);return; }
  throw std::runtime_error("Expected "+code);
}
struct Temporary {
  fs::path path;
  Temporary():path(fs::temp_directory_path()/path_from_utf8("agentcad-live-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-零")) { directory(path); }
  ~Temporary(){std::error_code ignored;fs::remove_all(path,ignored);}
};
Json box() {return {{"schema_version",1},{"units","mm"},{"parameters",{{"height",6}}},
  {"features",Json::array({{{"id","base"},{"type","box"},{"size",Json::array({20,10,Json{{"parameter","height"}}})}}})},{"output","base"}};}
Json call(Service& service,const std::string& tool,const Json& arguments) {
  for(int attempt=0;;++attempt) {
    try{return service.call(tool,arguments);}catch(const Error& error){if(error.code!="workspace_busy"||attempt==1000)throw;}
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}
Json ready(Service& service,const std::string& view="main",const std::string& known="") {
  Json args={{"action","sync"},{"view_id",view}};if(!known.empty())args["known_evaluation_id"]=known;
  for(int attempt=0;attempt<3000;++attempt) {
    const auto result=call(service,"cad_viewer",args);
    if(result.at("state")=="ready")return result;
    if(result.at("state")!="loading")throw std::runtime_error("Unexpected view result: "+result.dump());
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  throw std::runtime_error("View did not become ready");
}
Json chunks(Service& service,const std::string& view,const Json& evaluation,std::size_t* chunk_count=nullptr) {
  std::string text;std::size_t count=0;Json offset=0,total;
  do {
    const auto chunk=call(service,"cad_viewer",{{"action","mesh"},{"view_id",view},{"evaluation_id",evaluation},{"offset",offset}});
    require(chunk.at("offset")==offset,"chunk offset preserved");
    const auto data=chunk.at("data").get<std::string>();
    require(data.size()<=128*1024,"mesh transfer bounded to 128 KiB");
    require(std::all_of(data.begin(),data.end(),[](unsigned char c){return c<128;}),"chunk JSON is ASCII for byte-stable assembly");
    text+=data;offset=chunk.at("next_offset");total=chunk.at("total_bytes");++count;
  }while(!offset.is_null());
  require(text.size()==total.get<std::size_t>(),"mesh chunks exactly cover declared bytes");
  if(chunk_count)*chunk_count=count;
  return parse_json(text,64*1024*1024);
}
Json edit(Service& service,int revision,int height) {
  return call(service,"cad_apply",{{"document_id","part"},{"expected_revision",revision},
    {"operations",Json::array({{{"op","set_parameter"},{"name","height"},{"value",height}}})}});
}
}
int main() {try {
  set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));Temporary temporary;Service service(temporary.path);
  const auto empty=call(service,"cad_open",Json::object());
  require(empty.at("view_id")=="main"&&empty.at("document_id").is_null()&&empty.at("resource_uri")==viewer_app_uri,"new default view is empty");
  require(call(service,"cad_list",Json::object()).at("documents").empty(),"new workspace has no saved documents");
  require(call(service,"cad_viewer",{{"action","sync"},{"view_id","main"}}).at("state")=="empty","empty view synchronizes without a job");
  require(call(service,"cad_context",Json::object()).at("selection").is_null(),"new view has no selected entity");
  call(service,"cad_create",{{"document_id","part"},{"model",box()}});
  call(service,"cad_show",{{"document_id","part"}});
  require(call(service,"cad_open",Json::object()).at("document_id")=="part","opening without document preserves existing view");
  const auto loading=call(service,"cad_viewer",{{"action","sync"},{"view_id","main"}});
  require(loading.at("state")=="loading"&&!loading.contains("mesh"),"sync returns loading before isolated geometry completes");
  require(call(service,"cad_read",{{"document_id","part"}}).at("revision")==1,"reads stay available during live view evaluation");
  auto first=ready(service);
  require(first.at("revision")==1&&first.at("changed")==true&&first.contains("model"),"ready view includes current structured model");
  const auto first_id=first.at("evaluation_id").get<std::string>();
  const auto unchanged=ready(service,"main",first_id);
  require(unchanged.at("evaluation_id")==first_id&&unchanged.at("changed")==false&&!unchanged.contains("model"),"known evaluation avoids repeated model and geometry");
  auto evaluation=chunks(service,"main",first_id);
  require(evaluation.at("evaluation_id")==first_id&&evaluation.at("revision")==1,"frozen mesh retains exact evaluation identity");
  require(evaluation.at("mesh").at("triangle_faces").size()==12,"live mesh contains exact box face mapping");
  Json pick={{"document_id","part"},{"revision",1},{"evaluation_id",first_id},{"feature_id","base"},{"kind","edge"},
    {"entity_id",evaluation.at("topology").at("edges")[0].at("id")}};
  const Json camera={{"yaw",0.2},{"pitch",0.4},{"zoom",1.4},{"pan",{20,-10}}};
  const auto selection=call(service,"cad_viewer",{{"action","context"},{"view_id","main"},{"evaluation_id",first_id},{"selection",pick},{"camera",camera},{"prompt","Round this edge — 零"}});
  require(selection.at("stale")==false&&selection.at("selection")==pick&&selection.at("resolved_selection").contains("selector"),"selected edge publishes validated design reference");
  require(selection.at("camera")==camera&&selection.at("prompt")=="Round this edge — 零","camera and Unicode prompt persist");
  Service reopened(temporary.path);
  require(call(reopened,"cad_context",Json::object())==selection,"context survives native service restart");
  require(ready(reopened,"main",first_id).at("evaluation_id")==first_id,"restart reuses frozen current evaluation");
  auto wrong=pick;wrong["feature_id"]="other";
  fails("stale_selection",[&]{call(reopened,"cad_viewer",{{"action","context"},{"view_id","main"},{"evaluation_id",first_id},{"selection",wrong}});});
  wrong=pick;wrong["entity_id"]="edge-999";
  fails("selection_missing",[&]{call(reopened,"cad_viewer",{{"action","context"},{"view_id","main"},{"evaluation_id",first_id},{"selection",wrong}});});
  fails("stale_selection",[&]{call(reopened,"cad_viewer",{{"action","mesh"},{"view_id","main"},{"evaluation_id","eval_absent"}});});
  fails("invalid_argument",[&]{call(reopened,"cad_viewer",{{"action","mesh"},{"view_id","main"},{"evaluation_id",first_id},{"offset",-1}});});
  fails("invalid_argument",[&]{call(reopened,"cad_viewer",{{"action","mesh"},{"view_id","main"},{"evaluation_id",first_id},{"offset",64*1024*1024}});});
  auto invalid_camera=camera;invalid_camera["zoom"]=0;
  fails("invalid_argument",[&]{call(reopened,"cad_viewer",{{"action","context"},{"view_id","main"},{"evaluation_id",first_id},{"selection",nullptr},{"camera",invalid_camera}});});
  fails("limit_exceeded",[&]{call(reopened,"cad_viewer",{{"action","context"},{"view_id","main"},{"evaluation_id",first_id},{"selection",nullptr},{"prompt",std::string(8193,'x')}});});
  require(call(reopened,"cad_context",Json::object())==selection,"rejected context updates preserve saved context");
  auto cleared=call(reopened,"cad_viewer",{{"action","context"},{"view_id","main"},{"evaluation_id",first_id},{"selection",nullptr}});
  require(cleared.at("selection").is_null()&&!cleared.contains("resolved_selection")&&cleared.at("camera")==camera,"null selection clears reference and preserves same-evaluation camera");
  call(reopened,"cad_viewer",{{"action","context"},{"view_id","main"},{"evaluation_id",first_id},{"selection",pick}});
  // Leave a completed older job pending, then edit HEAD before sync consumes it.
  call(reopened,"cad_show",{{"view_id","race"},{"document_id","part"}});
  call(reopened,"cad_viewer",{{"action","sync"},{"view_id","race"}});
  const auto state=parse_json(read_text(temporary.path/"views"/"race"/"state.json"));
  const auto pending=state.at("pending").at("job_id");
  bool finished=false;
  for(int i=0;i<3000&&!finished;++i){const auto job=call(reopened,"cad_job",{{"action","get"},{"job_id",pending}});finished=job.at("state")=="succeeded";if(!finished)std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  require(finished,"older view evaluation completed before HEAD edit");
  edit(reopened,1,8);
  const auto stale=call(reopened,"cad_context",Json::object());
  require(stale.at("stale")==true&&stale.at("head_revision")==2&&stale.at("revision")==1,"saved pick explicitly reports stale after edit");
  fails("stale_selection",[&]{chunks(reopened,"main",first_id);});
  fails("stale_selection",[&]{call(reopened,"cad_viewer",{{"action","context"},{"view_id","main"},{"evaluation_id",first_id},{"selection",pick}});});
  const auto after_race=ready(reopened,"race");
  require(after_race.at("revision")==2&&after_race.at("model").at("parameters").at("height")==8,"late old job cannot replace newer HEAD");
  const auto second=ready(reopened,"main",first_id);
  require(second.at("revision")==2&&second.at("evaluation_id")!=first_id&&second.at("changed")==true,"main view follows new committed revision");
  require(call(reopened,"cad_context",Json::object()).at("stale")==true,"old saved context stays explicitly stale after display refresh");
  const auto reset=call(reopened,"cad_viewer",{{"action","context"},{"view_id","main"},{"evaluation_id",second.at("evaluation_id")},{"selection",nullptr}});
  require(reset.at("stale")==false&&!reset.contains("camera")&&!reset.contains("prompt"),"new-evaluation context does not silently reuse old camera or prompt");
  // A per-view lock excludes its own callers without serializing other views.
  call(reopened,"cad_open",{{"view_id","independent"}});
  { WorkspaceLock held(temporary.path/"views"/"main");
    fails("workspace_busy",[&]{reopened.call("cad_context",Json::object());});
    require(reopened.call("cad_context",{{"view_id","independent"}}).at("document_id").is_null(),"independent view remains readable while main is locked");
  }
  auto many=box();many["features"].push_back({{"id","row"},{"type","pattern"},{"input","base"},{"count",64},{"step",{30,0,0}}});many["output"]="row";
  call(reopened,"cad_create",{{"document_id","many"},{"model",many}});
  call(reopened,"cad_show",{{"view_id","large"},{"document_id","many"}});
  const auto large=ready(reopened,"large");std::size_t chunk_count=0;
  const auto large_mesh=chunks(reopened,"large",large.at("evaluation_id"),&chunk_count);
  require(chunk_count>1&&large_mesh.at("summary").at("solid_count")==64,"large frozen model assembles multiple bounded chunks");
  require(call(reopened,"cad_list",Json::object()).at("documents")==Json::array({Json{{"document_id","many"},{"revision",1}},Json{{"document_id","part"},{"revision",2}}}),"saved document listing is sorted with current HEAD revisions");
  call(reopened,"cad_show",{{"document_id","many"}});
  require(call(reopened,"cad_context",Json::object()).at("selection").is_null(),"retargeting view clears former document selection");
  fails("stale_selection",[&]{chunks(reopened,"main",second.at("evaluation_id"));});
  fails("invalid_argument",[&]{call(reopened,"cad_viewer",{{"action","sync"},{"view_id","../escape"}});});
  fails("invalid_argument",[&]{call(reopened,"cad_list",{{"extra",true}});});
  fails("not_found",[&]{call(reopened,"cad_show",{{"document_id","absent"}});});
  require(call(reopened,"cad_open",Json::object()).at("document_id")=="many","failed show preserves document association");
  // Managed view paths cannot redirect state/artifact writes outside the workspace.
  std::error_code symlink_error;fs::create_directory_symlink(temporary.path/"views"/"main",temporary.path/"views"/"alias",symlink_error);
  if(!symlink_error)fails("storage_error",[&]{call(reopened,"cad_open",{{"view_id","alias"}});});
  const auto definitions=tool_definitions();
  require(definitions.size()==18,"legacy, drawing and five live tools remain published");
  for(const auto& tool:definitions) {
    if(tool.at("name")=="cad_open")require(tool.at("_meta").at("ui").at("resourceUri")==viewer_app_uri,"open tool advertises MCP App resource");
    if(tool.at("name")=="cad_show")require(!tool.contains("_meta"),"show updates existing view without opening another app");
    if(tool.at("name")=="cad_viewer")require(tool.at("_meta").at("ui").at("visibility")==Json::array({"app"}),"viewer plumbing advertises app-only visibility");
  }
  std::cout<<"live: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAILED: "<<error.what()<<'\n';return 1;}}
