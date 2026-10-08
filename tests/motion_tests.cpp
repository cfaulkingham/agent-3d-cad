#include "agentcad/model.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <chrono>
#include <thread>
#include <bit>
#include <limits>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
void near(double a,double b){require(std::abs(a-b)<1e-6,"Expected "+std::to_string(b)+", got "+std::to_string(a));}
Error fails(const std::string& code,const std::function<void()>& action){try{action();}catch(const Error& e){require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what());return e;}throw std::runtime_error("Expected "+code);}
Json plane(Json origin={0,0,0},Json normal={0,0,1},Json x={1,0,0}) {return {{"origin",origin},{"normal",normal},{"x_direction",x}};}
Json fixture() {
  return parse_json(R"({"schema_version":1,"units":"mm","parameters":{"angle":90},"features":[
    {"id":"base","type":"box","size":[10,10,2]},
    {"id":"arm","type":"box","size":[10,2,2]},
    {"id":"tip","type":"box","size":[1,2,3]},
    {"id":"mechanism","type":"assembly","parts":[{"id":"ground","input":"base"},{"id":"lever","input":"arm"},{"id":"carriage","input":"tip"},{"id":"sleeve","input":"tip"}],
    "mates":[
      {"id":"hinge","type":"revolute","parent":"ground","child":"lever","angle_deg":{"parameter":"angle"},"angle_limits_deg":[-180,180],
       "parent_frame":{"origin":[0,0,2],"normal":[0,0,1],"x_direction":[1,0,0]},"child_frame":{"origin":[0,0,0],"normal":[0,0,1],"x_direction":[1,0,0]}},
      {"id":"rail","type":"slider","parent":"lever","child":"carriage","travel_mm":5,"travel_limits_mm":[0,20],
       "parent_frame":{"origin":[10,0,0],"normal":[1,0,0],"x_direction":[0,1,0]},"child_frame":{"origin":[0,0,0],"normal":[0,0,1],"x_direction":[1,0,0]}},
      {"id":"shaft","type":"cylindrical","parent":"ground","child":"sleeve","angle_deg":30,"angle_limits_deg":[-360,360],"travel_mm":7,"travel_limits_mm":[0,10],
       "parent_frame":{"origin":[20,0,0],"normal":[0,0,1],"x_direction":[1,0,0]},"child_frame":{"origin":[0,0,0],"normal":[0,0,1],"x_direction":[1,0,0]}}
    ]}],"output":"mechanism"})");
}
Json part(const Json& summary,const std::string& id){for(const auto& p:summary.at("assembly").at("parts"))if(p.at("id")==id)return p;throw std::runtime_error("Missing part");}
Json value(const std::string& mate,const std::string& coordinate,Json v){return {{"mate_id",mate},{"coordinate",coordinate},{"value",v}};}
Json coupling(const std::string& id,const std::string& source,const std::string& sc,const std::string& target,const std::string& tc,double ratio,double offset=0) {
  return {{"id",id},{"source",{{"mate_id",source},{"coordinate",sc}}},{"target",{{"mate_id",target},{"coordinate",tc}}},{"ratio",ratio},{"offset",offset}};
}
Json coupled() {
  auto doc=fixture();auto& assembly=doc["features"][3];
  assembly["mates"][1].erase("travel_mm");assembly["mates"][1]["travel_limits_mm"]={-20,20};assembly["mates"][2].erase("angle_deg");
  assembly["couplings"]=Json::array({coupling("gear","hinge","angle_deg","shaft","angle_deg",-2,10),
    coupling("lead","hinge","angle_deg","rail","travel_mm",1.0/18)});
  assembly["poses"]=Json::array({{{"id","home"},{"values",Json::array({value("hinge","angle_deg",0),value("shaft","travel_mm",0)})}},
    {{"id","working"},{"values",Json::array({value("hinge","angle_deg",90),value("shaft","travel_mm",7)})}}});
  return doc;
}
void geometry() {
  auto doc=fixture();BuiltModel built(doc);const auto summary=built.summary();
  near(summary.at("volume_mm3"),252);require(summary.at("solid_count")==4,"Moving assembly preserves every solid");
  const auto arm=part(summary,"lever");near(arm.at("bounds_mm").at("min")[0],-2);near(arm.at("bounds_mm").at("max")[1],10);
  const auto tip=part(summary,"carriage").at("transform");near(tip[3],0);near(tip[7],15);near(tip[11],2);
  const auto sleeve=part(summary,"sleeve").at("transform");near(sleeve[3],20);near(sleeve[11],7);near(sleeve[0],std::sqrt(3.0)/2);
  require(summary.at("assembly").at("motion").at("dofs").size()==4,"Cylindrical mate exposes both coordinates");
  const auto restored=BuiltModel(doc,built.snapshot()).summary();near(part(restored,"carriage").at("transform")[7],15);
  // Parent placement and a non-origin child frame are composed, not added in world coordinates.
  doc["features"][3]["parts"][0]["placement"]={{"translation",{100,20,30}},{"rotation",{{"origin",{0,0,0}},{"axis",{0,1,0}},{"angle_deg",90}}}};
  const auto moved=part(BuiltModel(doc).summary(),"carriage").at("transform");near(moved[3],102);near(moved[7],35);near(moved[11],30);
  doc=fixture();doc["features"][3]["mates"][0]["child_frame"]=plane({3,0,0});
  const auto offset=part(BuiltModel(doc).summary(),"lever").at("transform");near(offset[3],0);near(offset[7],-3);near(offset[11],2);
}
void validation() {
  auto doc=coupled();validate_model(doc);const auto motion=assembly_motion(doc["features"][3],doc["parameters"]);
  near(motion.at("dofs")[1].at("value"),5);near(motion.at("dofs")[2].at("value"),-170);
  require(motion.at("dofs")[1].at("driven")==true && motion.at("poses")==Json({"home","working"}),"Resolved motion identifies driven coordinates and poses");
  BuiltModel built(doc);near(part(built.summary(),"carriage").at("transform")[7],15);
  auto bad=doc;bad["parameters"]["angle"]=181;
  auto e=fails("invalid_model",[&]{validate_model(bad);});require(e.details.at("feature_id")=="mechanism" && e.details.at("mate_id")=="hinge","Limit failure locates moving mate");
  bad=doc;bad["features"][3]["mates"][1]["travel_mm"]=5;fails("invalid_model",[&]{validate_model(bad);});
  bad=doc;bad["features"][3]["couplings"].push_back(coupling("other","shaft","travel_mm","rail","travel_mm",1));fails("invalid_model",[&]{validate_model(bad);});
  bad=doc;bad["features"][3]["mates"][0].erase("angle_deg");bad["features"][3]["couplings"].push_back(coupling("cycle","shaft","angle_deg","hinge","angle_deg",1));fails("invalid_model",[&]{validate_model(bad);});
  bad=doc;bad["features"][3]["couplings"][0]["target"]["mate_id"]="missing";fails("invalid_model",[&]{validate_model(bad);});
  bad=doc;bad["features"][3]["mates"][1]["travel_limits_mm"]={0,4};fails("invalid_model",[&]{validate_model(bad);});
  bad=doc;bad["features"][3]["poses"][0]["values"].push_back(value("rail","travel_mm",0));fails("invalid_model",[&]{validate_model(bad);});
  bad=doc;bad["features"][3]["poses"][0]["values"].erase(1);fails("invalid_model",[&]{validate_model(bad);});
  bad=doc;bad["features"][3]["poses"][0]["values"][0]["value"]=500;
  e=fails("invalid_model",[&]{validate_model(bad);});require(e.details.at("pose_id")=="home","Stored pose limit failure locates the pose");
  const auto home=apply_operations(doc,Json::array({{{"op","apply_pose"},{"assembly_id","mechanism"},{"pose_id","home"}}}));
  near(part(BuiltModel(home).summary(),"carriage").at("transform")[3],10);
  near(part(BuiltModel(home).summary(),"sleeve").at("transform")[11],0);
  fails("invalid_argument",[&]{apply_operations(doc,Json::array({{{"op","set_joint_value"},{"assembly_id","mechanism"},{"mate_id","rail"},{"coordinate","angle_deg"},{"value",5}}}));});
  auto independent=apply_operations(doc,Json::array({{{"op","remove_coupling"},{"assembly_id","mechanism"},{"coupling_id","lead"}},
    {{"op","remove_pose"},{"assembly_id","mechanism"},{"pose_id","home"}},{{"op","remove_pose"},{"assembly_id","mechanism"},{"pose_id","working"}},
    {{"op","set_joint_value"},{"assembly_id","mechanism"},{"mate_id","rail"},{"coordinate","travel_mm"},{"value",10}}}));
  near(part(BuiltModel(independent).summary(),"carriage").at("transform")[7],20);
}
void service() {
  const auto root=temporary_file(fs::temp_directory_path());fs::remove(root);directory(root);
  struct Cleanup{fs::path path;~Cleanup(){std::error_code ignored;fs::remove_all(path,ignored);}} cleanup{root};
  set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));Service service(root);const auto doc=coupled();
  service.call("cad_create",{{"document_id","moving"},{"model",doc}});
  Json edits=Json::array({{{"op","apply_pose"},{"assembly_id","mechanism"},{"pose_id","home"}}});
  const auto preview=service.call("cad_preview",{{"document_id","moving"},{"expected_revision",1},{"operations",edits}});
  near(part(preview.at("summary"),"carriage").at("transform")[3],10);
  require(service.call("cad_read",{{"document_id","moving"}}).at("revision")==1,"Motion preview leaves HEAD unchanged");
  service.call("cad_apply",{{"document_id","moving"},{"expected_revision",1},{"operations",edits}});
  edits=Json::array({{{"op","set_joint_value"},{"assembly_id","mechanism"},{"mate_id","hinge"},{"coordinate","angle_deg"},{"value",300}}});
  fails("invalid_model",[&]{service.call("cad_apply",{{"document_id","moving"},{"expected_revision",2},{"operations",edits}});});
  Service reopened(root);require(reopened.call("cad_read",{{"document_id","moving"}}).at("revision")==2,"Invalid motion preserves HEAD after restart");
  require(reopened.call("cad_read",{{"document_id","moving"},{"revision",1}}).at("model")==doc,"Historical joint/pose/coupling source remains editable");
  auto step=reopened.call("cad_export",{{"document_id","moving"},{"revision",2},{"format","step"}});
  const auto imported=reopened.call("cad_import",{{"document_id","readback"},{"path",step.at("path")}});
  near(imported.at("summary").at("volume_mm3"),252);require(imported.at("summary").at("solid_count")==4,"Posed STEP retains all parts");
  const auto query=reopened.call("cad_query",{{"document_id","moving"},{"revision",2}});
  const auto& bounds=query.at("summary").at("bounds_mm");
  const auto stl=reopened.call("cad_export",{{"document_id","moving"},{"revision",2},{"format","stl"}});
  const auto bytes=read_text(path_from_utf8(stl.at("path")),64*1024*1024);
  const auto uint32=[&](std::size_t offset) {std::uint32_t value=0;for(int i=0;i<4;++i)value|=std::uint32_t(static_cast<unsigned char>(bytes.at(offset+i)))<<(8*i);return value;};
  const auto triangles=uint32(80);require(bytes.size()==84+50ULL*triangles,"Posed STL has complete binary triangles");
  std::array<double,3> minimum,maximum;minimum.fill(std::numeric_limits<double>::infinity());maximum.fill(-std::numeric_limits<double>::infinity());
  for(std::uint32_t triangle=0;triangle<triangles;++triangle) for(int vertex=0;vertex<3;++vertex) for(int axis=0;axis<3;++axis) {
    const double value=std::bit_cast<float>(uint32(84+50ULL*triangle+12+12*vertex+4*axis));
    minimum[axis]=std::min(minimum[axis],value);maximum[axis]=std::max(maximum[axis],value);
  }
  for(int axis=0;axis<3;++axis) {
    require(std::abs(minimum[axis]-bounds.at("min")[axis].get<double>())<1e-5,"Posed STL minimum matches native geometry");
    require(std::abs(maximum[axis]-bounds.at("max")[axis].get<double>())<1e-5,"Posed STL maximum matches native geometry");
  }
  const auto drawing=reopened.call("cad_drawing",{{"document_id","moving"},{"revision",2},{"drawing",{
    {"views",Json::array({{{"id","top"},{"orientation","top"}}})},{"dimensions",Json::array({{{"view","top"},{"kind","width"}},{{"view","top"},{"kind","height"}}})}}}});
  near(drawing.at("dimensions")[0].at("value_mm"),bounds.at("max")[0].get<double>()-bounds.at("min")[0].get<double>());
  near(drawing.at("dimensions")[1].at("value_mm"),bounds.at("max")[1].get<double>()-bounds.at("min")[1].get<double>());
}
Json live_call(Service& service,const Json& args,const std::string& tool="cad_viewer") {
  for (int attempt=0;;++attempt) {
    try {return service.call(tool,args);} catch(const Error& error) {if(error.code!="workspace_busy" || attempt==1000)throw;}
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}
Json ready(Service& service) {
  for(int attempt=0;attempt<3000;++attempt) {
    const auto result=live_call(service,{{"action","sync"},{"view_id","moving"}});
    if(result.at("state")=="ready") return result;
    require(result.at("state")=="loading","Unexpected motion view state: "+result.dump());
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  throw std::runtime_error("Motion view did not become ready");
}
Json mesh(Service& service,const Json& evaluation) {
  std::string text;Json offset=0;
  do {
    const auto chunk=live_call(service,{{"action","mesh"},{"view_id","moving"},{"evaluation_id",evaluation},{"offset",offset}});
    text+=chunk.at("data").get<std::string>();offset=chunk.at("next_offset");
  } while(!offset.is_null());
  return parse_json(text,64*1024*1024);
}
void live_motion() {
  const auto root=temporary_file(fs::temp_directory_path());fs::remove(root);directory(root);
  struct Cleanup{fs::path path;~Cleanup(){std::error_code ignored;fs::remove_all(path,ignored);}} cleanup{root};
  Service service(root);service.call("cad_create",{{"document_id","moving"},{"model",coupled()}});
  service.call("cad_open",{{"document_id","moving"},{"view_id","moving"}});
  auto shown=ready(service);const auto original=mesh(service,shown.at("evaluation_id"));
  auto action=[&](const std::string& name,Json fields=Json::object()) {
    fields["action"]=name;fields["view_id"]="moving";fields["evaluation_id"]=shown.at("evaluation_id");return live_call(service,fields);
  };
  const auto values=Json::array({value("hinge","angle_deg",45),value("shaft","travel_mm",3)});
  action("motion_preview",{{"values",values}});shown=ready(service);
  require(shown.at("draft")==true && shown.at("revision")==1,"Preview is explicitly a draft at its base revision");
  near(part(shown.at("summary"),"carriage").at("transform")[3],12.5/std::sqrt(2.0));
  near(part(shown.at("summary"),"carriage").at("transform")[7],12.5/std::sqrt(2.0));
  const auto draft=mesh(service,shown.at("evaluation_id"));
  require(draft.at("draft")==true && draft.at("mesh").at("positions")!=original.at("mesh").at("positions"),"Preview transfers native posed mesh");
  require(service.call("cad_read",{{"document_id","moving"}}).at("revision")==1,"Live preview never commits");
  const Json pick={{"document_id","moving"},{"revision",1},{"evaluation_id",shown.at("evaluation_id")},{"feature_id","mechanism"},
    {"kind","face"},{"entity_id",draft.at("topology").at("faces")[0].at("id")}};
  fails("draft_selection",[&]{service.call("cad_resolve_selection",pick);});
  fails("stale_selection",[&]{action("context",{{"selection",pick}});});
  const Json camera={{"yaw",.4},{"pitch",.3},{"zoom",2},{"pan",{.1,.2}}};
  const auto context=action("context",{{"selection",nullptr},{"camera",camera},{"hidden_part_ids",{"sleeve"}},{"prompt","Review this pose"}});
  require(context.at("draft")==true && context.at("stale")==false && context.at("preview_operations")==shown.at("preview_operations"),"Agent context names the exact unsaved pose");
  Service reopened(root);require(ready(reopened).at("evaluation_id")==shown.at("evaluation_id"),"Draft survives service reopen without becoming a saved revision");
  fails("invalid_model",[&]{action("motion_preview",{{"values",Json::array({value("hinge","angle_deg",200),value("shaft","travel_mm",3)})}});});
  fails("invalid_argument",[&]{action("motion_preview",{{"values",Json::array({value("hinge","angle_deg",20)})}});});
  require(ready(service).at("evaluation_id")==shown.at("evaluation_id"),"Invalid preview leaves previous valid draft intact");
  action("motion_reset");shown=ready(service);
  require(shown.at("draft")==false && mesh(service,shown.at("evaluation_id")).at("mesh")==original.at("mesh"),"Reset restores exact committed geometry");
  fails("stale_selection",[&]{live_call(service,{{"action","mesh"},{"view_id","moving"},{"evaluation_id",draft.at("evaluation_id")}});});
  require(shown.at("hidden_part_ids")==Json::array({"sleeve"}),"Visibility survives pose changes");
  require(service.call("cad_context",{{"view_id","moving"}}).at("camera")==camera,"Camera survives pose changes");
  action("motion_preview",{{"pose_id","home"}});
  // Reset even if a preview is still running; a late worker must not republish it.
  auto state=parse_json(read_text(root/"views"/"moving"/"state.json"));
  const auto pending=state.value("pending",Json());
  shown["evaluation_id"]=state.at("display").at("evaluation_id");action("motion_reset");shown=ready(service);
  bool superseded_finished=pending.is_null();
  if (!pending.is_null()) for(int attempt=0;attempt<3000;++attempt) {
    const auto job=live_call(service,{{"action","get"},{"job_id",pending.at("job_id")}},"cad_job");
    const auto status=job.at("state");if(status!="queued" && status!="running" && status!="cancelling"){superseded_finished=true;break;}
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  require(superseded_finished,"Superseded preview worker reaches a terminal state");
  require(!ready(service).at("draft").get<bool>(),"Superseded preview cannot replace a reset pose");
  action("motion_preview",{{"values",values}});shown=ready(service);
  action("motion_save",{{"pose_id","inspection"}});shown=ready(service);
  require(shown.at("revision")==2 && shown.at("draft")==false,"Save uses an ordinary committed revision");
  require(service.call("cad_read",{{"document_id","moving"},{"revision",1}}).at("model")==coupled(),"Saving motion preserves historical intent");
  const auto& poses=shown.at("model").at("features")[3].at("poses");
  require(poses.back().at("id")=="inspection" && poses.back().at("values")==values,"Saving a named pose records independent coordinates");
  near(part(shown.at("summary"),"carriage").at("transform")[3],12.5/std::sqrt(2.0));
  action("motion_preview",{{"pose_id","home"}});shown=ready(service);
  service.call("cad_apply",{{"document_id","moving"},{"expected_revision",2},{"operations",Json::array({
    {{"op","set_joint_value"},{"assembly_id","mechanism"},{"mate_id","hinge"},{"coordinate","angle_deg"},{"value",20}}})}});
  fails("stale_selection",[&]{action("motion_save");});
  shown=ready(service);require(shown.at("revision")==3 && !shown.at("draft").get<bool>(),"External revision retires an old draft instead of overwriting it");
  near(shown.at("summary").at("assembly").at("motion").at("dofs")[0].at("value"),20);
}
}
void cancelled_motion() {
  const auto root=temporary_file(fs::temp_directory_path());fs::remove(root);directory(root);
  struct Cleanup{fs::path path;~Cleanup(){std::error_code ignored;fs::remove_all(path,ignored);}} cleanup{root};
  Service service(root);service.call("cad_create",{{"document_id","moving"},{"model",coupled()}});
  Json edits=Json::array({{{"op","set_joint_value"},{"assembly_id","mechanism"},{"mate_id","hinge"},{"coordinate","angle_deg"},{"value",60}}});
  // A real expensive geometry branch keeps the transaction in flight after
  // its pose edit. All features are validated even outside the assembly output.
  const auto heavy=parse_json(R"([
    {"id":"plate","type":"box","size":[1300,1300,5],"origin":[-10,-10,0]},
    {"id":"pin","type":"cylinder","radius":4,"height":20,"origin":[0,0,-5]},
    {"id":"row","type":"pattern","input":"pin","count":64,"step":[20,0,0]},
    {"id":"grid","type":"pattern","input":"row","count":64,"step":[0,20,0]},
    {"id":"perforated","type":"cut","left":"plate","right":"grid"}])");
  for(const auto& feature:heavy) edits.push_back({{"op","add_feature"},{"feature",feature}});
  const auto candidate=apply_operations(coupled(),edits);validate_model(candidate);
  live_call(service,{{"action","submit"},{"request_id","cancel_pose"},{"tool","cad_apply"},
    {"arguments",{{"document_id","moving"},{"expected_revision",1},{"operations",edits}}}},"cad_job");
  bool building=false;
  for(int attempt=0;attempt<2000 && !building;++attempt) {
    for(const auto& worker:fs::directory_iterator(root/".workers"))
      if(fs::exists(worker.path()/"building.json") && fs::exists(worker.path()/"process.json")) building=true;
    if(!building)std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  require(building,"Motion transaction reaches a native geometry worker before cancellation");
  live_call(service,{{"action","cancel"},{"job_id","cancel_pose"}},"cad_job");
  Json terminal;
  for(int attempt=0;attempt<3000;++attempt) {
    terminal=live_call(service,{{"action","get"},{"job_id","cancel_pose"}},"cad_job");const auto state=terminal.at("state");
    if(state!="queued" && state!="running" && state!="cancelling")break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  require(terminal.at("state")=="cancelled","Cancelled motion transaction reaches terminal cancellation");
  const auto saved=service.call("cad_read",{{"document_id","moving"}});
  require(saved.at("revision")==1 && saved.at("model")==coupled(),"Cancelled pose and geometry edits preserve the entire saved intent");
  const auto query=service.call("cad_query",{{"document_id","moving"},{"revision",1}});
  near(query.at("summary").at("assembly").at("motion").at("dofs")[0].at("value"),90);
}
int main(){try{configure_kernel_logging();geometry();validation();service();live_motion();cancelled_motion();std::cout<<checks<<" motion checks passed\n";return 0;}
catch(const Error& e){std::cerr<<e.code<<": "<<e.what()<<" "<<e.details.dump()<<"\n";return 1;}
catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
