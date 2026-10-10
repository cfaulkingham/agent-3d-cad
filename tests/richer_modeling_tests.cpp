#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include "agentcad/cache.hpp"
#include "agentcad/jobs.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <random>
using namespace agentcad;
namespace {
int checks=0;
void require(bool ok,const std::string& reason){++checks;if(!ok)throw std::runtime_error(reason);}
void near(double actual,double expected,double tolerance=1e-5){require(std::abs(actual-expected)<=tolerance,"Expected "+std::to_string(expected)+", received "+std::to_string(actual));}
Json plane(Json origin={0,0,0}){return {{"origin",origin},{"normal",{0,0,1}},{"x_direction",{1,0,0}}};}
Json sketch(const std::string& id,Json profile,Json origin={0,0,0}){return {{"id",id},{"type","sketch"},{"workplane",plane(origin)},{"profile",profile}};}
Json circle(double radius){return {{"type","circle"},{"radius",radius}};}
Json rectangle(double width,double height){return {{"type","rectangle"},{"width",width},{"height",height}};}
Json model(Json features,const std::string& output="part"){return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",features},{"output",output}};}
Json extrude(Json profile,Json options){options["id"]="part";options["type"]="extrude";options["input"]="profile";return model(Json::array({sketch("profile",profile),options}));}
void fails(const std::function<void()>& action,const std::string& feature){try{action();}catch(const Error& e){require(e.details.value("feature_id",std::string())==feature,"Failure retains feature identity: "+e.json().dump());return;}throw std::runtime_error("Expected failure for "+feature);}
void extrusion_tests(){
  auto tilted=extrude(rectangle(10,20),{{"distance",10},{"direction",{0.6,0,0.8}}});
  auto summary=BuiltModel(tilted).summary();near(summary.at("volume_mm3"),1600);near(summary.at("bounds_mm").at("max")[0],16);near(summary.at("bounds_mm").at("max")[2],8);
  auto both=extrude(rectangle(10,20),{{"distance",5},{"both",true}});summary=BuiltModel(both).summary();near(summary.at("volume_mm3"),2000);near(summary.at("bounds_mm").at("min")[2],-5);near(summary.at("bounds_mm").at("max")[2],5);require(summary.at("solid_count")==1,"bidirectional regions form one body");
  const double angle=5,delta=10*std::tan(angle*std::numbers::pi/180),r0=5,r1=r0-delta;
  auto tapered=extrude(circle(r0),{{"distance",10},{"taper_deg",angle}});
  near(BuiltModel(tapered).summary().at("volume_mm3"),std::numbers::pi*10/3*(r0*r0+r0*r1+r1*r1),1e-4);
  auto negative=extrude(rectangle(20,20),{{"distance",-10},{"taper_deg",-5}});summary=BuiltModel(negative).summary();near(summary.at("bounds_mm").at("min")[2],-10);near(summary.at("bounds_mm").at("max")[0],20+delta,1e-5);
  auto collapse=extrude(circle(1),{{"distance",10},{"taper_deg",45}});fails([&]{BuiltModel bad(collapse);},"part");
  auto parallel=tilted;parallel["features"].back()["direction"]={1,0,0};fails([&]{BuiltModel bad(parallel);},"part");
  auto invalid=both;invalid["features"].back()["both"]="yes";fails([&]{validate_model(invalid);},"part");
}
Json target_model(const std::string& until,Json target_origin={-2,-2,5},Json target_size={14,14,3}){
  return model(Json::array({sketch("profile",rectangle(10,10)),{{"id","target"},{"type","box"},{"size",target_size},{"origin",target_origin}},{{"id","part"},{"type","extrude"},{"input","profile"},{"target","target"},{"until",until}}}));
}
void target_tests(){
  auto first=target_model("first"),last=target_model("last");near(BuiltModel(first).summary().at("volume_mm3"),500);near(BuiltModel(last).summary().at("volume_mm3"),800);
  auto miss=target_model("first",{100,100,5});fails([&]{BuiltModel bad(miss);},"part");
  auto partial=target_model("first",{0,0,5},{5,10,3});fails([&]{BuiltModel bad(partial);},"part");
  partial["features"].back()["until"]="last";fails([&]{BuiltModel bad(partial);},"part");
  auto behind=target_model("first",{-2,-2,-5});fails([&]{BuiltModel bad(behind);},"part");
  auto conflict=first;conflict["features"].back()["distance"]=10;fails([&]{validate_model(conflict);},"part");
  auto rotated=target_model("first",{-10,-10,10},{40,40,5});
  rotated["features"].insert(rotated["features"].begin()+2,Json{{"id","sloping"},{"type","transform"},{"input","target"},{"rotation",{{"origin",{0,0,10}},{"axis",{0,1,0}},{"angle_deg",20}}}});
  rotated["features"].back()["target"]="sloping";
  near(BuiltModel(rotated).summary().at("volume_mm3"),100*(10-5*std::tan(20*std::numbers::pi/180)),1e-4);
  auto before=feature_cache_keys(first);first["features"][1]["origin"][2]=7;auto after=feature_cache_keys(first);require(before.at("part")!=after.at("part"),"termination target edits invalidate dependent cache");
}
Json wire(Json start,Json end){return {{"type","wire"},{"segments",Json::array({{{"type","line"},{"start",start},{"end",end}}})}};}
Json swept(Json options){options["id"]="part";options["type"]="sweep";options["input"]="profile";options["path"]=wire({0,0,0},{0,0,10});return model(Json::array({sketch("profile",circle(2)),options}));}
Json hollow_profile(double size,double radius){
  const double h=size/2;Json edges=Json::array();const std::vector<Json> points={{-h,-h},{h,-h},{h,h},{-h,h}};
  for(int i=0;i<4;++i)edges.push_back({{"type","line"},{"start",points[i]},{"end",points[(i+1)%4]}});
  return {{"type","wire"},{"segments",edges},{"holes",Json::array({Json::array({{{"type","arc"},{"start",{radius,0}},{"mid",{0,radius}},{"end",{-radius,0}}},{{"type","arc"},{"start",{-radius,0}},{"mid",{0,-radius}},{"end",{radius,0}}}})})}};
}
void sweep_tests(){
  for(const auto& options:std::vector<Json>{Json{{"orientation","corrected_frenet"}},Json{{"orientation","frenet"}},Json{{"orientation","fixed"}},Json{{"binormal",{1,0,0}}},Json{{"guide",wire({3,0,0},{3,0,10})}}})
    near(BuiltModel(swept(options)).summary().at("volume_mm3"),40*std::numbers::pi,1e-4);
  auto variable=model(Json::array({sketch("a",circle(2)),sketch("b",circle(4),{0,0,10}),{{"id","part"},{"type","sweep"},{"sections",{"a","b"}},{"path",wire({0,0,0},{0,0,10})}}}));
  near(BuiltModel(variable).summary().at("volume_mm3"),std::numbers::pi*10/3*(4+8+16),1e-4);
  auto hollow=swept({{"orientation","frenet"}});hollow["features"][0]["profile"]=hollow_profile(10,1);
  near(BuiltModel(hollow).summary().at("volume_mm3"),10*(100-std::numbers::pi),1e-4);
  auto changing_hollow=variable;changing_hollow["features"][0]["profile"]=hollow_profile(10,1);changing_hollow["features"][1]["profile"]=hollow_profile(12,2);
  near(BuiltModel(changing_hollow).summary().at("volume_mm3"),10.0/3*(100+120+144)-std::numbers::pi*10/3*(1+2+4),1e-3);
  auto invalid=variable;invalid["features"].back()["input"]="a";fails([&]{validate_model(invalid);},"part");
  invalid=swept({{"orientation","fixed"},{"binormal",{1,0,0}}});fails([&]{validate_model(invalid);},"part");
  invalid=variable;invalid["features"][1]["workplane"]["origin"]={1,0,10};fails([&]{BuiltModel bad(invalid);},"part");
  invalid=variable;invalid["features"][1]["workplane"]["origin"]={0,0,8};fails([&]{BuiltModel bad(invalid);},"part");
  invalid=changing_hollow;invalid["features"][1]["profile"]=circle(4);fails([&]{BuiltModel bad(invalid);},"part");
  auto curved=swept({{"orientation","frenet"}});curved["features"].back()["path"]={{"type","wire"},{"segments",Json::array({{{"type","arc"},{"start",{0,0,0}},{"mid",{10-10/std::sqrt(2.0),0,10/std::sqrt(2.0)}},{"end",{10,0,10}}}})}};
  near(BuiltModel(curved).summary().at("volume_mm3"),20*std::numbers::pi*std::numbers::pi,1e-3);
  auto guided=curved;guided["features"].back().erase("orientation");
  guided["features"].back()["guide"]={{"type","wire"},{"segments",Json::array({{{"type","arc"},{"start",{-3,0,0}},{"mid",{10-13/std::sqrt(2.0),0,13/std::sqrt(2.0)}},{"end",{10,0,13}}}})}};
  near(BuiltModel(guided).summary().at("volume_mm3"),20*std::numbers::pi*std::numbers::pi,1e-3);
}
void lifecycle_tests(){
  const auto root=std::filesystem::temp_directory_path()/("agentcad-richer-"+std::to_string(std::random_device{}()));
  struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove_all(p,ec);}}cleanup{root};
  set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));Service service(root);auto intent=target_model("first");intent["parameters"]["stop"]=5;intent["features"][1]["origin"][2]={{"parameter","stop"}};
  auto created=service.call("cad_create",{{"document_id","termination"},{"model",intent}});require(created.at("revision")==1,"create commits first revision");
  service.call("cad_apply",{{"document_id","termination"},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","stop"},{"value",7}}})}});
  const auto read=service.call("cad_read",{{"document_id","termination"}});require(read.at("revision")==2,"target parameter edit commits");
  const auto failed=[&]{service.call("cad_apply",{{"document_id","termination"},{"expected_revision",2},{"operations",Json::array({{{"op","set_parameter"},{"name","stop"},{"value",-8}}})}});};
  try{failed();throw std::runtime_error("Failed target edit unexpectedly committed");}catch(const Error& e){require(e.details.at("feature_id")=="part","worker error names failing extrusion");}
  require(service.call("cad_read",{{"document_id","termination"}})==read,"failed target edit preserves complete saved record");
  Service reopened(root);auto query=reopened.call("cad_query",{{"document_id","termination"},{"revision",2}});near(query.at("summary").at("volume_mm3"),700);
  auto exported=reopened.call("cad_export",{{"document_id","termination"},{"revision",2},{"format","step"}});require(std::filesystem::file_size(exported.at("path").get<std::string>())>0,"cold reopen exports exact STEP");
}
}
int main(){try{configure_kernel_logging();extrusion_tests();target_tests();sweep_tests();lifecycle_tests();std::cout<<checks<<" richer modeling checks passed\n";return 0;}catch(const Error& e){std::cerr<<e.json().dump()<<"\n";return 1;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
