#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/cache.hpp"
#include "agentcad/hash.hpp"
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <numbers>
#include <set>
#include <source_location>
#include <thread>
using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
void near(double actual,double expected,double tolerance=1e-5){require(std::abs(actual-expected)<=tolerance,"Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
Error fails(const std::set<std::string>& codes,const std::function<void()>& action,const std::source_location& where=std::source_location::current()){
  try{action();}catch(const Error& e){require(codes.contains(e.code),"Unexpected "+e.code+": "+e.what());return e;}
  throw std::runtime_error("Expected failure at line "+std::to_string(where.line()));
}
struct Temp {fs::path root=temporary_directory(fs::canonical(fs::temp_directory_path()));~Temp(){std::error_code ignored;fs::remove_all(root,ignored);}};
Json document(Json features,const std::string& output){return {{"schema_version",1},{"units","mm"},{"parameters",{{"height",10},{"wall",-2}}},{"features",features},{"output",output}};}
Json base(){return {{"id","base"},{"type","box"},{"size",Json::array({20,30,Json{{"parameter","height"}}})}};}
Json planar(Json normal,int expected=1,const std::string& input="base"){
  return {{"type","geometric"},{"feature_id",input},{"surface_kind","plane"},{"expected_count",expected},{"normal",{{"vector",normal},{"tolerance",1e-6}}}};
}
Json shell(Json faces=planar({0,0,1}),Json wall=Json{{"parameter","wall"}}){return {{"id","result"},{"type","shell"},{"input","base"},{"thickness",wall},{"faces",faces},{"join","intersection"}};}
Json offset(Json distance=1,const std::string& input="base"){return {{"id","result"},{"type","offset"},{"input",input},{"distance",distance},{"join","intersection"}};}
Json feature_model(Json feature){return document(Json::array({base(),feature}),"result");}
double volume(const Json& model){const auto summary=BuiltModel(model).summary();require(summary.at("valid")==true,"Result is valid");return summary.at("volume_mm3");}
void lineage(const BuiltModel& built,const std::string& source="base"){
  const auto before=built.topology(source),after=built.topology();
  std::set<std::string> inputs,outputs;
  for(const auto* key:{"faces","edges"}){for(const auto& e:before.at(key))inputs.insert(e.at("id"));for(const auto& e:after.at(key))outputs.insert(e.at("id"));}
  const auto& provenance=after.at("provenance");require(provenance.at("dependencies")==Json::array({source}),"Provenance retains source dependency");
  require(!provenance.at("history").empty()&&!provenance.at("history_truncated").get<bool>(),"Operation emits complete available native lineage");
  for(const auto& h:provenance.at("history")){
    require(h.at("source_feature_id")==source&&inputs.contains(h.at("source_id")),"Lineage source belongs to source enumeration");
    if(h.contains("result_id"))require(outputs.contains(h.at("result_id")),"Lineage result belongs to complete output enumeration");
  }
}
void shell_geometry(){
  auto model=feature_model(shell());BuiltModel inward(model);near(inward.summary().at("volume_mm3"),6000-16*26*8);lineage(inward);
  require(inward.topology("base")==BuiltModel(document(Json::array({base()}),"base")).topology(),"Shell preserves source geometry/topology");
  near(volume(feature_model(shell(planar({0,0,7}),2))),24*34*12-6000);
  near(volume(feature_model(shell(Json::array()))),6000-16*26*6);
  near(volume(feature_model(shell(Json::array(),2))),24*34*14-6000);
  near(volume(feature_model(shell(Json::array({planar({0,0,1}),planar({1,0,0})})))) ,6000-18*26*8);
  auto cylindrical=document(Json::array({{{"id","base"},{"type","cylinder"},{"radius",5},{"height",10}},shell({{"type","geometric"},{"feature_id","base"},{"surface_kind","plane"},{"expected_count",2}},-1)}),"result");
  near(volume(cylindrical),90*std::numbers::pi);
  auto bad=model;bad["features"].back()["faces"]={{"type","geometric"},{"feature_id","base"},{"surface_kind","sphere"},{"expected_count",1}};
  auto failure=fails({"selection_missing"},[&]{BuiltModel invalid(bad);});require(failure.details.at("source_feature_id")=="base"&&failure.details.at("feature_id")=="result","Face selection failure is feature scoped");
  require(failure.details.at("candidates").size()==6,"Face selector failures expose source candidates");
  bad["features"].back()["faces"]={{"type","geometric"},{"feature_id","base"},{"surface_kind","plane"},{"expected_count",1}};
  failure=fails({"selection_ambiguous"},[&]{BuiltModel invalid(bad);});require(failure.details.at("actual_count")==6,"Ambiguity exposes actual face cardinality");
  bad["features"].back()["faces"]["expected_count"]=7;fails({"selection_count_mismatch"},[&]{BuiltModel invalid(bad);});
  bad=model;bad["features"].back()["faces"]=Json::array({planar({0,0,1}),planar({0,0,1})});fails({"invalid_model"},[&]{BuiltModel invalid(bad);});
  bad=model;bad["features"].back()["faces"]["feature_id"]="wrong";fails({"invalid_model"},[&]{validate_model(bad);});
  for(const auto thickness:{-15,-30,-60}){bad=model;bad["features"].back()["thickness"]=thickness;fails({"kernel_failure","invalid_shape"},[&]{BuiltModel invalid(bad);});}
  bad=model;bad["features"].back()["thickness"]=0;fails({"invalid_model"},[&]{validate_model(bad);});
  bad=model;bad["features"].back()["join"]="tangent";fails({"invalid_model"},[&]{validate_model(bad);});
  bad=model;bad["features"].back()["faces"]={{"type","geometric"},{"feature_id","base"},{"surface_kind","plane"},{"expected_count",6}};fails({"invalid_model"},[&]{BuiltModel invalid(bad);});
  auto two=document(Json::array({base(),{{"id","two"},{"type","pattern"},{"input","base"},{"count",2},{"step",{50,0,0}}},shell(planar({0,0,1},2,"two"))}),"result");two["features"].back()["input"]="two";fails({"invalid_model"},[&]{BuiltModel invalid(two);});
}
void offset_geometry(){
  auto model=feature_model(offset());BuiltModel expanded(model);near(expanded.summary().at("volume_mm3"),22*32*12);lineage(expanded);
  near(expanded.summary().at("bounds_mm").at("min")[0],-1);near(expanded.summary().at("bounds_mm").at("max")[2],11);
  near(volume(feature_model(offset(-1))),18*28*8);
  auto arc=offset();arc["join"]="arc";near(volume(feature_model(arc)),6000+2200+60*std::numbers::pi+4*std::numbers::pi/3);
  auto cylinder=document(Json::array({{{"id","base"},{"type","cylinder"},{"radius",5},{"height",10}},offset(-1)}),"result");near(volume(cylinder),128*std::numbers::pi);
  auto pair=document(Json::array({base(),{{"id","two"},{"type","pattern"},{"input","base"},{"count",2},{"step",{50,0,0}}},offset(1,"two")}),"result");
  BuiltModel multiple(pair);near(multiple.summary().at("volume_mm3"),2*22*32*12);require(multiple.summary().at("solid_count")==2,"Offset retains independent input solids");lineage(multiple,"two");
  auto bad=feature_model(offset(-30));fails({"kernel_failure","invalid_shape"},[&]{BuiltModel invalid(bad);});
  for(const auto distance:{-11,-16,-30,-60})fails({"kernel_failure","invalid_shape"},[&]{BuiltModel invalid(feature_model(offset(distance)));});
  bad=feature_model(offset(0));fails({"invalid_model"},[&]{validate_model(bad);});
}
void thickening_geometry(){
  const Json sketch={{"id","profile"},{"type","sketch"},{"workplane",{{"origin",{0,0,5}},{"normal",{0,0,1}},{"x_direction",{1,0,0}}}},
    {"profile",{{"type","rectangle"},{"width",20},{"height",30}}}};
  Json thick={{"id","result"},{"type","thicken"},{"input","profile"},{"thickness",2},{"join","intersection"}};
  auto model=document(Json::array({sketch,thick}),"result");BuiltModel plate(model);near(plate.summary().at("volume_mm3"),1200);near(plate.summary().at("bounds_mm").at("min")[2],5);near(plate.summary().at("bounds_mm").at("max")[2],7);lineage(plate,"profile");
  model["features"].back()["thickness"]=-2;near(volume(model),1200);near(BuiltModel(model).summary().at("bounds_mm").at("min")[2],3);
  thick["input"]="base";thick["faces"]=planar({0,0,1});model=feature_model(thick);near(volume(model),1200);near(BuiltModel(model).summary().at("bounds_mm").at("max")[2],12);
  // A cylindrical lateral face gives a curved, open surface patch. The source
  // cylinder is retained as a dependency; the new feature is only the tube.
  thick["thickness"]=1;thick["faces"]={{"type","geometric"},{"feature_id","base"},{"surface_kind","cylinder"},{"expected_count",1}};
  model=document(Json::array({{{"id","base"},{"type","cylinder"},{"radius",5},{"height",10}},thick}),"result");BuiltModel tube(model);near(tube.summary().at("volume_mm3"),110*std::numbers::pi);lineage(tube);
  model["features"].back()["thickness"]=-1;near(volume(model),90*std::numbers::pi);
  model["features"].back()["thickness"]=-12;fails({"kernel_failure","invalid_shape"},[&]{BuiltModel invalid(model);});
  thick["faces"]=Json::array({planar({0,0,1}),planar({1,0,0})});thick["thickness"]=2;model=feature_model(thick);near(volume(model),2*30*(20+10+2));
  auto bad=feature_model(thick);bad["features"].back()["faces"]=Json::array({planar({0,0,1}),planar({0,0,-1})});fails({"invalid_model"},[&]{BuiltModel invalid(bad);});
  bad=feature_model(thick);bad["features"].back()["faces"]={{"type","geometric"},{"feature_id","base"},{"surface_kind","plane"},{"expected_count",6}};fails({"invalid_model"},[&]{BuiltModel invalid(bad);});
  bad=feature_model(thick);bad["features"].back().erase("faces");fails({"invalid_model"},[&]{validate_model(bad);});
  bad=document(Json::array({sketch,thick}),"result");bad["features"].back()["input"]="profile";fails({"invalid_model"},[&]{validate_model(bad);});
}
Json job(Service& service,const Json& args){for(int i=0;i<1000;++i){try{return service.call("cad_job",args);}catch(const Error& e){if(e.code!="workspace_busy")throw;}std::this_thread::sleep_for(std::chrono::milliseconds(5));}throw std::runtime_error("Job metadata remains busy");}
Json finish(Service& service,const std::string& id){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);while(std::chrono::steady_clock::now()<deadline){auto state=job(service,{{"action","get"},{"job_id",id}});if(state.at("state")=="succeeded"||state.at("state")=="failed")return state;std::this_thread::sleep_for(std::chrono::milliseconds(20));}throw std::runtime_error("Job did not terminate");}
void cache_rebuilds(){
  for(auto feature:Json::array({shell(),offset(),Json{{"id","result"},{"type","thicken"},{"input","base"},{"thickness",2},{"faces",planar({0,0,1})},{"join","intersection"}}})) {
    Temp temp;
    auto model=feature_model(feature);Json diagnostics;const auto first=evaluate_model(temp.root,model,{{"kind","view"}},&diagnostics);
    near(BuiltModel(model,BuiltModel(model).snapshot()).summary().at("volume_mm3"),first.at("summary").at("volume_mm3"));
    fs::remove(temp.root/".cache"/(geometry_cache_key(model)+".json"));
    const auto warm=evaluate_model(temp.root,model,{{"kind","view"}},&diagnostics);require(diagnostics.at("feature_hits").at("result")==true,"Exact feature snapshot is reused");
    require(warm.at("topology")==first.at("topology"),"Cached topology retains face selectors and operation history");
    model["parameters"]["height"]=14;const auto changed=evaluate_model(temp.root,model,{{"kind","view"}},&diagnostics);
    require(diagnostics.at("feature_hits").at("base")==false&&diagnostics.at("feature_hits").at("result")==false,"Upstream dimensions invalidate the affected operation");
    near(changed.at("summary").at("volume_mm3"),BuiltModel(model).summary().at("volume_mm3"));
  }
}
void lifecycle(){
  Temp temp;const auto workspace=temp.root/"workspace";Service service(workspace);auto model=feature_model(shell());
  auto record=service.call("cad_create",{{"document_id","housing"},{"model",model}});near(record.at("summary").at("volume_mm3"),2672);
  const auto view=service.call("cad_query",{{"document_id","housing"},{"revision",1},{"kind","topology"},{"feature_id","base"}});
  const auto face=view.at("topology").at("faces").front();require(face.contains("selector"),"Query suggests a unique persistent face rule");
  const auto reference=service.call("cad_resolve_selection",{{"document_id","housing"},{"revision",1},{"evaluation_id",view.at("evaluation_id")},{"feature_id","base"},{"kind","face"},{"entity_id",face.at("id")}});
  require(reference.at("selector")==face.at("selector"),"Face picks resolve the same persistent geometric rule");
  fs::remove_all(workspace/".cache");Service cold(workspace);near(cold.call("cad_query",{{"document_id","housing"},{"revision",1}}).at("summary").at("volume_mm3"),2672);
  auto edited=cold.call("cad_apply",{{"document_id","housing"},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","height"},{"value",12}}})}});near(edited.at("summary").at("volume_mm3"),20*30*12-16*26*10);
  const auto previous=cold.call("cad_read",{{"document_id","housing"}});
  fails({"kernel_failure","invalid_shape"},[&]{cold.call("cad_apply",{{"document_id","housing"},{"expected_revision",2},{"operations",Json::array({{{"op","set_parameter"},{"name","wall"},{"value",-30}}})}});});
  require(cold.call("cad_read",{{"document_id","housing"}})==previous,"Failed shell edit preserves complete committed record");
  fails({"stale_selection"},[&]{cold.call("cad_resolve_selection",reference.at("reference"));});
  near(cold.call("cad_query",{{"document_id","housing"},{"revision",1}}).at("summary").at("volume_mm3"),2672);
  for(const auto* format:{"step","stl","3mf"}){auto exported=cold.call("cad_export",{{"document_id","housing"},{"revision",2},{"format",format}});require(fs::exists(path_from_utf8(exported.at("path"))),std::string(format)+" exports independently");if(std::string(format)=="step")near(cold.call("cad_import",{{"document_id","readback"},{"path",exported.at("path")}}).at("summary").at("volume_mm3"),3040);}
  // A pinned component must rewrite selectors along with their source feature.
  cold.call("cad_create",{{"document_id","consumer"},{"model",document(Json::array({base()}),"base")}});
  const auto captured=cold.call("cad_apply",{{"document_id","consumer"},{"expected_revision",1},{"operations",Json::array({
    {{"op","set_component"},{"id","housingCopy"},{"source_document_id","housing"},{"source_revision",2},{"bindings",{{"height",14}}}},
    {{"op","set_output"},{"feature_id","housingCopy"}}})}});near(captured.at("summary").at("volume_mm3"),20*30*14-16*26*12);
  const auto& local=captured.at("model").at("features").back();require(local.at("faces").at("feature_id")==local.at("input"),"Component maps face selector source IDs");
  Temp portable;Service independent(portable.root);fs::remove_all(workspace/"documents/housing");
  near(independent.call("cad_create",{{"document_id","portable"},{"model",captured.at("model")}}).at("summary").at("volume_mm3"),3408);
  job(independent,{{"action","submit"},{"request_id","changeWall"},{"tool","cad_apply"},{"arguments",{{"document_id","portable"},{"expected_revision",1},{"operations",Json::array({{{"op","replace_feature"},{"id","housingCopy"},{"feature",Json{{"id","housingCopy"},{"type","offset"},{"input",local.at("input")},{"distance",1},{"join","intersection"}}}}})}}}});
  const auto success=finish(independent,"changeWall");require(success.at("state")=="succeeded",success.dump());near(success.at("result").at("summary").at("volume_mm3"),22*32*16);
  job(independent,{{"action","submit"},{"request_id","collapsed"},{"tool","cad_apply"},{"arguments",{{"document_id","portable"},{"expected_revision",2},{"operations",Json::array({{{"op","replace_feature"},{"id","housingCopy"},{"feature",Json{{"id","housingCopy"},{"type","offset"},{"input",local.at("input")},{"distance",-30}}}}})}}}});
  const auto failure=finish(independent,"collapsed");require(failure.at("state")=="failed","Collapsed asynchronous offset fails");require(independent.call("cad_read",{{"document_id","portable"}}).at("revision")==2,"Failed native job preserves HEAD");
}
}
int main(){try{set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));shell_geometry();offset_geometry();thickening_geometry();cache_rebuilds();lifecycle();std::cout<<checks<<" shell/offset/thicken checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"Shell/offset/thicken failure: "<<e.what()<<'\n';return 1;}}
