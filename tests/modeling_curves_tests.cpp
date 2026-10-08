#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include <STEPControl_Reader.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include <functional>
#include <iostream>
#include <numbers>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message) {++checks;if(!value)throw std::runtime_error(message);}
void near(double a,double b,double tolerance=1e-5) {require(std::abs(a-b)<tolerance,"Expected "+std::to_string(b)+", got "+std::to_string(a));}
Error fails(const std::string& code,const std::function<void()>& action) {
  try {action();}catch(const Error& e){require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what());return e;}
  throw std::runtime_error("Expected "+code);
}
struct Temp {
  fs::path path=temporary_file(fs::temp_directory_path());
  Temp(){fs::remove(path);directory(path);}~Temp(){std::error_code ignored;fs::remove_all(path,ignored);}
};
Json line(Json a,Json b){return {{"type","line"},{"start",a},{"end",b}};}
Json arc(Json a,Json m,Json b){return {{"type","arc"},{"start",a},{"mid",m},{"end",b}};}
Json plane(Json origin={0,0,0},Json normal={0,0,1},Json x={1,0,0}) {return {{"origin",origin},{"normal",normal},{"x_direction",x}};}
Json wire(Json segments){return {{"type","wire"},{"segments",segments}};}
Json model(Json features,const std::string& output){return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",features},{"output",output}};}
Json profile_model(Json profile,Json workplane=plane()) {
  return model(Json::array({{{"id","profile"},{"type","sketch"},{"workplane",workplane},{"profile",profile}},
    {{"id","part"},{"type","extrude"},{"input","profile"},{"distance",5}}}),"part");
}
Json square(double x,double y,double size) {
  return Json::array({line({x,y},{x+size,y}),line({x+size,y},{x+size,y+size}),line({x+size,y+size},{x,y+size}),line({x,y+size},{x,y})});
}
bool curve_kind(const BuiltModel& built,const std::string& kind,const std::string& feature="") {
  const auto topology=built.topology(feature);
  for(const auto& e:topology.at("edges"))if(e.at("curve_kind")==kind)return true;
  return false;
}
void readback(const BuiltModel& built,const fs::path& path,double expected) {
  built.export_file(path,"step");STEPControl_Reader reader;const auto filename=path_to_utf8(path);
  require(reader.ReadFile(filename.c_str())==IFSelect_RetDone,"STEP reads independently");
  require(reader.TransferRoots()>0,"STEP roots transfer");const auto shape=reader.OneShape();
  require(BRepCheck_Analyzer(shape).IsValid(),"Independent STEP is valid");
  GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);near(properties.Mass(),expected,2e-4);
}
void chamfers(const fs::path& root) {
  const Json selector={{"type","geometric"},{"feature_id","base"},{"curve_kind","line"},{"expected_count",1},
    {"direction",{{"vector",{0,0,1}},{"tolerance",1e-6}}},{"center",{{"point",{0,0,5}},{"tolerance",1e-6}}}};
  auto doc=model(Json::array({{{"id","base"},{"type","box"},{"size",{10,20,10}}},
    {{"id","bevel"},{"type","chamfer"},{"input","base"},{"distance",2},{"edges",selector}}}),"bevel");
  BuiltModel built(doc);near(built.summary().at("volume_mm3"),1980);near(built.summary("base").at("volume_mm3"),2000);
  require(built.summary().at("face_count")==7,"Selective chamfer adds one plane");
  readback(built,root/"bevel.step",1980);
  auto restored=BuiltModel(doc,built.snapshot());near(restored.summary().at("volume_mm3"),1980);
  auto bad=doc;bad["features"][1]["edges"].erase("center");
  const auto e=fails("selection_ambiguous",[&]{BuiltModel rejected(bad);});
  require(e.details.at("feature_id")=="bevel" && e.details.at("actual_count")==4,"Chamfer cardinality failure names its feature");
  bad=doc;bad["features"][1]["edges"]["center"]["point"]={100,100,100};fails("selection_missing",[&]{BuiltModel rejected(bad);});
  bad=doc;bad["features"][1]["distance"]=100;fails("kernel_failure",[&]{BuiltModel rejected(bad);});
  doc["features"][1]["edges"]="all";doc["features"][1]["distance"]=0.5;
  require(BuiltModel(doc).summary().at("volume_mm3").get<double>()<2000,"All-edge chamfer removes material");
}
void profiles(const fs::path& root) {
  auto semi=profile_model(wire(Json::array({arc({-10,0},{0,10},{10,0}),line({10,0},{-10,0})})));
  BuiltModel semicircle(semi);near(semicircle.summary().at("volume_mm3"),250*std::numbers::pi);
  require(curve_kind(semicircle,"circle","profile"),"Circular profile remains an exact arc");
  readback(semicircle,root/"semicircle.step",250*std::numbers::pi);
  auto rotated=semi;rotated["features"][0]["workplane"]=plane({3,4,5},{0,1,0},{1,0,0});
  BuiltModel turned(rotated);near(turned.summary().at("volume_mm3"),250*std::numbers::pi);
  near(turned.summary().at("bounds_mm").at("min")[1],4);near(turned.summary().at("bounds_mm").at("max")[1],9);
  auto bezier=profile_model(wire(Json::array({{{"type","bezier"},{"points",{{-10,0},{-10,10},{10,10},{10,0}}}},line({10,0},{-10,0})})));
  BuiltModel curved(bezier);near(curved.summary().at("volume_mm3"),600);
  require(curve_kind(curved,"bezier","profile"),"Bezier poles are not replaced by a polyline");
  readback(curved,root/"bezier.step",600);
  auto spline=profile_model(wire(Json::array({{{"type","spline"},{"points",{{-10,0},{-5,7},{0,10},{5,7},{10,0}}}},line({10,0},{-10,0})})));
  BuiltModel smooth(spline);require(curve_kind(smooth,"bspline","profile"),"Interpolated profile has a native spline");
  require(smooth.summary().at("volume_mm3").get<double>()>500,"Spline profile encloses material");
  near(BuiltModel(spline,smooth.snapshot()).summary().at("volume_mm3"),smooth.summary().at("volume_mm3"));
  auto periodic=profile_model(wire(Json::array({{{"type","spline"},{"periodic",true},{"points",{{10,0},{0,10},{-10,0},{0,-10}}}}})));
  require(BuiltModel(periodic).summary().at("solid_count")==1,"Periodic spline closes without duplicating its first point");
  auto holed=profile_model(wire(square(0,0,20)));
  holed["features"][0]["profile"]["holes"]=Json::array({Json::array({arc({8,10},{10,12},{12,10}),arc({12,10},{10,8},{8,10})})});
  BuiltModel ring(holed);near(ring.summary().at("volume_mm3"),2000-20*std::numbers::pi);
  readback(ring,root/"interior.step",2000-20*std::numbers::pi);
  holed["features"][0]["profile"]["holes"].push_back(square(2,2,2));
  near(BuiltModel(holed).summary().at("volume_mm3"),1980-20*std::numbers::pi);
  auto bad=holed;bad["features"][0]["profile"]["holes"].push_back(square(19,19,2));
  auto e=fails("invalid_shape",[&]{BuiltModel rejected(bad);});require(e.details.at("hole_index")==2,"Outside hole is located");
  bad=holed;bad["features"][0]["profile"]["holes"].push_back(square(1,1,4));fails("invalid_shape",[&]{BuiltModel rejected(bad);});
  bad=holed;bad["features"][0]["profile"]["holes"].push_back(square(0,5,2));fails("invalid_shape",[&]{BuiltModel rejected(bad);});
  bad=semi;bad["features"][0]["profile"]["segments"][1]["start"]={11,0};
  e=fails("invalid_shape",[&]{BuiltModel rejected(bad);});require(e.details.at("segment_index")==1 && e.details.at("feature_id")=="profile","Disconnected curve is located");
  bad=semi;bad["features"][0]["profile"]["segments"][0]["mid"]={0,0};fails("invalid_shape",[&]{BuiltModel rejected(bad);});
  bad=spline;bad["features"][0]["profile"]["segments"][0]["points"][2]={-5,7};fails("invalid_shape",[&]{BuiltModel rejected(bad);});
  bad=bezier;bad["features"][0]["profile"]["segments"][0]["points"][1]={-10,0};fails("invalid_shape",[&]{BuiltModel rejected(bad);});
  bad=profile_model(wire(Json::array({line({0,0},{10,10}),line({10,10},{0,10}),line({0,10},{10,0}),line({10,0},{0,0})})));
  fails("invalid_shape",[&]{BuiltModel rejected(bad);});
  // Curved profiles also feed revolution and loft, not only extrusion.
  auto revolved=semi;revolved["features"][1]={{"id","part"},{"type","revolve"},{"input","profile"},
    {"axis",{{"origin",{0,0,0}},{"direction",{1,0,0}}}},{"angle_deg",360}};
  near(BuiltModel(revolved).summary().at("volume_mm3"),4*std::numbers::pi*1000/3);
  auto loft=semi;loft["features"].erase(1);auto second=loft["features"][0];second["id"]="top";second["workplane"]=plane({0,0,10});
  loft["features"].push_back(second);loft["features"].push_back({{"id","part"},{"type","loft"},{"sections",{"profile","top"}}});
  near(BuiltModel(loft).summary().at("volume_mm3"),500*std::numbers::pi);
  loft["features"][0]["profile"]=holed["features"][0]["profile"];
  fails("invalid_model",[&]{BuiltModel rejected(loft);}); // Never silently discard interior wires.
}
void sweeps(const fs::path& root) {
  auto doc=profile_model({{"type","circle"},{"radius",1}});
  doc["features"][1]={{"id","part"},{"type","sweep"},{"input","profile"},
    {"path",wire(Json::array({arc({0,0,0},{10-10/std::sqrt(2.0),0,10/std::sqrt(2.0)},{10,0,10})}))}};
  BuiltModel elbow(doc);near(elbow.summary().at("volume_mm3"),5*std::numbers::pi*std::numbers::pi,1e-4);
  require(elbow.summary().at("solid_count")==1,"Curved sweep makes one connected solid");
  readback(elbow,root/"elbow.step",5*std::numbers::pi*std::numbers::pi);
  auto bad=doc;bad["features"][0]["workplane"]=plane({1,0,0});fails("invalid_model",[&]{BuiltModel rejected(bad);});
  bad=doc;bad["features"][0]["workplane"]=plane({0,0,0},{1,0,0},{0,1,0});fails("invalid_model",[&]{BuiltModel rejected(bad);});
  doc["features"][1]["path"]=wire(Json::array({{{"type","bezier"},{"points",{{0,0,0},{0,0,5},{5,0,10},{10,0,10}}}}}));
  require(BuiltModel(doc).summary().at("solid_count")==1,"Bezier path produces a solid");
  doc["features"][1]["path"]=wire(Json::array({{{"type","spline"},{"points",{{0,0,0},{2,0,6},{10,0,10}}},
    {"start_tangent",{0,0,1}},{"end_tangent",{1,0,0}}}}));
  BuiltModel smooth(doc);require(smooth.summary().at("solid_count")==1,"Spline endpoint tangents orient the sweep");
  near(BuiltModel(doc,smooth.snapshot()).summary().at("volume_mm3"),smooth.summary().at("volume_mm3"));
}
void patterns() {
  auto doc=model(Json::array({{{"id","pin"},{"type","cylinder"},{"origin",{10,0,0}},{"radius",1},{"height",2}},
    {{"id","ring"},{"type","circular_pattern"},{"input","pin"},{"count",4},
      {"axis",{{"origin",{0,0,0}},{"direction",{0,0,1}}}},{"angle_deg",90}}}),"ring");
  BuiltModel built(doc);near(built.summary().at("volume_mm3"),8*std::numbers::pi);
  require(built.summary().at("solid_count")==4,"Circular pattern preserves instances");
  near(built.summary().at("bounds_mm").at("min")[0],-11);near(built.summary().at("bounds_mm").at("max")[1],11);
  doc["features"][1]["angle_deg"]=-90;near(BuiltModel(doc).summary().at("volume_mm3"),8*std::numbers::pi);
  doc["features"][1]["count"]=5;fails("invalid_model",[&]{BuiltModel rejected(doc);});
}
void service(const fs::path& root) {
  set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));Service service(root/"service");
  auto doc=profile_model(wire(Json::array({{{"type","bezier"},{"points",{{-10,0},{-10,10},{10,10},{10,0}}}},line({10,0},{-10,0})})));
  doc["parameters"]={{"height",5},{"crown",10}};doc["features"][1]["distance"]={{"parameter","height"}};
  for (int i:{1,2}) doc["features"][0]["profile"]["segments"][0]["points"][i][1]={{"parameter","crown"}};
  service.call("cad_create",{{"document_id","curve"},{"model",doc}});
  const Json edits=Json::array({{{"op","set_parameter"},{"name","height"},{"value",10}},{{"op","set_parameter"},{"name","crown"},{"value",20}}});
  near(service.call("cad_preview",{{"document_id","curve"},{"expected_revision",1},{"operations",edits}}).at("summary").at("volume_mm3"),2400);
  require(service.call("cad_read",{{"document_id","curve"}}).at("revision")==1,"Curve preview preserves HEAD");
  service.call("cad_apply",{{"document_id","curve"},{"expected_revision",1},{"operations",edits}});
  auto bad=doc["features"][0];bad["profile"]["segments"][1]["end"]={0,0};
  const auto e=fails("invalid_shape",[&]{service.call("cad_apply",{{"document_id","curve"},{"expected_revision",2},
    {"operations",Json::array({{{"op","replace_feature"},{"id","profile"},{"feature",bad}}})}});});
  require(e.details.at("feature_id")=="profile","Worker failure carries curve feature identity");
  Service reopened(root/"service");require(reopened.call("cad_read",{{"document_id","curve"}}).at("revision")==2,"Failed curve edit preserves committed revision");
  require(reopened.call("cad_read",{{"document_id","curve"},{"revision",1}}).at("model")==doc,"Old curve source survives reopen");
  auto exported=reopened.call("cad_export",{{"document_id","curve"},{"revision",2},{"format","step"}});
  near(reopened.call("cad_import",{{"document_id","copy"},{"path",exported.at("path")}}).at("summary").at("volume_mm3"),2400,2e-4);
}
}
int main(){try{configure_kernel_logging();Temp temp;chamfers(temp.path);profiles(temp.path);sweeps(temp.path);patterns();service(temp.path);
  std::cout<<checks<<" modeling flexibility checks passed\n";return 0;
}catch(const Error& e){std::cerr<<e.code<<": "<<e.what()<<" "<<e.details.dump()<<"\n";return 1;}
catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
