#include "mutation_test_support.hpp"
#include "agentcad/component.hpp"
#include "agentcad/model.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/hash.hpp"
#include "geometry_equivalence.hpp"
#include <cmath>
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
void near(double actual,double expected){require(std::abs(actual-expected)<1e-6,"Geometry differs: "+std::to_string(actual)+" vs "+std::to_string(expected));}
Error fails(const std::string& code,const std::function<void()>& action){try{action();}catch(const Error& e){require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what());return e;}throw std::runtime_error("Expected "+code);}
struct Temporary {
  fs::path root;
  Temporary(){root=temporary_file(fs::temp_directory_path());fs::remove(root);directory(root);}
  ~Temporary(){std::error_code ignored;fs::remove_all(root,ignored);}
};
Json source_model() {
  return parse_json(R"({"schema_version":1,"units":"mm","parameters":{"width":10,"thickness":2,"angle":90,"unused":99},"features":[
    {"id":"body","type":"box","size":[{"parameter":"width"},4,{"parameter":"thickness"}]},
    {"id":"lever","type":"box","size":[6,2,2]},
    {"id":"unrelated","type":"box","size":[1,1,{"parameter":"unused"}]},
    {"id":"mechanism","type":"assembly","parts":[{"id":"foot","input":"body"},{"id":"link","input":"lever"}],"mates":[
      {"id":"pivot","type":"revolute","parent":"foot","child":"link","angle_deg":{"parameter":"angle"},"angle_limits_deg":[0,180],
       "parent_frame":{"origin":[10,0,2],"normal":[0,0,1],"x_direction":[1,0,0]},
       "child_frame":{"origin":[0,0,0],"normal":[0,0,1],"x_direction":[1,0,0]}}],
     "poses":[{"id":"straight","values":[{"mate_id":"pivot","coordinate":"angle_deg","value":0}]}],
     "bom":[{"input":"body","part_number":"LIBRARY-BODY"},{"input":"lever","part_number":"LIBRARY-LEVER"}]}],"output":"mechanism"})");
}
Json consumer() {return parse_json(R"({"schema_version":1,"units":"mm","parameters":{"span":20},"features":[{"id":"seed","type":"box","size":[1,1,1]}],"output":"seed"})");}
Json capture(const std::string& id="module",int revision=1) {
  return {{"op","set_component"},{"id",id},{"source_document_id","library"},{"source_revision",revision},{"bindings",{{"width",{{"parameter","span"}}}}}};
}
Json assembly() {return parse_json(R"({"id":"machine","type":"assembly","parts":[{"id":"left","input":"module"},{"id":"right","input":"module","placement":{"translation":[80,0,0]}}]})");}
Json apply(Service& service,int revision,Json operations){return service.call("cad_apply",{{"document_id","consumer"},{"expected_revision",revision},{"operations",operations}});}
Json job_call(Service& service,const Json& arguments) {
  for(int attempt=0;;++attempt) {
    try{return service.call("cad_job",arguments);}
    catch(const Error& error){if(error.code!="workspace_busy"||attempt==1000)throw;}
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}
void workflow() {
  Temporary temporary;Service service(temporary.root);
  const auto source=source_model();service.call("cad_create",{{"document_id","library"},{"model",source}});
  service.call("cad_create",{{"document_id","consumer"},{"model",consumer()}});
  const Json edits=Json::array({capture(),{{"op","add_feature"},{"feature",assembly()}},{{"op","set_output"},{"feature_id","machine"}}});
  auto preview=service.call("cad_preview",{{"document_id","consumer"},{"expected_revision",1},{"operations",edits},{"kind","mesh"}});
  near(preview.at("summary").at("volume_mm3"),368);
  require(service.call("cad_read",{{"document_id","consumer"}}).at("revision")==1,"Component preview never commits");
  auto record=apply(service,1,edits);auto revision=record.at("revision").get<int>();
  auto record_source=test::receipt_source(service,record);const auto first_source=record_source;const auto component=record_source.at("components")[0];
  require(component.at("snapshot")==source&&component.at("sha256")==sha256(source.dump()),"Full pinned source and its checksum are embedded");
  require(component.at("source").at("revision")==1&&component.at("feature_map").at("mechanism")=="module","Source revision and stable component root are explicit");
  require(component.at("feature_map").size()==3&&!component.at("feature_map").contains("unrelated")&&!component.at("parameter_map").contains("unused"),"Only output dependencies and used parameters become local features");
  require(record.at("summary").at("components")[0].at("modified")==false,"Fresh capture has no local changes");
  require(record.at("summary").at("assembly").at("mechanisms")[0].at("assembly_id")=="module","Imported child mechanisms remain editable in the parent view");
  const auto bom=service.call("cad_bom",{{"document_id","consumer"},{"revision",revision}}).at("bom");
  require(bom.at("total_quantity")==4&&bom.at("items")[0].at("quantity")==2,"Component BOM rolls up exact leaf instances");
  require(bom.dump().find("LIBRARY-BODY")!=std::string::npos,"Source BOM metadata survives namespacing");
  record=apply(service,revision,Json::array({{{"op","set_parameter"},{"name","span"},{"value",30}}}));revision=record.at("revision");record_source=test::receipt_source(service,record);
  near(record.at("summary").at("volume_mm3"),528);
  require(!record.at("summary").at("components")[0].at("modified").get<bool>(),"Mapped parent parameter changes preserve the declared component binding");
  service.call("cad_apply",{{"document_id","library"},{"expected_revision",1},{"operations",Json::array({
    {{"op","set_parameter"},{"name","thickness"},{"value",3}},{{"op","set_parameter"},{"name","angle"},{"value",0}}})}});
  near(service.call("cad_query",{{"document_id","consumer"},{"revision",revision}}).at("summary").at("volume_mm3"),528);
  require(service.call("cad_read",{{"document_id","consumer"}}).at("model").at("components")[0].at("source").at("revision")==1,"Source HEAD changes never update a consumer implicitly");
  record=apply(service,revision,Json::array({{{"op","set_joint_value"},{"assembly_id","module"},{"mate_id","pivot"},{"coordinate","angle_deg"},{"value",45}}}));revision=record.at("revision");record_source=test::receipt_source(service,record);
  require(record.at("summary").at("components")[0].at("modified")==true,"Local child pose edit is reported as a component variant");
  auto update=capture("module",2);update.erase("bindings");
  const auto conflict=fails("component_modified",[&]{apply(service,revision,Json::array({update}));});
  require(conflict.details.at("component_id")=="module"&&conflict.details.at("changes").at("features")==Json({"module"}),"Refresh reports the specific local edits instead of overwriting them");
  update["discard_local_changes"]=true;
  preview=service.call("cad_preview",{{"document_id","consumer"},{"expected_revision",revision},{"operations",Json::array({update})},{"kind","mesh"}});
  near(preview.at("summary").at("volume_mm3"),768);
  require(service.call("cad_read",{{"document_id","consumer"}}).at("model")==record_source,"Reviewing a source update preserves local edits until explicit save");
  record=apply(service,revision,Json::array({update}));revision=record.at("revision");record_source=test::receipt_source(service,record);
  near(record.at("summary").at("volume_mm3"),768);
  require(record_source.at("components")[0].at("bindings")==component.at("bindings"),"Refresh retains explicit mappings when omitted");
  require(record_source.at("components")[0].at("feature_map")==component.at("feature_map"),"Unchanged source IDs preserve consumer feature IDs across revisions");
  require(service.call("cad_read",{{"document_id","consumer"},{"revision",2}}).at("model")==first_source,"Consumer history retains the earlier source snapshot");
  const auto step=service.call("cad_export",{{"document_id","consumer"},{"revision",revision},{"format","step"}});
  const auto readback=service.call("cad_import",{{"document_id","readback"},{"path",step.at("path")}});
  near(readback.at("summary").at("volume_mm3"),768);
  require(readback.at("summary").at("solid_count")==4,"Independent STEP readback preserves repeated component solids");
  auto robot=service.call("cad_robot_export",{{"document_id","consumer"},{"revision",revision},{"robot",{{"format","urdf"},{"joint_properties",Json::array({
    {{"mate_id","left/pivot"},{"coordinate","angle_deg"},{"effort",1},{"velocity",.2}},
    {{"mate_id","right/pivot"},{"coordinate","angle_deg"},{"effort",1},{"velocity",.2}}})}}}});
  require(read_text(path_from_utf8(robot.at("path"))).find("mimic")!=std::string::npos,"Robot handoff keeps shared imported child motion");
  Temporary portable;Service independent(portable.root);
  const auto copied=independent.call("cad_create",{{"document_id","portable"},{"model",record_source}});
  near(copied.at("summary").at("volume_mm3"),768);
  require(test::receipt_source(independent,copied)==record_source,"A copied editable document works in a workspace with no source library");
  fails("not_found",[&]{independent.call("cad_read",{{"document_id","library"}});});
  auto damaged=record_source;damaged["components"][0]["snapshot"]["parameters"]["width"]=11;
  fails("invalid_model",[&]{validate_model(damaged);});
  damaged=record_source;damaged["components"][0]["feature_map"]["body"]="seed";
  fails("invalid_model",[&]{validate_model(damaged);});
  auto wrong=capture("other");wrong["bindings"]["absent"]=3;
  fails("invalid_argument",[&]{apply(service,revision,Json::array({wrong}));});
  wrong=capture("other");wrong["bindings"]["width"]={{"parameter","absent"}};
  fails("invalid_model",[&]{apply(service,revision,Json::array({wrong}));});
  wrong=capture("other");wrong["source_revision"]=999;
  fails("not_found",[&]{apply(service,revision,Json::array({wrong}));});
  fails("invalid_argument",[&]{apply(service,revision,Json::array({capture("seed")}));});
  fails("revision_conflict",[&]{apply(service,revision-1,Json::array({capture("other")}));});
  require(service.call("cad_read",{{"document_id","consumer"}}).at("model")==record_source,"Invalid source/bindings/collisions preserve HEAD and component intent");
  fails("invalid_model",[&]{apply(service,revision,Json::array({{{"op","remove_component"},{"id","module"}}}));});
  record=apply(service,revision,Json::array({{{"op","detach_component"},{"id","module"}}}));revision=record.at("revision");record_source=test::receipt_source(service,record);
  require(record_source.at("components").empty(),"Detach keeps editable materialized geometry and removes its source association");
  near(record.at("summary").at("volume_mm3"),768);
}
void graph_and_limits() {
  auto source=source_model();
  auto resolver=[&](const std::string& id,std::uint64_t revision){return Json{{"document_id",id},{"revision",revision},{"kernel_version","8.0.1"},{"model",source}};};
  auto operation=capture();operation["bindings"]["width"]={{"parameter","later"}};
  operation["bindings"]["angle"]={{"expression",{{"op","add"},{"args",{20,10}},{"unit","deg"}}}};
  const auto imported=apply_operations(consumer(),Json::array({operation,{{"op","set_parameter"},{"name","later"},{"value",25}},
    {{"op","set_output"},{"feature_id","module"}}}),resolver);
  near(BuiltModel(imported).summary().at("volume_mm3"),224);
  near(BuiltModel(imported).summary().at("assembly").at("motion").at("dofs")[0].at("value"),30);
  const auto parameter=imported.at("components")[0].at("parameter_map").at("thickness").get<std::string>();
  const auto variant=apply_operations(imported,Json::array({{{"op","set_parameter"},{"name",parameter},{"value",3}}}));
  require(component_status(variant)[0].at("changes").at("parameters")==Json({parameter}),"Unmapped local parameter edits are tracked for source refresh");
  require(test::geometry_equivalent(BuiltModel(imported,BuiltModel(imported).snapshot()).summary(),BuiltModel(imported).summary()),"Component snapshots restore equivalent exact geometry and unchanged provenance through the cache");
  auto base=consumer();source=consumer();
  for(int depth=0;depth<4;++depth) {
    auto next=Json::array({{{"op","set_component"},{"id","wrapped"},{"source_document_id","source"},{"source_revision",1}},
      {{"op","set_output"},{"feature_id","wrapped"}}});
    base=apply_operations(consumer(),next,resolver);source=base;
  }
  fails("limit_exceeded",[&]{apply_operations(consumer(),Json::array({{{"op","set_component"},{"id","wrapped"},{"source_document_id","source"},{"source_revision",1}}}),resolver);});
  const auto a=std::string(63,'a')+"x",b=std::string(63,'a')+"y",component=std::string(64,'z');
  source=consumer();source["features"]=Json::array({{{"id",a},{"type","box"},{"size",{1,1,1}}},{{"id",b},{"type","box"},{"size",{2,2,2}}},
    {{"id","pair"},{"type","assembly"},{"parts",Json::array({{{"id","one"},{"input",a}},{{"id","two"},{"input",b}}})}}});source["output"]="pair";
  const auto long_names=apply_operations(consumer(),Json::array({{{"op","set_component"},{"id",component},{"source_document_id","source"},{"source_revision",1}}}),resolver);
  const auto names=long_names.at("components")[0].at("feature_map");
  require(names.at(a)!=names.at(b)&&names.at(a).get<std::string>().size()<=64&&names.at(b).get<std::string>().size()<=64,"Long source identifiers receive distinct bounded stable names");
  source=source_model();base=consumer();
  auto invalid=capture();invalid["bindings"]["width"]={{"expression",{{"op","add"},{"args",{2,3}},{"unit","deg"}}}};
  fails("invalid_model",[&]{apply_operations(base,Json::array({invalid}),resolver);});
}
void selectors_and_jobs() {
  Temporary temporary;Service service(temporary.root);
  const auto source=parse_json(R"({"schema_version":1,"units":"mm","parameters":{"radius":0.5},"features":[
    {"id":"profile","type":"sketch","workplane":{"origin":[0,0,0],"normal":[0,0,1],"x_direction":[1,0,0]},"profile":{"type":"rectangle","width":10,"height":8}},
    {"id":"prism","type":"extrude","input":"profile","distance":4},
    {"id":"finished","type":"fillet","input":"prism","radius":{"parameter":"radius"},"edges":{"type":"geometric","feature_id":"prism","curve_kind":"line","expected_count":4,"direction":{"vector":[0,0,1],"tolerance":0.00001}}}],"output":"finished"})");
  service.call("cad_create",{{"document_id","library"},{"model",source}});
  service.call("cad_create",{{"document_id","consumer"},{"model",consumer()}});
  auto operation=capture("rounded");operation["bindings"]=Json::object();
  const Json edits=Json::array({operation,{{"op","set_output"},{"feature_id","rounded"}}});
  const auto imported=apply(service,1,edits);
  near(imported.at("summary").at("volume_mm3"),BuiltModel(source).summary().at("volume_mm3"));
  const auto imported_source=test::receipt_source(service,imported);
  const auto& map=imported_source.at("components")[0].at("feature_map");
  const auto& last=imported_source.at("features").back();
  require(last.at("edges").at("feature_id")==map.at("prism")&&last.at("input")==map.at("prism"),"Geometric selectors and sketch dependencies are remapped together");
  auto failing=operation;failing["bindings"]={{"radius",100}};
  const auto error=fails("kernel_failure",[&]{apply(service,2,Json::array({failing}));});
  require(error.details.at("feature_id")=="rounded","Failed component geometry identifies its local editable feature");
  require(service.call("cad_read",{{"document_id","consumer"}}).at("model")==imported_source,"Failed component rebuild leaves the prior snapshot and HEAD intact");
  const Json arguments={{"document_id","consumer"},{"expected_revision",2},{"operations",Json::array({operation})},{"request_id","component_refresh"}};
  job_call(service,{{"action","submit"},{"request_id","component_refresh"},{"tool","cad_apply"},{"arguments",arguments}});
  Json job;
  for(int i=0;i<3000;++i) {
    job=job_call(service,{{"action","get"},{"job_id","component_refresh"}});
    if(job.at("state")!="queued"&&job.at("state")!="running")break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  require(job.at("state")=="succeeded"&&job.at("result").at("revision")==3,"Pinned source capture runs through the bounded job path");
  require(service.call("cad_apply",arguments).at("revision")==3&&service.call("cad_read",{{"document_id","consumer"}}).at("revision")==3,"Replaying a captured component request never commits twice");
  const auto before_cancel=service.call("cad_read",{{"document_id","consumer"}});
  auto changed=operation;changed["bindings"]={{"radius",.75}};
  Json pending=Json::array({changed});
  // Hold this real candidate in native geometry work, where cancellation can
  // acquire the publication lock and prevent the captured snapshot being saved.
  const auto heavy=parse_json(R"([
    {"id":"plate","type":"box","size":[1300,1300,5],"origin":[-10,-10,0]},
    {"id":"pin","type":"cylinder","radius":4,"height":20,"origin":[0,0,-5]},
    {"id":"row","type":"pattern","input":"pin","count":64,"step":[20,0,0]},
    {"id":"grid","type":"pattern","input":"row","count":64,"step":[0,20,0]},
    {"id":"perforated","type":"cut","left":"plate","right":"grid"}])");
  for(const auto& feature:heavy)pending.push_back({{"op","add_feature"},{"feature",feature}});
  job_call(service,{{"action","submit"},{"request_id","cancel_component"},{"tool","cad_apply"},
    {"arguments",{{"document_id","consumer"},{"expected_revision",3},{"operations",pending}}}});
  bool building=false;
  for(int attempt=0;attempt<2000&&!building;++attempt) {
    for(const auto& worker:fs::directory_iterator(temporary.root/".workers"))
      if(fs::exists(worker.path()/"building.json")&&fs::exists(worker.path()/"process.json"))building=true;
    if(!building)std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  require(building,"Component refresh reaches native geometry before cancellation");
  job_call(service,{{"action","cancel"},{"job_id","cancel_component"}});
  for(int i=0;i<3000;++i) {
    job=job_call(service,{{"action","get"},{"job_id","cancel_component"}});
    if(job.at("state")!="queued"&&job.at("state")!="running"&&job.at("state")!="cancelling")break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  require(job.at("state")=="cancelled","Cancellation prevents component publication: "+job.dump());
  const auto after_cancel=service.call("cad_read",{{"document_id","consumer"}});
  require(after_cancel.at("revision")==3&&after_cancel.at("model")==before_cancel.at("model"),"Cancelled source refresh preserves HEAD, component snapshot, bindings and local features");
}
}
int main(){try{configure_kernel_logging();set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));workflow();graph_and_limits();selectors_and_jobs();std::cout<<checks<<" component checks passed\n";return 0;}
catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;}}
