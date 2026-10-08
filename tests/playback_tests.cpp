#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/hash.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <thread>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
void near(double a,double b){require(std::abs(a-b)<1e-6,"Expected "+std::to_string(b)+", got "+std::to_string(a));}
void fails(const std::string& code,const std::function<void()>& action){try{action();}catch(const Error& error){require(error.code==code,"Expected "+code+", got "+error.code+": "+error.what());return;}throw std::runtime_error("Expected "+code);}
struct Temporary {
  fs::path path=fs::temp_directory_path()/path_from_utf8("agentcad-playback-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  Temporary(){directory(path);}~Temporary(){std::error_code ignored;fs::remove_all(path,ignored);}
};
Json call(Service& service,const std::string& tool,const Json& arguments){
  for(int i=0;;++i){try{return service.call(tool,arguments);}catch(const Error& error){if(error.code!="workspace_busy"||i==1000)throw;}std::this_thread::sleep_for(std::chrono::milliseconds(5));}
}
Json ready(Service& service){
  for(int i=0;i<3000;++i){const auto result=call(service,"cad_viewer",{{"action","sync"},{"view_id","review"}});if(result.at("state")=="ready")return result;if(result.at("state")!="loading")throw std::runtime_error(result.dump());std::this_thread::sleep_for(std::chrono::milliseconds(5));}throw std::runtime_error("View did not become ready");
}
Json presentation(double distance=0,double offset=8){
  const Json clip={{"normal",{0,0,1}},{"offset_mm",offset},{"keep","positive"}};
  const Json directions=Json::array({Json{{"part_id","lever"},{"direction",{1,0,0}}}});
  return {{"clip",clip},{"explode",Json{{"distance_mm",distance},{"directions",directions}}}};
}
Json frame(double time,double angle,double travel,double distance=0){return {{"time_s",time},{"presentation",presentation(distance)},{"joints",Json::array({{{"assembly_id","mechanism"},{"values",Json::array({{{"mate_id","hinge"},{"coordinate","angle_deg"},{"value",angle}},{{"mate_id","spindle_joint"},{"coordinate","travel_mm"},{"value",travel}}})}}})}};}
double coordinate(const Json& shown,const std::string& mate,const std::string& coordinate){for(const auto& dof:shown.at("summary").at("assembly").at("motion").at("dofs"))if(dof.at("mate_id")==mate&&dof.at("coordinate")==coordinate)return dof.at("value");throw std::runtime_error("Missing coordinate");}
void playback(){
  Temporary temporary;Service service(temporary.path);const auto fixture=parse_json(read_text(fs::path(CAD_SOURCE_DIR)/"examples/articulated-arm.create.json"));call(service,"cad_create",fixture);
  call(service,"cad_open",{{"document_id","articulated_arm"},{"view_id","review"}});auto shown=ready(service);auto eid=shown.at("evaluation_id");
  const auto head_path=temporary.path/"documents/articulated_arm/HEAD.json",state_path=temporary.path/"views/review/state.json";
  const auto original_head=read_text(head_path);const auto original_model=call(service,"cad_read",{{"document_id","articulated_arm"}}).at("model");
  const auto action=[&](const std::string& operation,const Json& fields=Json::object()){Json args={{"action","sequence"},{"operation",operation},{"view_id","review"},{"evaluation_id",eid}};args.update(fields);return call(service,"cad_viewer",args);};
  Json sequence={{"name","Coordinated review"},{"frames",Json::array({frame(0,0,0,0),frame(2,90,18,20)})}};
  std::reverse(sequence["frames"][1]["joints"][0]["values"].begin(),sequence["frames"][1]["joints"][0]["values"].end());
  auto saved=action("save",{{"sequence",sequence}});const auto source=saved.at("sequences")[0].at("source");
  require(source.at("revision")==1&&source.at("evaluation_id")==eid&&source.at("feature_id")=="mechanism"&&source.at("model_sha256")==sha256(original_model.dump()),"Saved keyframes bind to complete committed source identity");
  const auto mesh=call(service,"cad_query",{{"document_id","articulated_arm"},{"revision",1},{"kind","mesh"}});
  const Json pick={{"document_id","articulated_arm"},{"revision",1},{"evaluation_id",eid},{"feature_id","mechanism"},{"kind","face"},{"entity_id",mesh.at("topology").at("faces")[0].at("id")}};
  call(service,"cad_viewer",{{"action","context"},{"view_id","review"},{"evaluation_id",eid},{"selection",pick},{"presentation",presentation()}});
  saved=action("options",{{"name",sequence.at("name")},{"speed",1.5},{"loop",true}});require(saved.at("playback").at("state")=="unapplied"&&saved.at("playback").at("speed")==1.5&&saved.at("playback").at("loop")==true,"Options select a sequence without claiming a displayed sample");
  auto admission=action("seek",{{"name",sequence.at("name")},{"time_s",1}});require(admission.at("state")=="loading"||admission.at("state")=="ready","Native seek admits bounded draft work");
  require(call(service,"cad_context",{{"view_id","review"}}).at("selection").is_null(),"Native seek clears the old committed pick before geometry publication");
  if(admission.at("state")=="loading")require(call(service,"cad_context",{{"view_id","review"}}).at("playback").at("state")=="pending","Unevaluated sample never claims a displayed time");
  shown=ready(service);eid=shown.at("evaluation_id");require(shown.at("draft")==true&&shown.at("playback").at("time_s")==1&&shown.at("playback").at("state")=="displayed","Completed native draft publishes its displayed time");
  near(coordinate(shown,"hinge","angle_deg"),45);near(coordinate(shown,"rail","travel_mm"),4.5);near(coordinate(shown,"spindle_joint","angle_deg"),-22.5);near(coordinate(shown,"spindle_joint","travel_mm"),9);near(shown.at("presentation").at("explode").at("distance_mm"),10);
  require(shown.at("summary").at("solid_count")==4&&shown.at("preview_operations").size()==2,"Coordinated keyframes evaluate native solids with independent coordinates only");
  auto context=action("list");require(context.at("evaluation_id")==eid&&!context.at("stale").get<bool>()&&context.at("draft").get<bool>(),"Sequence list after native seek responds with current draft headers");
  fails("stale_selection",[&]{action("save",{{"sequence",sequence}});});
  Service reopened(temporary.path);auto reopened_context=call(reopened,"cad_context",{{"view_id","review"}});require(reopened_context.at("playback").at("time_s")==1&&reopened_context.at("playback").at("speed")==1.5&&!reopened_context.contains("playing"),"Native restart preserves sample and options without starting a clock");
  action("seek",{{"name",sequence.at("name")},{"time_s",2}});shown=ready(service);eid=shown.at("evaluation_id");near(coordinate(shown,"hinge","angle_deg"),90);near(coordinate(shown,"spindle_joint","travel_mm"),18);
  call(service,"cad_viewer",{{"action","motion_reset"},{"view_id","review"},{"evaluation_id",eid}});shown=ready(service);eid=shown.at("evaluation_id");require(shown.at("playback").is_null(),"Manual motion reset retires the timeline position");
  // Invalid definitions must preserve persisted view state and source HEAD.
  const auto before_invalid=read_text(state_path);
  for(int mode=0;mode<13;++mode){auto bad=sequence;std::string code="invalid_argument";
    if(mode==0)bad["frames"][0]["time_s"]=.1;
    if(mode==1)bad["frames"][1]["time_s"]=0;
    if(mode==2)bad["frames"][1]["time_s"]=3601;
    if(mode==3)bad["frames"][1]["joints"][0]["values"].erase(0);
    if(mode==4)bad["frames"][1]["joints"][0]["values"].push_back(bad["frames"][1]["joints"][0]["values"][0]);
    if(mode==5)bad["frames"][1]["joints"][0]["assembly_id"]="missing";
    if(mode==6)bad["frames"][1]["presentation"]["clip"]["normal"]={1,0,0};
    if(mode==7)bad["frames"][1]["presentation"]["explode"]["directions"][0]["part_id"]="missing";
    if(mode==8)bad["frames"][1]["joints"][0]["values"][0]["mate_id"]="rail";
    if(mode==9)bad["script"]="model JavaScript is forbidden";
    if(mode==10){bad["frames"][1]["joints"][0]["values"][1]["value"]=101;code="invalid_model";}
    if(mode==11){bad["frames"]=Json::array({bad["frames"][0]});code="limit_exceeded";}
    if(mode==12)bad["frames"][1]["joints"]=Json::array();
    fails(code,[&]{action("save",{{"sequence",bad}});});
  }
  fails("invalid_argument",[&]{action("seek",{{"name",sequence.at("name")},{"time_s",-1}});});fails("invalid_argument",[&]{action("seek",{{"name",sequence.at("name")},{"time_s",2.01}});});
  fails("invalid_argument",[&]{action("options",{{"name",sequence.at("name")},{"speed",4.01}});});fails("invalid_argument",[&]{action("options",{{"name",sequence.at("name")},{"loop",1}});});
  require(read_text(state_path)==before_invalid&&read_text(head_path)==original_head,"Invalid keyframes, seeks and options preserve persisted state atomically");
  // Pure presentation sequences retain exact native sections only for matching geometry.
  Json visual={{"name","Exploded presentation"},{"frames",Json::array({frame(0,0,0,0),frame(2,0,0,20)})}};for(auto& f:visual["frames"])f["joints"]=Json::array();visual["frames"][1]["presentation"]["clip"]["offset_mm"]=12;
  action("save",{{"sequence",visual}});call(service,"cad_viewer",{{"action","context"},{"view_id","review"},{"evaluation_id",eid},{"selection",nullptr},{"presentation",presentation()}});
  auto section=call(service,"cad_viewer",{{"action","section"},{"view_id","review"},{"evaluation_id",eid},{"query",{{"action","section"},{"plane",{{"normal",{0,0,1}},{"offset_mm",8}}}}}});const auto job=section.at("job_id");
  auto pure=action("seek",{{"name",visual.at("name")},{"time_s",0}});require(pure.at("evaluation_id")==eid&&pure.at("section").at("job_id")==job&&pure.at("playback").at("state")=="displayed","Pure presentation seek reuses immutable source evaluation and matching section");
  pure=action("seek",{{"name",visual.at("name")},{"time_s",1}});require(pure.at("evaluation_id")==eid&&!pure.contains("section")&&!pure.value("draft",false),"Changed presentation retires section without creating a source draft");near(pure.at("presentation").at("explode").at("distance_mm"),10);near(pure.at("presentation").at("clip").at("offset_mm"),10);
  require(call(service,"cad_read",{{"document_id","articulated_arm"}}).at("model")==original_model&&read_text(head_path)==original_head,"Every playback operation leaves editable committed source unchanged");
  for(int i=0;i<14;++i){auto extra=visual;extra["name"]="Visual "+std::to_string(i);action("save",{{"sequence",extra}});}require(action("list").at("sequences").size()==16,"Stored sequence count is bounded at sixteen");auto extra=visual;extra["name"]="Overflow";fails("limit_exceeded",[&]{action("save",{{"sequence",extra}});});require(action("save",{{"sequence",visual}}).at("sequences").size()==16,"An existing definition can be replaced at the bound");
  call(service,"cad_apply",{{"document_id","articulated_arm"},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","arm_angle"},{"value",20}}})}});shown=ready(service);eid=shown.at("evaluation_id");require(shown.at("playback").is_null()&&shown.at("sequences").size()==16,"Revision change retires timeline position while preserving qualified definitions");
  auto historical=action("list");require(historical.at("revision")==2&&historical.at("evaluation_id")==eid&&!historical.at("stale").get<bool>()&&historical.at("sequences")[0].at("source").at("revision")==1,"Historical sequence list preserves source pins and current response identity");
  fails("stale_selection",[&]{action("seek",{{"name",visual.at("name")},{"time_s",1}});});fails("stale_selection",[&]{action("options",{{"name",visual.at("name")},{"speed",1}});});
  require(action("delete",{{"name",visual.at("name")}}).at("sequences").size()==15,"Historical definitions can be explicitly deleted without rebinding");
  call(service,"cad_apply",{{"document_id","articulated_arm"},{"expected_revision",2},{"operations",Json::array({{{"op","set_output"},{"feature_id","base"}}})}});shown=ready(service);eid=shown.at("evaluation_id");fails("stale_selection",[&]{action("seek",{{"name",sequence.at("name")},{"time_s",1}});});
  auto other=fixture;other["document_id"]="other";call(service,"cad_create",other);call(service,"cad_show",{{"document_id","other"},{"view_id","review"}});shown=ready(service);require(shown.at("sequences").empty()&&shown.at("playback").is_null(),"Retargeting clears document scoped sequence state");
}
void composed(){
  Temporary temporary;Service service(temporary.path);auto fixture=parse_json(read_text(fs::path(CAD_SOURCE_DIR)/"examples/nested-assembly.create.json"));auto& model=fixture["model"];
  auto second=model["features"][2];second["id"]="second_module";second["mates"][0]["angle_deg"]=0;model["features"].insert(model["features"].begin()+3,second);
  model["features"].back()["parts"].push_back({{"id","extra"},{"input","second_module"},{"placement",{{"translation",{140,0,0}}}}});call(service,"cad_create",fixture);
  call(service,"cad_open",{{"document_id",fixture.at("document_id")},{"view_id","review"}});auto shown=ready(service);const auto eid=shown.at("evaluation_id");const auto original=call(service,"cad_read",{{"document_id",fixture.at("document_id")}});
  auto keyframe=[](double time,double angle,double other){
    const Json visual={{"clip",nullptr},{"explode",{{"distance_mm",0},{"directions",Json::array()}}}};
    const auto scope=[](const char* assembly,double value){return Json{{"assembly_id",assembly},{"values",Json::array({{{"mate_id","pivot"},{"coordinate","angle_deg"},{"value",value}}})}};};
    return Json{{"time_s",time},{"presentation",visual},{"joints",Json::array({scope("module",angle),scope("second_module",other)})}};
  };
  Json sequence={{"name","Composed coordinated review"},{"frames",Json::array({keyframe(0,0,0),keyframe(2,120,60)})}};std::reverse(sequence["frames"][1]["joints"].begin(),sequence["frames"][1]["joints"].end());
  call(service,"cad_viewer",{{"action","sequence"},{"operation","save"},{"view_id","review"},{"evaluation_id",eid},{"sequence",sequence}});
  call(service,"cad_viewer",{{"action","sequence"},{"operation","seek"},{"view_id","review"},{"evaluation_id",eid},{"name",sequence.at("name")},{"time_s",1}});shown=ready(service);
  bool first=false,other=false;for(const auto& mechanism:shown.at("summary").at("assembly").at("mechanisms")){
    if(mechanism.at("assembly_id")=="module"){near(mechanism.at("motion").at("dofs")[0].at("value"),60);require(mechanism.at("occurrences").size()==2,"Repeated source definitions share sampled motion in both occurrences");first=true;}
    if(mechanism.at("assembly_id")=="second_module"){near(mechanism.at("motion").at("dofs")[0].at("value"),30);other=true;}
  }
  require(first&&other&&shown.at("summary").at("solid_count")==7,"Composed seek evaluates both definitions and every native occurrence");require(shown.at("preview_operations").size()==2&&shown.at("playback").at("time_s")==1,"Coordinated definitions publish one complete sampled operation vector");require(call(service,"cad_read",{{"document_id",fixture.at("document_id")}})==original,"Composed native playback leaves committed source and parameters immutable");
}
}
int main(){try{set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));playback();composed();std::cout<<"PASS "<<checks<<" playback checks\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
