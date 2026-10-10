#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/cache.hpp"
#include <BRepAdaptor_Surface.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
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
void require(bool condition,const std::string& message){++checks;if(!condition)throw std::runtime_error(message);}
void near(double actual,double expected,double tolerance=1e-5){require(std::abs(actual-expected)<tolerance,"Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
Error fails(const std::set<std::string>& codes,const std::function<void()>& action,const std::source_location& where=std::source_location::current()){
  try{action();}catch(const Error& e){require(codes.contains(e.code),"Unexpected "+e.code+": "+e.what());return e;}
  throw std::runtime_error("Expected failure at line "+std::to_string(where.line()));
}
struct Temp{fs::path root=temporary_directory(fs::canonical(fs::temp_directory_path()));~Temp(){std::error_code ignored;fs::remove_all(root,ignored);}};
Json selector(Json center,Json direction={1,0,0}){return {{"type","geometric"},{"feature_id","profile"},{"curve_kind","line"},{"expected_count",1},
  {"center",{{"point",center},{"tolerance",1e-6}}},{"direction",{{"vector",direction},{"tolerance",1e-6}}}};}
Json flange(const std::string& id="top",Json edge=selector({10,30,0}),Json angle=90){return {{"id",id},{"edge",edge},{"inside_radius",{{"parameter","radius"}}},{"angle_deg",angle},{"length",{{"parameter","length"}}}};}
Json model(Json flanges=Json::array({flange()}),bool flat=false){
  Json result={{"schema_version",1},{"units","mm"},{"parameters",{{"thickness",2},{"radius",3},{"length",10},{"k",.5}}},
    {"features",Json::array({{{"id","profile"},{"type","sketch"},{"workplane",{{"origin",{0,0,0}},{"normal",{0,0,1}},{"x_direction",{1,0,0}}}},
      {"profile",{{"type","rectangle"},{"width",20},{"height",30}}}},
      {{"id","formed"},{"type","sheet_metal"},{"input","profile"},{"thickness",{{"parameter","thickness"}}},{"k_factor",{{"parameter","k"}}},{"flanges",flanges}}})},{"output",flat?"flat":"formed"}};
  if(flat)result["features"].push_back({{"id","flat"},{"type","sheet_unfold"},{"input","formed"}});return result;
}
double expected(double angle=90,double width=20,double k=.5){return 1200+width*2*(10+std::abs(angle)*std::numbers::pi/180*(3+k*2));}
void geometry(){
  auto source=model();BuiltModel formed(source);near(formed.summary().at("volume_mm3"),expected());
  const auto report=formed.summary().at("sheet_metal");near(report.at("bends")[0].at("bend_allowance_mm"),2*std::numbers::pi);
  require(report.at("mode")=="formed"&&report.at("neutral_axis_basis")=="caller_supplied_k_factor"&&report.at("volume_preservation_assumed")==false,"K-factor and modeled-volume assumptions are explicit");
  require(formed.summary().at("solid_count")==1,"Base, bend and wall form an exact connected solid");
  const auto source_topology=formed.topology("profile"),formed_topology=formed.topology();std::set<std::string> sources,results;
  for(const auto* kind:{"faces","edges"}){for(const auto& entity:source_topology.at(kind))sources.insert(entity.at("id"));for(const auto& entity:formed_topology.at(kind))results.insert(entity.at("id"));}
  require(formed_topology.at("provenance").at("dependencies")==Json::array({"profile"})&&!formed_topology.at("provenance").at("history").empty(),"Formed sheet preserves available native base lineage and its sketch dependency");
  for(const auto& relation:formed_topology.at("provenance").at("history")){require(relation.at("source_feature_id")=="profile"&&sources.contains(relation.at("source_id")),"Sheet lineage names an actual sketch entity");if(relation.contains("result_id"))require(results.contains(relation.at("result_id")),"Sheet lineage names an actual completed result entity");}
  near(formed.summary().at("bounds_mm").at("min")[2],-2);near(formed.summary().at("bounds_mm").at("max")[1],35);near(formed.summary().at("bounds_mm").at("max")[2],13);
  Temp temp;const auto step=temp.root/"formed.step";formed.export_file(step,"step");STEPControl_Reader reader;
  require(reader.ReadFile(path_to_utf8(step).c_str())==IFSelect_RetDone&&reader.TransferRoots()>0,"Independent STEP reader loads the formed sheet");
  std::set<double> radii;for(TopExp_Explorer it(reader.OneShape(),TopAbs_FACE);it.More();it.Next()){BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));if(surface.GetType()==GeomAbs_Cylinder)radii.insert(surface.Cylinder().Radius());}
  require(radii.size()==2,"Bend contains exact inner/outer cylindrical surfaces");near(*radii.begin(),3);near(*radii.rbegin(),5);near(*radii.rbegin()-*radii.begin(),2);
  auto blank=model(Json::array({flange()}),true);BuiltModel unfolded(blank);near(unfolded.summary().at("volume_mm3"),expected());
  near(unfolded.summary().at("bounds_mm").at("max")[1],40+2*std::numbers::pi);near(unfolded.summary().at("bounds_mm").at("max")[2],0);
  require(unfolded.summary().at("sheet_metal").at("mode")=="flat","Unfold has source-qualified intent report");
  const auto flat_topology=unfolded.topology();for(const auto& face:flat_topology.at("faces"))require(face.at("surface_kind")=="plane","Unfold replaces cylindrical bends with a true planar blank");
  auto negative=model(Json::array({flange("down",selector({10,30,0}),-90)}));near(BuiltModel(negative).summary().at("volume_mm3"),expected());
  near(BuiltModel(negative).summary().at("bounds_mm").at("min")[2],-15);near(BuiltModel(negative).summary().at("bounds_mm").at("max")[2],0);
  for(const auto angle:{45,120,-45,-120})near(BuiltModel(model(Json::array({flange("sloped",selector({10,30,0}),angle)}))).summary().at("volume_mm3"),expected(angle));
  auto dimensions=model();dimensions["parameters"]["thickness"]=3;dimensions["parameters"]["radius"]=4;dimensions["parameters"]["length"]=7;
  const auto resized=BuiltModel(dimensions).summary();near(resized.at("volume_mm3"),1800+60*(7+5.5*std::numbers::pi/2));near(resized.at("bounds_mm").at("min")[2],-3);near(resized.at("bounds_mm").at("max")[1],37);near(resized.at("bounds_mm").at("max")[2],11);
  auto opposite=model(Json::array({flange(),flange("bottom",selector({10,0,0}),-90)}),true);near(BuiltModel(opposite).summary("formed").at("volume_mm3"),2*expected()-1200);near(BuiltModel(opposite).summary().at("volume_mm3"),2*expected()-1200);
  for(const auto k:{0.0,1.0}){auto extreme=model(Json::array({flange()}),true);extreme["parameters"]["k"]=k;near(BuiltModel(extreme).summary().at("volume_mm3"),expected(90,20,k));}
  blank["parameters"]["k"]=.3;const auto changed=BuiltModel(blank).summary();near(changed.at("volume_mm3"),expected(90,20,.3));
  near(changed.at("sheet_metal").at("formed_volume_mm3"),expected());require(std::abs(changed.at("volume_mm3").get<double>()-changed.at("sheet_metal").at("formed_volume_mm3").get<double>())>1,"Non-midplane K does not falsely claim equal modeled volumes");
  source["features"][1]["flanges"][0]["start_gap"]=2;source["features"][1]["flanges"][0]["end_gap"]=3;near(BuiltModel(source).summary().at("volume_mm3"),expected(90,15));
  const auto plate=BuiltModel(model(Json::array())).summary();near(plate.at("volume_mm3"),1200);require(plate.at("sheet_metal").at("bends").empty(),"Base sheet has no invented bend history");
  // Workplane YZ: profile X goes to world Y, profile Y goes to world Z.
  auto rotated=model();rotated["features"][0]["workplane"]={{"origin",{7,11,13}},{"normal",{1,0,0}},{"x_direction",{0,1,0}}};
  rotated["features"][1]["flanges"][0]["edge"]=selector({7,21,43},{0,1,0});const auto oriented=BuiltModel(rotated).summary();near(oriented.at("volume_mm3"),expected());near(oriented.at("bounds_mm").at("min")[0],5);near(oriented.at("bounds_mm").at("max")[0],20);
  // Native blank holes stay in the base after folding and developing.
  Json outer=Json::array({{{"type","line"},{"start",{0,0}},{"end",{20,0}}},{{"type","line"},{"start",{20,0}},{"end",{20,30}}},{{"type","line"},{"start",{20,30}},{"end",{0,30}}},{{"type","line"},{"start",{0,30}},{"end",{0,0}}}});
  Json inner=Json::array({{{"type","line"},{"start",{5,5}},{"end",{9,5}}},{{"type","line"},{"start",{9,5}},{"end",{9,9}}},{{"type","line"},{"start",{9,9}},{"end",{5,9}}},{{"type","line"},{"start",{5,9}},{"end",{5,5}}}});
  auto holed=model(Json::array({flange()}),true);holed["features"][0]["profile"]={{"type","wire"},{"segments",outer},{"holes",Json::array({inner})}};near(BuiltModel(holed).summary().at("volume_mm3"),expected()-32);near(BuiltModel(holed).summary("formed").at("volume_mm3"),expected()-32);
  holed["features"][1]["flanges"][0]["edge"]=selector({7,5,0});fails({"invalid_model"},[&]{BuiltModel invalid(holed);});
  auto curved=model(Json::array({flange("bottom",selector({10,0,0}))}),true);curved["features"][0]["profile"]={{"type","wire"},{"segments",Json::array({{{"type","arc"},{"start",{0,0}},{"mid",{10,10}},{"end",{20,0}}},{{"type","line"},{"start",{20,0}},{"end",{0,0}}}})}};
  const auto curved_built=BuiltModel(curved);near(curved_built.summary().at("sheet_metal").at("flat_area_mm2"),expected()/2-600+50*std::numbers::pi);near(curved_built.summary().at("volume_mm3"),expected()-1200+100*std::numbers::pi,1e-4);
  // OCCT 8.0.1 aborts the self-interference checker for this Bezier-based result.
  // Keep that limitation an explicit rejected candidate, without relaxing checks.
  curved["features"][0]["profile"]["segments"][0]={{"type","bezier"},{"points",{{0,0},{0,10},{20,10},{20,0}}}};
  const auto unsupported=fails({"invalid_shape"},[&]{BuiltModel invalid(curved);});require(unsupported.details.at("native_faults")[0].at("status")=="operation_aborted","Unverified Bezier sheet fails with honest native-check diagnostics");
}
void failures(){
  auto bad=model();bad["parameters"]["thickness"]=0;fails({"invalid_model"},[&]{validate_model(bad);});
  bad=model();bad["parameters"]["k"]=1.1;fails({"invalid_model"},[&]{validate_model(bad);});
  bad=model();bad["parameters"]["radius"]=0;fails({"invalid_model"},[&]{validate_model(bad);});
  for(const auto angle:{0,180,-180}){bad=model(Json::array({flange("invalid",selector({10,30,0}),angle)}));fails({"invalid_model"},[&]{validate_model(bad);});}
  bad=model();bad["features"][1]["flanges"][0]["edge"]["center"]["point"]={100,100,0};const auto absent=fails({"selection_missing"},[&]{BuiltModel invalid(bad);});require(absent.details.at("flange_id")=="top","Failures name the offending flange");
  bad=model();bad["features"][1]["flanges"][0]["edge"].erase("center");fails({"selection_ambiguous"},[&]{BuiltModel invalid(bad);});
  bad=model();bad["features"][1]["flanges"][0]["start_gap"]=21;fails({"invalid_model"},[&]{BuiltModel invalid(bad);});
  bad=model(Json::array({flange("one"),flange("two")}));fails({"invalid_model"},[&]{BuiltModel invalid(bad);});
  bad=model();bad["features"][1]["flanges"][0]["edge"]["feature_id"]="formed";fails({"invalid_model"},[&]{validate_model(bad);});
  bad=model(Json::array({flange("top",selector({10,30,0}),160),flange("right",selector({20,15,0},{0,1,0}),160)}));bad["parameters"]["length"]=40;fails({"invalid_model","invalid_shape"},[&]{BuiltModel invalid(bad);});
  // Opposing flanges in a narrow concave pocket cannot silently intersect.
  bad=model();bad["features"][0]["profile"]={{"type","polygon"},{"points",{{0,0},{20,0},{20,4},{4,4},{4,30},{0,30}}}};
  bad["features"][1]["flanges"][0]["edge"]=selector({12,4,0});bad["features"][1]["flanges"][0]["angle_deg"]=-160;bad["parameters"]["length"]=40;
  // A narrow U-pocket makes the curved bend collision deterministic.
  bad["features"][0]["profile"]["points"]={{0,0},{20,0},{20,30},{12,30},{12,4},{8,4},{8,30},{0,30}};
  bad["features"][1]["flanges"][0]["edge"]=selector({8,17,0},{0,1,0});bad["features"][1]["flanges"][0]["angle_deg"]=90;
  bad["features"][1]["flanges"].push_back(flange("opposite",selector({12,17,0},{0,1,0}),90));
  fails({"invalid_model","invalid_shape"},[&]{BuiltModel invalid(bad);});
  // Wider opposing bends are valid when formed, but their flat extensions overlap.
  bad=model(Json::array({flange("left",selector({4,17,0},{0,1,0})),flange("right",selector({16,17,0},{0,1,0}))}));
  bad["features"][0]["profile"]={{"type","polygon"},{"points",{{0,0},{20,0},{20,30},{16,30},{16,4},{4,4},{4,30},{0,30}}}};
  require(BuiltModel(bad).summary().at("solid_count")==1,"Separated concave flanges form a valid folded solid");bad["features"].push_back({{"id","flat"},{"type","sheet_unfold"},{"input","formed"}});bad["output"]="flat";
  fails({"invalid_model","invalid_shape"},[&]{BuiltModel invalid(bad);});
  bad=model();bad["features"].push_back({{"id","altered"},{"type","transform"},{"input","formed"},{"translation",{1,0,0}}});bad["features"].push_back({{"id","flat"},{"type","sheet_unfold"},{"input","altered"}});bad["output"]="flat";fails({"invalid_model"},[&]{validate_model(bad);});
}
Json job(Service& service,const Json& args){for(int i=0;i<1000;++i){try{return service.call("cad_job",args);}catch(const Error& e){if(e.code!="workspace_busy")throw;}std::this_thread::sleep_for(std::chrono::milliseconds(5));}throw std::runtime_error("Job metadata remains busy");}
Json finish(Service& service,const std::string& id){auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);while(std::chrono::steady_clock::now()<deadline){auto state=job(service,{{"action","get"},{"job_id",id}});if(state.at("state")=="succeeded"||state.at("state")=="failed")return state;std::this_thread::sleep_for(std::chrono::milliseconds(20));}throw std::runtime_error("Job did not terminate");}
void lifecycle(){
  Temp temp;Service service(temp.root);const auto original=service.call("cad_create",{{"document_id","sheet"},{"model",model(Json::array({flange()}),true)}});near(original.at("summary").at("volume_mm3"),expected());
  fs::remove_all(temp.root/".cache");Service reopened(temp.root);near(reopened.call("cad_query",{{"document_id","sheet"},{"revision",1}}).at("summary").at("volume_mm3"),expected());
  Json diagnostics;const auto first=evaluate_model(temp.root,original.at("model"),{{"kind","view"}},&diagnostics);fs::remove(temp.root/".cache"/(geometry_cache_key(original.at("model"))+".json"));
  const auto cached=evaluate_model(temp.root,original.at("model"),{{"kind","view"}},&diagnostics);require(diagnostics.at("feature_hits").at("formed")==true&&diagnostics.at("feature_hits").at("flat")==true,"Both sheet modes restore exact feature snapshots");require(cached.at("summary").at("sheet_metal")==first.at("summary").at("sheet_metal")&&cached.at("topology")==first.at("topology"),"Cache preserves exact developed bend intent and native topology provenance");
  auto changed=reopened.call("cad_apply",{{"document_id","sheet"},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","k"},{"value",.3}}})}});near(changed.at("summary").at("volume_mm3"),expected(90,20,.3));
  const auto old_keys=feature_cache_keys(original.at("model")),new_keys=feature_cache_keys(changed.at("model"));require(old_keys.at("profile")==new_keys.at("profile")&&old_keys.at("formed")!=new_keys.at("formed")&&old_keys.at("flat")!=new_keys.at("flat"),"Neutral-axis edits preserve the base cache and invalidate the full bend-intent closure");
  const auto before=reopened.call("cad_read",{{"document_id","sheet"}});fails({"invalid_model"},[&]{reopened.call("cad_apply",{{"document_id","sheet"},{"expected_revision",2},{"operations",Json::array({{{"op","set_parameter"},{"name","thickness"},{"value",0}}})}});});require(reopened.call("cad_read",{{"document_id","sheet"}})==before,"Invalid material edit preserves complete committed record");
  auto colliding=before.at("model").at("features")[1];colliding["flanges"]=Json::array({flange("top",selector({10,30,0}),160),flange("right",selector({20,15,0},{0,1,0}),160)});
  fails({"invalid_model","invalid_shape"},[&]{reopened.call("cad_apply",{{"document_id","sheet"},{"expected_revision",2},{"operations",Json::array({{{"op","set_parameter"},{"name","length"},{"value",40}},{{"op","replace_feature"},{"id","formed"},{"feature",colliding}}})}});});require(reopened.call("cad_read",{{"document_id","sheet"}})==before,"A geometry collision preserves the complete committed record");
  const auto drawing=reopened.call("cad_drawing",{{"document_id","sheet"},{"revision",2},{"drawing",{{"formats",{"dxf"}},{"views",Json::array({{{"id","top"},{"orientation","top"}}})}}}});
  bool dxf=false;for(const auto& artifact:drawing.at("artifacts"))if(artifact.at("format")=="dxf"){const auto contents=read_text(path_from_utf8(artifact.at("path")),16*1024*1024);require(contents.find("LINE")!=std::string::npos&&contents.find("EOF")!=std::string::npos,"Developed blank produces independent exact-outline DXF");dxf=true;}require(dxf,"Unfold top-view DXF is published");
  for(const auto* format:{"step","stl","3mf"})for(const auto* feature:{"formed","flat"}){const auto exported=reopened.call("cad_export",{{"document_id","sheet"},{"revision",2},{"feature_id",feature},{"format",format}});require(fs::exists(path_from_utf8(exported.at("path"))),"Each mode exports independently");}
  auto seed=model(Json::array());seed["features"]=Json::array({{{"id","seed"},{"type","box"},{"size",{1,1,1}}}});seed["output"]="seed";reopened.call("cad_create",{{"document_id","consumer"},{"model",seed}});
  auto captured=reopened.call("cad_apply",{{"document_id","consumer"},{"expected_revision",1},{"operations",Json::array({{{"op","set_component"},{"id","blank"},{"source_document_id","sheet"},{"source_revision",2}},{{"op","set_output"},{"feature_id","blank"}}})}});
  for(const auto& feature:captured.at("model").at("features"))if(feature.at("type")=="sheet_metal")require(feature.at("flanges")[0].at("edge").at("feature_id")==feature.at("input"),"Component rewrites nested flange edge references");
  Temp portable;Service independent(portable.root);fs::remove_all(temp.root/"documents/sheet");near(independent.call("cad_create",{{"document_id","portable"},{"model",captured.at("model")}}).at("summary").at("volume_mm3"),expected(90,20,.3));
  const Json submit={{"action","submit"},{"request_id","sheetJob"},{"tool","cad_create"},{"arguments",{{"document_id","jobSheet"},{"model",model(Json::array({flange()}),true)}}}};
  job(independent,submit);const auto result=finish(independent,"sheetJob");require(result.at("state")=="succeeded",result.dump());near(result.at("result").at("summary").at("volume_mm3"),expected());
  job(independent,{{"action","submit"},{"request_id","badJob"},{"tool","cad_apply"},{"arguments",{{"document_id","jobSheet"},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","radius"},{"value",0}}})}}}});require(finish(independent,"badJob").at("state")=="failed","Invalid bend job fails");require(independent.call("cad_read",{{"document_id","jobSheet"}}).at("revision")==1,"Failed bend job preserves HEAD");
}
}
int main(){try{set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));geometry();failures();lifecycle();std::cout<<checks<<" sheet-metal checks passed\n";return 0;}catch(const Error& e){std::cerr<<"Sheet-metal failure: "<<e.code<<": "<<e.what()<<" "<<e.details.dump()<<'\n';return 1;}catch(const std::exception& e){std::cerr<<"Sheet-metal failure: "<<e.what()<<'\n';return 1;}}
