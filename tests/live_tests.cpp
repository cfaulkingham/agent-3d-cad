#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/app.hpp"
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <set>
#include <thread>
#include <vector>

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
// Reserved Windows device names are rejected as document/view/request IDs, but
// they stay valid model-internal names (parts, features), and a legacy document
// directory that now fails identifier() must not break listing the others.
void device_name_tests() {
  Temporary temporary;Service service(temporary.path);auto model=box();
  model["features"].push_back({{"id","assembly"},{"type","assembly"},{"parts",Json::array({
    {{"id","aux"},{"input","base"}},{{"id","nul"},{"input","base"},{"placement",{{"translation",{30,0,0}}}}}})}});
  model["output"]="assembly";
  call(service,"cad_create",{{"document_id","assembly"},{"model",model}});
  call(service,"cad_open",{{"document_id","assembly"},{"view_id","devices"}});
  const auto shown=ready(service,"devices");
  const auto saved=call(service,"cad_viewer",{{"action","context"},{"view_id","devices"},{"evaluation_id",shown.at("evaluation_id")},
    {"selection",nullptr},{"hidden_part_ids",Json::array({"aux"})}});
  require(saved.at("hidden_part_ids")==Json::array({"aux"}),"A part named like a device can be hidden");
  fails("invalid_argument",[&]{call(service,"cad_open",{{"document_id","assembly"},{"view_id","con"}});});
  const auto legacy=temporary.path/"documents"/"con";directory(legacy);
  atomic_text(legacy/"HEAD.json",Json{{"revision",1}}.dump());
  require(call(service,"cad_list",Json::object()).at("documents")==Json::array({Json{{"document_id","assembly"},{"revision",1}}}),
    "Listing skips a legacy document whose directory name is no longer a valid identifier");
}
void visibility_tests() {
  Temporary temporary;Service service(temporary.path);auto model=box();
  const Json assembly={{"id","assembly"},{"type","assembly"},{"parts",Json::array({
    {{"id","alpha"},{"input","base"}},
    {{"id","beta"},{"input","base"},{"placement",{{"translation",{30,0,0}}}}},
    {{"id","gamma"},{"input","base"},{"placement",{{"translation",{60,0,0}}}}}
  })}};
  model["features"].push_back(assembly);model["output"]="assembly";
  call(service,"cad_create",{{"document_id","assembly"},{"model",model}});
  call(service,"cad_create",{{"document_id","other"},{"model",box()}});
  call(service,"cad_open",{{"document_id","assembly"},{"view_id","visibility"}});
  auto shown=ready(service,"visibility");const auto initial=shown.at("evaluation_id");
  const auto full=chunks(service,"visibility",initial);
  require(shown.at("hidden_part_ids").empty(),"Assembly starts with all parts visible");
  auto pick_for=[&](const std::string& part) {
    for (const auto& face:full.at("topology").at("faces")) if(face.at("part_id")==part)
      return Json{{"document_id","assembly"},{"revision",1},{"evaluation_id",initial},{"feature_id","assembly"},{"kind","face"},{"entity_id",face.at("id")}};
    throw std::runtime_error("Missing fixture part face");
  };
  const auto alpha=pick_for("alpha"),beta=pick_for("beta");
  auto context_args=[&](Json selection,Json hidden) {return Json{{"action","context"},{"view_id","visibility"},{"evaluation_id",initial},{"selection",selection},{"hidden_part_ids",hidden}};};
  auto saved=call(service,"cad_viewer",context_args(nullptr,Json::array({"beta","gamma"})));
  require(saved.at("hidden_part_ids")==Json::array({"beta","gamma"}),"Hide and isolate state reports exact part IDs");
  require(call(service,"cad_read",{{"document_id","assembly"}}).at("model")==model,"Visibility never edits source intent");
  Service reopened(temporary.path);
  require(call(reopened,"cad_context",{{"view_id","visibility"}})==saved,"Hidden parts persist across service restart");
  auto unchanged=ready(reopened,"visibility",initial.get<std::string>());
  require(!unchanged.at("changed").get<bool>()&&unchanged.at("hidden_part_ids")==saved.at("hidden_part_ids"),"Known ready sync still carries visibility state");
  auto omitted=context_args(alpha,Json::array());omitted.erase("hidden_part_ids");
  saved=call(reopened,"cad_viewer",omitted);
  require(saved.at("hidden_part_ids")==Json::array({"beta","gamma"})&&saved.at("selection")==alpha,"Omitted visibility retains mask and allows visible-part selection");
  for (const Json invalid:Json::array({Json("beta"),Json::array({"beta","beta"}),Json::array({"missing"}),Json::array({4}),Json::array({"../bad"})}))
    fails("invalid_argument",[&]{call(reopened,"cad_viewer",context_args(nullptr,invalid));});
  fails("invalid_argument",[&]{call(reopened,"cad_viewer",context_args(nullptr,Json(std::vector<std::string>(65,"beta"))));});
  fails("invalid_argument",[&]{call(reopened,"cad_viewer",context_args(beta,Json::array({"beta"})));});
  auto omitted_hidden=context_args(beta,Json::array());omitted_hidden.erase("hidden_part_ids");
  fails("invalid_argument",[&]{call(reopened,"cad_viewer",omitted_hidden);});
  fails("invalid_argument",[&]{call(reopened,"cad_viewer",context_args(alpha,Json::array({"alpha"})));});
  require(call(reopened,"cad_context",{{"view_id","visibility"}})==saved,"Invalid masks and hidden selections roll back context and presentation together");
  auto visible=call(reopened,"cad_viewer",context_args(beta,Json::array()));
  require(visible.at("hidden_part_ids").empty()&&visible.at("selection")==beta,"Show all and select formerly hidden part atomically");
  call(reopened,"cad_viewer",context_args(nullptr,Json::array({"alpha","beta","gamma"})));
  require(chunks(reopened,"visibility",initial)==full,"Hiding all parts does not rewrite frozen geometry or topology");
  call(reopened,"cad_viewer",context_args(alpha,Json::array({"beta","gamma"})));
  call(reopened,"cad_show",{{"document_id","assembly"},{"view_id","visibility"}});
  require(call(reopened,"cad_context",{{"view_id","visibility"}}).at("hidden_part_ids")==Json::array({"beta","gamma"}),"Showing the same document retains hidden parts");
  fails("invalid_model",[&]{call(reopened,"cad_apply",{{"document_id","assembly"},{"expected_revision",1},
    {"operations",Json::array({{{"op","set_parameter"},{"name","height"},{"value",-1}}})}});});
  require(ready(reopened,"visibility").at("hidden_part_ids")==Json::array({"beta","gamma"}),"Rejected model edit preserves visibility");
  call(reopened,"cad_apply",{{"document_id","assembly"},{"expected_revision",1},
    {"operations",Json::array({{{"op","set_parameter"},{"name","height"},{"value",8}}})}});
  auto stale=call(reopened,"cad_context",{{"view_id","visibility"}});
  require(stale.at("stale").get<bool>()&&stale.at("hidden_part_ids")==Json::array({"beta","gamma"}),"Stale saved pick retains current displayed presentation state");
  fails("stale_selection",[&]{call(reopened,"cad_viewer",context_args(nullptr,Json::array()));});
  shown=ready(reopened,"visibility");
  require(shown.at("revision")==2&&shown.at("hidden_part_ids")==Json::array({"beta","gamma"}),"Same-document revision preserves existing part visibility");
  auto reduced=assembly;reduced["parts"].erase(2);
  call(reopened,"cad_apply",{{"document_id","assembly"},{"expected_revision",2},
    {"operations",Json::array({{{"op","replace_feature"},{"id","assembly"},{"feature",reduced}}})}});
  require(call(reopened,"cad_context",{{"view_id","visibility"}}).at("hidden_part_ids")==Json::array({"beta","gamma"}),"Removed part remains masked until new display publishes");
  shown=ready(reopened,"visibility");
  require(shown.at("revision")==3&&shown.at("hidden_part_ids")==Json::array({"beta"}),"New display prunes only disappeared part IDs");
  stale=call(reopened,"cad_context",{{"view_id","visibility"}});
  require(stale.at("stale").get<bool>()&&stale.at("revision")==1&&stale.at("hidden_part_ids")==Json::array({"beta"}),"Old pick cannot overwrite current display visibility");
  call(reopened,"cad_show",{{"document_id","other"},{"view_id","visibility"}});
  require(call(reopened,"cad_context",{{"view_id","visibility"}}).at("hidden_part_ids").empty(),"Retargeting a document clears visibility immediately");
  require(ready(reopened,"visibility").at("hidden_part_ids").empty(),"Single-part view has empty hidden-part state");
  call(reopened,"cad_show",{{"document_id","assembly"},{"view_id","visibility"}});
  shown=ready(reopened,"visibility");require(shown.at("hidden_part_ids").empty(),"Returning to document does not revive prior view masks");
  call(reopened,"cad_viewer",{{"action","context"},{"view_id","visibility"},{"evaluation_id",shown.at("evaluation_id")},{"selection",nullptr},{"hidden_part_ids",Json::array({"beta"})}});
  call(reopened,"cad_apply",{{"document_id","assembly"},{"expected_revision",3},
    {"operations",Json::array({{{"op","set_output"},{"feature_id","base"}}})}});
  shown=ready(reopened,"visibility");require(shown.at("hidden_part_ids").empty(),"Switching output to a solid prunes assembly visibility");
  fails("invalid_argument",[&]{call(reopened,"cad_viewer",{{"action","context"},{"view_id","visibility"},{"evaluation_id",shown.at("evaluation_id")},{"selection",nullptr},{"hidden_part_ids",Json::array({"beta"})}});});
}
std::set<std::string> json_files(const fs::path& directory) {
  std::set<std::string> names;
  if (!fs::exists(directory)) return names;
  for (const auto& entry:fs::directory_iterator(directory)) if (entry.path().extension()==".json") names.insert(path_to_utf8(entry.path().stem()));
  return names;
}
void age(const fs::path& path) { fs::last_write_time(path,fs::file_time_type::clock::now()-std::chrono::hours(2)); }
void retention_tests() {
  Temporary temporary;Service service(temporary.path);
  const auto frozen=temporary.path/"views"/"kept"/"evaluations",global=temporary.path/"evaluations";
  call(service,"cad_create",{{"document_id","part"},{"model",box()}});
  call(service,"cad_open",{{"document_id","part"},{"view_id","idle"}});
  const auto idle=ready(service,"idle").at("evaluation_id").get<std::string>();
  call(service,"cad_open",{{"document_id","part"},{"view_id","kept"}});
  auto previous=ready(service,"kept").at("evaluation_id").get<std::string>();
  for (int revision=1;revision<=5;++revision) {
    edit(service,revision,6+revision);
    const auto current=ready(service,"kept",previous).at("evaluation_id").get<std::string>();
    require(json_files(frozen)==std::set<std::string>{current},"A view keeps only its displayed frozen evaluation");
    require(!fs::exists(global/(previous+".json"))&&fs::exists(global/(current+".json")),"Publishing a revision removes the superseded live metadata only");
    fails("stale_selection",[&]{chunks(service,"kept",previous);});
    previous=current;
  }
  require(json_files(global)==std::set<std::string>{previous,idle},"Five revisions leave the displayed evaluation plus another view's referenced display");
  const auto mesh=chunks(service,"kept",previous);
  const Json pick={{"document_id","part"},{"revision",6},{"evaluation_id",previous},{"feature_id","base"},{"kind","edge"},{"entity_id",mesh.at("topology").at("edges")[0].at("id")}};
  require(call(service,"cad_viewer",{{"action","context"},{"view_id","kept"},{"evaluation_id",previous},{"selection",pick}}).contains("resolved_selection"),"Retained metadata still resolves the displayed pick");
  // Retargeting releases the frozen mesh at once; current-revision metadata stays resolvable.
  call(service,"cad_create",{{"document_id","second"},{"model",box()}});
  call(service,"cad_show",{{"document_id","second"},{"view_id","kept"}});
  require(json_files(frozen).empty(),"Retargeting deletes the former frozen mesh");
  require(call(service,"cad_resolve_selection",pick).at("reference")==pick,"A retargeted view's current-revision pick remains resolvable");
  const auto current_query=call(service,"cad_query",{{"document_id","second"},{"revision",1},{"kind","topology"}}).at("evaluation_id").get<std::string>();
  const auto old_query=call(service,"cad_query",{{"document_id","part"},{"revision",6},{"kind","topology"}}).at("evaluation_id").get<std::string>();
  edit(service,6,20);
  for (const auto& entry:fs::directory_iterator(global)) age(entry.path());
  ready(service,"kept");
  const auto remaining=json_files(global);
  require(!remaining.contains(old_query)&&!remaining.contains(previous),"Old superseded metadata no view references is swept after publication");
  require(remaining.contains(idle),"A superseded evaluation stays while a view still displays it");
  require(remaining.contains(current_query),"Unsuperseded agent metadata is never swept by age");
  fails("stale_selection",[&]{call(service,"cad_resolve_selection",pick);});
  // The sweep is throttled: a publication soon after a sweep leaves old files
  // for the next one, while the view's own superseded display goes at once.
  const auto late=call(service,"cad_query",{{"document_id","part"},{"revision",6},{"kind","topology"}}).at("evaluation_id").get<std::string>();
  age(global/(late+".json"));
  const auto refreshed=ready(service,"idle").at("evaluation_id").get<std::string>();
  require(json_files(global).contains(late),"A recent sweep defers the next one");
  require(json_files(temporary.path/"views"/"idle"/"evaluations")==std::set<std::string>{refreshed},"The idle view keeps one frozen evaluation after refreshing");
  require(!json_files(global).contains(idle),"Refreshing the idle view removes its superseded display");
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
  require(definitions.size()==19,"legacy, drawing, BOM and five live tools remain published");
  for(const auto& tool:definitions) {
    if(tool.at("name")=="cad_open")require(tool.at("_meta").at("ui").at("resourceUri")==viewer_app_uri,"open tool advertises MCP App resource");
    if(tool.at("name")=="cad_show")require(!tool.contains("_meta"),"show updates existing view without opening another app");
    if(tool.at("name")=="cad_viewer")require(tool.at("_meta").at("ui").at("visibility")==Json::array({"app"}),"viewer plumbing advertises app-only visibility");
  }
  visibility_tests();
  device_name_tests();
  retention_tests();
  std::cout<<"live: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAILED: "<<error.what()<<'\n';return 1;}}
