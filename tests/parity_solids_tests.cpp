#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include "agentcad/cache.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/hash.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <numbers>
#include <set>
using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
void near(double actual,double expected,double tolerance=1e-5){require(std::abs(actual-expected)<=tolerance,"Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
Error fails(const std::function<void()>& action,const std::string& feature="part") {try{action();}catch(const Error& e){require(e.details.value("feature_id",std::string())==feature,"Failure carries feature identity: "+e.json().dump());return e;}throw std::runtime_error("Expected feature failure");}
struct Temp {fs::path root=temporary_directory(fs::canonical(fs::temp_directory_path()));~Temp(){std::error_code ignored;fs::remove_all(root,ignored);}};
Json plane(Json origin={0,0,0}){return {{"origin",origin},{"normal",{0,0,1}},{"x_direction",{1,0,0}}};}
Json sketch(const std::string& id,Json profile,Json origin={0,0,0}){return {{"id",id},{"type","sketch"},{"workplane",plane(origin)},{"profile",profile}};}
Json circle(double radius){return {{"type","circle"},{"radius",radius}};}
Json model(Json features,const std::string& output="part"){return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",features},{"output",output}};}
Json box(){return {{"id","base"},{"type","box"},{"size",{20,30,10}}};}
Json face(Json normal,const std::string& input="base"){return {{"type","geometric"},{"feature_id",input},{"surface_kind","plane"},{"expected_count",1},{"normal",{{"vector",normal},{"tolerance",1e-6}}}};}
Json edge(){return {{"type","geometric"},{"feature_id","base"},{"curve_kind","line"},{"expected_count",1},{"center",{{"point",{20,15,10}},{"tolerance",1e-6}}}};}
Json scale(Json factors=2){return model(Json::array({box(),{{"id","part"},{"type","scale"},{"input","base"},{"factors",factors},{"origin",{1,2,3}}}}));}
Json draft(){return model(Json::array({box(),{{"id","part"},{"type","draft"},{"input","base"},{"faces",face({1,0,0})},{"angle_deg",5},{"direction",{0,0,1}},{"neutral_plane",plane()}}}));}
Json chamfer(){return model(Json::array({box(),{{"id","part"},{"type","chamfer"},{"input","base"},{"edges",edge()},{"distance",2},{"distance2",3},{"reference_face",face({0,0,1})}}}));}
Json twist(Json angle=90,Json distance=10){return model(Json::array({sketch("profile",{{"type","rectangle"},{"width",4},{"height",2}}),{{"id","part"},{"type","twist_extrude"},{"input","profile"},{"distance",distance},{"angle_deg",angle}}}));}
Json ring(double outer,double inner){Json segments=Json::array({{{"type","arc"},{"start",{outer,0}},{"mid",{0,outer}},{"end",{-outer,0}}},{{"type","arc"},{"start",{-outer,0}},{"mid",{0,-outer}},{"end",{outer,0}}}});Json hole=Json::array({{{"type","arc"},{"start",{inner,0}},{"mid",{0,inner}},{"end",{-inner,0}}},{{"type","arc"},{"start",{-inner,0}},{"mid",{0,-inner}},{"end",{inner,0}}}});return {{"type","wire"},{"segments",segments},{"holes",Json::array({hole})}};}
Json loft(){return model(Json::array({sketch("a",ring(5,2)),sketch("b",ring(8,3),{0,0,10}),{{"id","part"},{"type","loft"},{"sections",{"a","b"}},{"ruled",true}}}));}
void lineage(const BuiltModel& built){auto topology=built.topology();std::set<std::string> results;for(const auto* key:{"faces","edges"})for(const auto& item:topology.at(key))results.insert(item.at("id"));require(!topology.at("provenance").at("history").empty(),"Operation records native history");for(const auto& item:topology.at("provenance").at("history"))if(item.contains("result_id"))require(results.contains(item.at("result_id")),"History result belongs to final feature");}
void signed_moment_tests(){
  Temp temp;set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));Service service(temp.root);
  const auto base=model(Json::array({{{"id","part"},{"type","box"},{"size",{10,20,30}}}}));
  int fixture=0;
  const auto verify=[&](const Json& intent,double mass,const std::array<double,3>& center,double tolerance=1e-9){
    const auto check_summary=[&](const Json& summary){near(summary.at("volume_mm3"),mass,1e-5);for(int axis=0;axis<3;++axis)near(summary.at("center_of_mass_mm")[axis],center[axis],tolerance);};
    BuiltModel built(intent);check_summary(built.summary());check_summary(BuiltModel(intent,built.snapshot()).summary());
    const auto id="moments"+std::to_string(++fixture);check_summary(service.call("cad_create",{{"document_id",id},{"model",intent}}).at("summary"));
    const auto exported=service.call("cad_export",{{"document_id",id},{"revision",1},{"format","step"}});
    const auto imported=service.call("cad_import",{{"document_id",id+"step"},{"path",exported.at("path")}});check_summary(imported.at("summary"));
    // The captured import remains sufficient after its original file disappears.
    fs::remove(path_from_utf8(text_field(exported,"path")));fs::remove_all(temp.root/".cache");check_summary(service.call("cad_query",{{"document_id",id+"step"},{"revision",1}}).at("summary"));
  };
  // At the exterior integration origin, the first three face fluxes of this
  // box sum to zero mass but retain a nonzero first moment. Do not normalize
  // the signed partial sums as though each prefix were a material solid.
  verify(base,6000,{5,10,15});
  auto transformed=base;transformed["features"].push_back({{"id","placed"},{"type","transform"},{"input","part"},{"translation",{123,-47,22}},
    {"rotation",{{"origin",{0,0,0}},{"axis",{2,-1,3}},{"angle_deg",37}}}});transformed["output"]="placed";
  // Independent Rodrigues rotation of the analytic box centroid.
  const double angle=37*std::numbers::pi/180,c=std::cos(angle),sn=std::sin(angle),norm=std::sqrt(14.);
  const std::array<double,3> axis={2/norm,-1/norm,3/norm},source={5,10,15},translation={123,-47,22};
  const std::array<double,3> cross={axis[1]*source[2]-axis[2]*source[1],axis[2]*source[0]-axis[0]*source[2],axis[0]*source[1]-axis[1]*source[0]};
  const double dot=axis[0]*source[0]+axis[1]*source[1]+axis[2]*source[2];std::array<double,3> center;
  for(int k=0;k<3;++k)center[k]=translation[k]+c*source[k]+sn*cross[k]+(1-c)*dot*axis[k];
  verify(transformed,6000,center);
  // Cubic roof z=(2*x/3-1)^3 over z=-1, 0<=x<=3, 0<=y<=1.
  // Its exact mass is 3, centroid (9/5,1/2,-3/7). The small second
  // component conditions the bounding reference so the curved face's signed
  // mass is zero on pinned OCCT 8.0.1, but its x first moment is nonzero.
  // Other floating-point implementations may retain a near-zero face mass;
  // both cases must preserve the same independently integrated solid moments.
  const double marker_x=3.00000025-.1;
  auto cancellation=model(Json::array({
    Json{{"id","profile"},{"type","sketch"},{"workplane",{{"origin",{0,0,0}},{"normal",{0,-1,0}},{"x_direction",{1,0,0}}}},
      {"profile",{{"type","wire"},{"segments",Json::array({Json{{"type","line"},{"start",{0,-1}},{"end",{3,-1}}},Json{{"type","line"},{"start",{3,-1}},{"end",{3,1}}},Json{{"type","bezier"},{"points",{{3,1},{2,-1},{1,1},{0,-1}}}}})}}}},
    Json{{"id","wedge"},{"type","extrude"},{"input","profile"},{"distance",-1}},
    Json{{"id","marker"},{"type","box"},{"origin",{marker_x,.3,-.5}},{"size",{.1,.1,.1}}},
    Json{{"id","part"},{"type","assembly"},{"parts",Json::array({Json{{"id","roof"},{"input","wedge"}},Json{{"id","small"},{"input","marker"}}})}}
  }));
  const std::array<double,3> wedge_center={1.8,.5,-3./7},marker_center={marker_x+.05,.35,-.45};
  for(int k=0;k<3;++k)center[k]=(3*wedge_center[k]+.001*marker_center[k])/3.001;
  verify(cancellation,3.001,center,1e-7);
  // The fused variant keeps the cancellation inside one closed solid, so
  // solid-local conditioning must still recover its lost face moments. The
  // protruding box sliver is bounded by OCCT's modeling tolerance; integrate
  // its authored dimensions independently instead of borrowing native results.
  auto fused=cancellation;fused["features"].back()={{"id","part"},{"type","fuse"},{"left","wedge"},{"right","marker"}};
  const double sliver=(marker_x+.1-3)*.1*.1;const std::array<double,3> sliver_center={(marker_x+.1+3)/2,.35,-.45};
  for(int k=0;k<3;++k)center[k]=(3*wedge_center[k]+sliver*sliver_center[k])/(3+sliver);
  verify(fused,3+sliver,center,1e-7);
  auto mixed=base;mixed["features"].push_back({{"id","cylinder"},{"type","cylinder"},{"radius",5},{"height",10}});
  mixed["features"].push_back({{"id","ellipse"},{"type","scale"},{"input","cylinder"},{"factors",{2,3,.5}},{"origin",{1,2,3}}});
  mixed["features"].push_back({{"id","mixed"},{"type","assembly"},{"parts",Json::array({Json{{"id","box"},{"input","part"}},Json{{"id","elliptic"},{"input","ellipse"},{"placement",{{"translation",{50,60,70}}}}}})}});mixed["output"]="mixed";
  const double ellipse_mass=750*std::numbers::pi,total=6000+ellipse_mass;const std::array<double,3> ellipse_center={49,56,74};
  for(int k=0;k<3;++k)center[k]=(6000*source[k]+ellipse_mass*ellipse_center[k])/total;
  verify(mixed,total,center,1e-6);
  std::reverse(mixed["features"].back()["parts"].begin(),mixed["features"].back()["parts"].end());verify(mixed,total,center,1e-6);
  // An assembly-wide flux origin loses digits when tiny closed bodies are far
  // apart. Each positive solid has exact local moments even at this separation.
  auto separated=model(Json::array({Json{{"id","near"},{"type","box"},{"size",{1,1,1}}},
    Json{{"id","far"},{"type","box"},{"origin",{1000000,0,0}},{"size",{1,1,1}}},
    Json{{"id","part"},{"type","assembly"},{"parts",Json::array({Json{{"id","near"},{"input","near"}},Json{{"id","far"},{"input","far"}}})}}}));
  verify(separated,2,{500000.5,.5,.5});
  std::reverse(separated["features"].back()["parts"].begin(),separated["features"].back()["parts"].end());verify(separated,2,{500000.5,.5,.5});
  separated["features"][1]["origin"]={1000000,-500000,250000};separated["features"][1]["size"]={2,3,4};
  const std::array<double,3> far_center={1000001,-499998.5,250002};
  for(int k=0;k<3;++k)center[k]=(.5+24*far_center[k])/25;
  verify(separated,25,center);
  separated["features"].push_back({{"id","placed"},{"type","assembly"},{"parts",Json::array({Json{{"id","pair"},{"input","part"},{"placement",{{"translation",{123,456,789}},{"rotation",{{"origin",{0,0,0}},{"axis",{0,0,1}},{"angle_deg",90}}}}}}})}});separated["output"]="placed";
  verify(separated,25,{-center[1]+123,center[0]+456,center[2]+789});
  // Coincident assembly instances remain two occurrences, not a fused union.
  separated["features"]=Json::array({Json{{"id","cube"},{"type","box"},{"size",{1,1,1}}},Json{{"id","part"},{"type","assembly"},{"parts",Json::array({Json{{"id","first"},{"input","cube"}},Json{{"id","second"},{"input","cube"}}})}}});separated["output"]="part";
  verify(separated,2,{.5,.5,.5});
}
void scale_tests(){
  auto intent=scale();BuiltModel uniform(intent);near(uniform.summary().at("volume_mm3"),48000);near(uniform.summary().at("bounds_mm").at("min")[0],-1);near(uniform.summary().at("bounds_mm").at("max")[2],17);lineage(uniform);
  intent=scale({2,3,.5});BuiltModel anisotropic(intent);near(anisotropic.summary().at("volume_mm3"),18000);near(anisotropic.summary().at("bounds_mm").at("max")[1],86);lineage(anisotropic);
  intent["features"][0]={{"id","base"},{"type","cylinder"},{"radius",5},{"height",10}};auto elliptical=BuiltModel(intent).summary();near(elliptical.at("volume_mm3"),750*std::numbers::pi,1e-4);near(elliptical.at("center_of_mass_mm")[0],-1);near(elliptical.at("center_of_mass_mm")[1],-4);near(elliptical.at("center_of_mass_mm")[2],4);
  for(auto factors:Json::array({0,-1,Json::array({1,0,1}),Json::array({1,2})})){intent=scale(factors);fails([&]{BuiltModel invalid(intent);});}
}
void draft_tests(){
  auto intent=draft();BuiltModel narrowed(intent);const double delta=10*std::tan(5*std::numbers::pi/180);near(narrowed.summary().at("volume_mm3"),6000-150*delta);lineage(narrowed);
  intent["features"].back()["angle_deg"]=-5;near(BuiltModel(intent).summary().at("volume_mm3"),6000+150*delta);
  intent=draft();intent["features"].back()["neutral_plane"]=plane({0,0,5});near(BuiltModel(intent).summary().at("volume_mm3"),6000);
  intent=draft();intent["features"][0]={{"id","base"},{"type","cylinder"},{"radius",5},{"height",10}};intent["features"].back()["faces"]={{"type","geometric"},{"feature_id","base"},{"surface_kind","cylinder"},{"expected_count",1}};
  near(BuiltModel(intent).summary().at("volume_mm3"),std::numbers::pi*10.0/3*(25+5*(5-delta)+(5-delta)*(5-delta)),1e-4);
  intent=draft();intent["features"].back()["faces"].erase("normal");fails([&]{BuiltModel invalid(intent);});
  intent=draft();intent["features"].back()["direction"]={1,0,0};fails([&]{BuiltModel invalid(intent);});
  intent=draft();intent["features"].back()["angle_deg"]=89;fails([&]{validate_model(intent);});
}
void chamfer_tests(){
  auto intent=chamfer();BuiltModel cut(intent);near(cut.summary().at("volume_mm3"),5910);lineage(cut);
  auto cut_topology=cut.topology();bool bevel=false;for(const auto& face:cut_topology.at("faces")){const auto c=face.at("center_mm");if(std::abs(c[0].get<double>()-19)<1e-6&&std::abs(c[2].get<double>()-8.5)<1e-6)bevel=true;}require(bevel,"First chamfer distance is measured on the reference face");
  intent["features"].back().erase("distance2");intent["features"].back()["angle_deg"]=std::atan(1.5)*180/std::numbers::pi;near(BuiltModel(intent).summary().at("volume_mm3"),5910);
  intent=chamfer();intent["features"].back()["reference_face"]=face({1,0,0});near(BuiltModel(intent).summary().at("volume_mm3"),5910);
  auto swapped_topology=BuiltModel(intent).topology();bevel=false;for(const auto& face:swapped_topology.at("faces")){const auto c=face.at("center_mm");if(std::abs(c[0].get<double>()-18.5)<1e-6&&std::abs(c[2].get<double>()-9)<1e-6)bevel=true;}require(bevel,"Changing reference face swaps the asymmetric distances");
  intent=chamfer();intent["features"].back()["reference_face"]=face({-1,0,0});fails([&]{BuiltModel invalid(intent);});
  intent=chamfer();intent["features"].back().erase("reference_face");fails([&]{validate_model(intent);});
  intent=chamfer();intent["features"].back()["angle_deg"]=45;fails([&]{validate_model(intent);});
  intent=chamfer();intent["features"].back()["distance"]=50;fails([&]{BuiltModel invalid(intent);});
}
void twist_tests(){
  for(double angle:{0.,90.,-90.,360.})for(double distance:{10.,-10.}){auto intent=twist(angle,distance);BuiltModel twisted(intent);near(twisted.summary().at("volume_mm3"),80,2e-3);require(twisted.summary().at("solid_count")==1,"Twist is one exact solid");near(twisted.summary().at("bounds_mm").at(distance>0?"max":"min")[2],distance,1e-5);lineage(twisted);
    auto topology=twisted.topology();bool end=false;for(const auto& face:topology.at("faces"))if(face.at("surface_kind")=="plane"&&std::abs(face.at("center_mm")[2].get<double>()-distance)<1e-6){const double radians=angle*std::numbers::pi/180;near(face.at("center_mm")[0],2*std::cos(radians)-std::sin(radians),1e-5);near(face.at("center_mm")[1],2*std::sin(radians)+std::cos(radians),1e-5);near(face.at("area_mm2"),8,1e-5);end=true;}require(end,"Twist end cap is rotated about the authored axis");}
  auto intent=twist(90);intent["features"][0]["profile"]=ring(5,2);near(BuiltModel(intent).summary().at("volume_mm3"),210*std::numbers::pi,2e-3);
  intent=twist();intent["features"].back()["center"]={2,1,0};near(BuiltModel(intent).summary().at("volume_mm3"),80,2e-3);
  intent["features"][0]["workplane"]={{"origin",{1,2,3}},{"normal",{1,0,0}},{"x_direction",{0,1,0}}};intent["features"].back().erase("center");near(BuiltModel(intent).summary().at("volume_mm3"),80,2e-3);
  intent=twist();intent["features"].back()["center"]={0,0,1};fails([&]{BuiltModel invalid(intent);});
  intent=twist(6000);fails([&]{validate_model(intent);});
}
void loft_tests(){
  auto intent=loft();BuiltModel hollow(intent);near(hollow.summary().at("volume_mm3"),std::numbers::pi*10.0/3*(25+40+64-4-6-9),1e-4);lineage(hollow);
  intent=model(Json::array({sketch("a",circle(5)),{{"id","part"},{"type","loft"},{"sections",{"a"}},{"end_vertex",{0,0,10}},{"ruled",true}}}));near(BuiltModel(intent).summary().at("volume_mm3"),250*std::numbers::pi/3,1e-4);
  intent["features"].back()["start_vertex"]={0,0,-5};near(BuiltModel(intent).summary().at("volume_mm3"),125*std::numbers::pi,1e-4);
  intent=loft();intent["features"][1]["profile"]=circle(8);fails([&]{BuiltModel invalid(intent);});
  intent=loft();intent["features"][1]["workplane"]["origin"][2]=0;fails([&]{BuiltModel invalid(intent);});
  // Two circular holes have deliberately explicit correspondence, independent
  // of kernel wire enumeration or a section's frame orientation.
  auto profile=ring(8,1);auto hole=profile["holes"][0];for(auto& segment:profile["holes"][0])for(const auto* key:{"start","mid","end"})segment[key][0]=segment[key][0].get<double>()-3;
  for(auto& segment:hole)for(const auto* key:{"start","mid","end"})segment[key][0]=segment[key][0].get<double>()+3;profile["holes"].push_back(hole);
  intent=model(Json::array({sketch("a",profile),sketch("b",profile,{0,0,10}),{{"id","part"},{"type","loft"},{"sections",{"a","b"}},{"ruled",true}}}));fails([&]{BuiltModel invalid(intent);});
  intent["features"].back()["hole_order"]=Json::array({Json::array({Json{{"point",{-3,0,0}},{"tolerance",1e-6}},Json{{"point",{3,0,0}},{"tolerance",1e-6}}}),Json::array({Json{{"point",{-3,0,10}},{"tolerance",1e-6}},Json{{"point",{3,0,10}},{"tolerance",1e-6}}})});near(BuiltModel(intent).summary().at("volume_mm3"),620*std::numbers::pi,1e-4);
  auto crossing=intent;std::swap(crossing["features"].back()["hole_order"][1][0],crossing["features"].back()["hole_order"][1][1]);fails([&]{BuiltModel invalid(crossing);});
  intent["features"].back()["hole_order"][0][0]["tolerance"]=10;fails([&]{BuiltModel invalid(intent);});
}
void edit_tests(){
  Temp temp;Service service(temp.root);int index=0;
  for(auto intent:std::vector<Json>{scale(),draft(),chamfer(),twist(),loft()}) {
    const auto id="edit"+std::to_string(++index);const auto type=intent["features"].back()["type"].get<std::string>();
    double initial=2,changed=3,invalid=-1;
    if(type=="scale")intent["features"].back()["factors"]={{"parameter","value"}};
    if(type=="draft"){initial=5;changed=8;invalid=85;intent["features"].back()["angle_deg"]={{"parameter","value"}};}
    if(type=="chamfer"){invalid=50;intent["features"].back()["distance"]={{"parameter","value"}};}
    if(type=="twist_extrude"){initial=90;changed=180;invalid=6000;intent["features"].back()["angle_deg"]={{"parameter","value"}};}
    if(type=="loft"){initial=10;changed=12;invalid=0;intent["features"][1]["workplane"]["origin"][2]={{"parameter","value"}};}
    intent["parameters"]["value"]=initial;auto original=service.call("cad_create",{{"document_id",id},{"model",intent}});const auto original_source=service.call("cad_read",{{"document_id",id},{"revision",original.at("revision")}}).at("model");
    auto revised=service.call("cad_apply",{{"document_id",id},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","value"},{"value",changed}}})}});
    require(revised.at("revision")==2,"Operation parameter edit commits");auto saved=service.call("cad_read",{{"document_id",id}});
    fails([&]{service.call("cad_apply",{{"document_id",id},{"expected_revision",2},{"operations",Json::array({{{"op","set_parameter"},{"name","value"},{"value",invalid}}})}});});
    require(service.call("cad_read",{{"document_id",id}})==saved,"Invalid geometry or dimension preserves complete operation revision");
    require(service.call("cad_read",{{"document_id",id},{"revision",1}}).at("model")==original_source,"Edit preserves historical source");
    Json diagnostics;fs::remove_all(temp.root/".cache");evaluate_model(temp.root,intent,{{"kind","view"}},&diagnostics);const auto keys=feature_cache_keys(intent);intent["parameters"]["value"]=changed;
    const auto rebuilt=evaluate_model(temp.root,intent,{{"kind","view"}},&diagnostics);require(diagnostics.at("feature_hits").at("part")==false,"Parameter change rebuilds new operation");
    near(rebuilt.at("summary").at("volume_mm3"),BuiltModel(intent).summary().at("volume_mm3"),1e-4);require(keys.at("part")!=feature_cache_keys(intent).at("part"),"Parameter changes dependent cache identity");
  }
}
void lifecycle(){
  Temp temp;set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));Service service(temp.root);
  std::vector<Json> intents={scale(),draft(),chamfer(),twist(),loft(),scale({2,3,.5})};auto elliptical=scale({2,3,.5});elliptical["features"][0]={{"id","base"},{"type","cylinder"},{"radius",5},{"height",10}};intents.push_back(elliptical);int index=0;
  for(auto intent:intents){const auto id="solid"+std::to_string(++index);const auto expected=BuiltModel(intent).summary();service.call("cad_create",{{"document_id",id},{"model",intent}});
    auto exported=service.call("cad_export",{{"document_id",id},{"revision",1},{"format","step"}});auto readback=service.call("cad_import",{{"document_id",id+"readback"},{"path",exported.at("path")}});near(readback.at("summary").at("volume_mm3"),expected.at("volume_mm3"),3e-3);require(readback.at("summary").at("solid_count")==expected.at("solid_count"),"STEP retains solid count");
    for(const auto* side:{"min","max"})for(int axis=0;axis<3;++axis)near(readback.at("summary").at("bounds_mm").at(side)[axis],expected.at("bounds_mm").at(side)[axis],3e-4);
    Json diagnostics;const auto first=evaluate_model(temp.root,intent,{{"kind","view"}},&diagnostics);fs::remove(temp.root/".cache"/(geometry_cache_key(intent)+".json"));const auto warm=evaluate_model(temp.root,intent,{{"kind","view"}},&diagnostics);require(diagnostics.at("feature_hits").at("part")==true,"Operation reuses exact feature cache");require(first.at("topology")==warm.at("topology"),"Cache retains final topology and history");
    near(BuiltModel(intent,BuiltModel(intent).snapshot()).summary().at("volume_mm3"),expected.at("volume_mm3"),1e-4);
    auto altered=intent;altered["features"].back()["unexpected"]=true;fails([&]{service.call("cad_apply",{{"document_id",id},{"expected_revision",1},{"operations",Json::array({{{"op","replace_feature"},{"id","part"},{"feature",altered["features"].back()}}})}});});require(service.call("cad_read",{{"document_id",id}}).at("revision")==1,"Invalid operation keeps HEAD");
    service.call("cad_create",{{"document_id",id+"consumer"},{"model",model(Json::array({box()}),"base")}});auto captured=service.call("cad_apply",{{"document_id",id+"consumer"},{"expected_revision",1},{"operations",Json::array({{{"op","set_component"},{"id","component"},{"source_document_id",id},{"source_revision",1}},{{"op","set_output"},{"feature_id","component"}}})}});near(captured.at("summary").at("volume_mm3"),expected.at("volume_mm3"),1e-4);
    const auto captured_source=service.call("cad_read",{{"document_id",id+"consumer"},{"revision",captured.at("revision")}}).at("model");
    Temp portable;Service isolated(portable.root);near(isolated.call("cad_create",{{"document_id","portable"},{"model",captured_source}}).at("summary").at("volume_mm3"),expected.at("volume_mm3"),1e-4);
    if(index==3)require(captured_source.at("features").back().at("reference_face").at("feature_id")==captured_source.at("features").back().at("input"),"Component remaps asymmetric reference face");
  }
  // Scaling opaque captured STEP stays independent of its source path.
  auto record=service.call("cad_read",{{"document_id","solid1readback"}});auto imported=record.at("model");const auto source=imported.at("output").get<std::string>();imported["features"].push_back({{"id","scaled"},{"type","scale"},{"input",source},{"origin",{0,0,0}},{"factors",{.5,2,1}}});imported["output"]="scaled";near(BuiltModel(imported).summary().at("volume_mm3"),48000,1e-4);require(imported_step_source(imported,"scaled")==nullptr,"Scaling does not claim unchanged purchased-part geometry");
  auto edit=scale(Json{{"parameter","factor"}});edit["parameters"]["factor"]=2;service.call("cad_create",{{"document_id","editable"},{"model",edit}});service.call("cad_apply",{{"document_id","editable"},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","factor"},{"value",3}}})}});auto saved=service.call("cad_read",{{"document_id","editable"}});near(BuiltModel(saved.at("model")).summary().at("volume_mm3"),162000);fails([&]{service.call("cad_apply",{{"document_id","editable"},{"expected_revision",2},{"operations",Json::array({{{"op","set_parameter"},{"name","factor"},{"value",-1}}})}});});require(service.call("cad_read",{{"document_id","editable"}})==saved,"Invalid factor preserves complete revision");
  const auto before=feature_cache_keys(edit);edit["parameters"]["factor"]=3;const auto after=feature_cache_keys(edit);require(before.at("base")==after.at("base")&&before.at("part")!=after.at("part"),"Scale parameter selectively invalidates cache");
}
}
int main(){try{configure_kernel_logging();signed_moment_tests();scale_tests();draft_tests();chamfer_tests();twist_tests();loft_tests();lifecycle();edit_tests();std::cout<<checks<<" solid parity checks passed\n";return 0;}catch(const Error& e){std::cerr<<e.json().dump()<<"\n";return 1;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
