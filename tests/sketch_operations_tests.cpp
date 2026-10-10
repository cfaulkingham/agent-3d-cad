#include "mutation_test_support.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/cache.hpp"
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
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
void near(double a,double b,double tolerance=1e-5){require(std::abs(a-b)<tolerance,"Expected "+std::to_string(b)+", got "+std::to_string(a));}
Error fails(const std::string& code,const std::function<void()>& action){try{action();}catch(const Error& e){require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what());return e;}throw std::runtime_error("Expected "+code);}
struct Temp {fs::path path=temporary_file(fs::temp_directory_path());Temp(){fs::remove(path);directory(path);}~Temp(){std::error_code ignored;fs::remove_all(path,ignored);}};
Json plane(Json origin={0,0,0},Json normal={0,0,1},Json x={1,0,0}){return {{"origin",origin},{"normal",normal},{"x_direction",x}};}
Json model(Json features,const std::string& output){return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",features},{"output",output}};}
Json rectangle(const std::string& id,double w=10,double h=20,Json workplane=plane()){return {{"id",id},{"type","sketch"},{"workplane",workplane},{"profile",{{"type","rectangle"},{"width",w},{"height",h}}}};}
Json circle(const std::string& id,double radius,Json workplane=plane()){return {{"id",id},{"type","sketch"},{"workplane",workplane},{"profile",{{"type","circle"},{"radius",radius}}}};}
Json extrude(const std::string& input,double distance=5){return {{"id","part"},{"type","extrude"},{"input",input},{"distance",distance}};}
Json vertex(const std::string& input,Json point,double tolerance=1e-6,int expected=1){return {{"type","geometric"},{"feature_id",input},{"point",point},{"tolerance",tolerance},{"expected_count",expected}};}
Json face(const std::string& input,Json normal={0,0,1},int expected=1){return {{"type","geometric"},{"feature_id",input},{"surface_kind","plane"},{"expected_count",expected},{"normal",{{"vector",normal},{"tolerance",1e-6}}}};}
void readback(const BuiltModel& built,const fs::path& path,double expected){built.export_file(path,"step");STEPControl_Reader reader;require(reader.ReadFile(path_to_utf8(path).c_str())==IFSelect_RetDone,"Independent STEP reads");require(reader.TransferRoots()>0,"Independent STEP transfers");require(BRepCheck_Analyzer(reader.OneShape()).IsValid(),"Independent STEP valid");GProp_GProps props;BRepGProp::VolumeProperties(reader.OneShape(),props);near(props.Mass(),expected,1e-4);}
bool has_curve(const BuiltModel& built,const std::string& feature,const std::string& kind){const auto topology=built.topology(feature);for(const auto& edge:topology.at("edges"))if(edge.at("curve_kind")==kind)return true;return false;}
void booleans(const fs::path& root){
  auto doc=model(Json::array({rectangle("a"),rectangle("b",10,20,plane({5,0,0})),{{"id","combined"},{"type","sketch_fuse"},{"left","a"},{"right","b"}},extrude("combined")}),"part");
  BuiltModel unioned(doc);near(unioned.summary().at("volume_mm3"),1500);near(unioned.summary("a").at("area_mm2"),200);near(unioned.summary("combined").at("area_mm2"),300);
  require(unioned.topology("combined").at("provenance").at("dependencies")==Json({"a","b"}),"Boolean dependencies are explicit");
  readback(unioned,root/"union.step",1500);near(BuiltModel(doc,unioned.snapshot()).summary().at("volume_mm3"),1500);
  doc["features"][2]["type"]="sketch_intersection";near(BuiltModel(doc).summary().at("volume_mm3"),500);
  doc["features"][2]["type"]="sketch_cut";near(BuiltModel(doc).summary().at("volume_mm3"),500);
  doc["features"][1]["workplane"]=plane({0,0,1});fails("invalid_model",[&]{BuiltModel bad(doc);});
  doc["features"][1]["workplane"]=plane({40,0,0});doc["features"][2]["type"]="sketch_intersection";fails("invalid_shape",[&]{BuiltModel bad(doc);});
  doc["features"][2]["type"]="sketch_fuse";BuiltModel disjoint(doc);near(disjoint.summary().at("volume_mm3"),2000);require(disjoint.summary().at("solid_count")==2,"Disjoint sketch regions extrude separately");
  near(BuiltModel(doc,disjoint.snapshot()).summary().at("volume_mm3"),2000);
  // Opposite normals are coplanar; extrusion follows the left sketch normal.
  doc["features"][1]["workplane"]=plane({5,20,0},{0,0,-1});near(BuiltModel(doc).summary().at("volume_mm3"),1500);
  auto ring=model(Json::array({circle("outer",10),circle("inner",3),{{"id","ring"},{"type","sketch_cut"},{"left","outer"},{"right","inner"}},extrude("ring")}),"part");
  BuiltModel bored(ring);near(bored.summary().at("volume_mm3"),455*std::numbers::pi);require(has_curve(bored,"ring","circle"),"Boolean preserves exact circles");readback(bored,root/"ring.step",455*std::numbers::pi);
  auto walls=ring;walls["features"].erase(3);
  walls["features"].push_back({{"id","second"},{"type","sketch_instance"},{"input","ring"},{"translation",{30,0,0}}});
  walls["features"].push_back({{"id","regions"},{"type","sketch_fuse"},{"left","ring"},{"right","second"}});
  walls["features"].push_back({{"id","part"},{"type","thicken"},{"input","regions"},{"thickness",2}});
  BuiltModel multiple_walls(walls);near(multiple_walls.summary().at("volume_mm3"),364*std::numbers::pi);require(multiple_walls.summary().at("solid_count")==2,"Derived disjoint sketch regions thicken independently with holes retained");
  readback(multiple_walls,root/"thick-regions.step",364*std::numbers::pi);
  // Compound sketches are forbidden as final model outputs and solid operands.
  auto bad=ring;bad["output"]="ring";fails("invalid_model",[&]{validate_model(bad);});
  bad=ring;bad["features"][3]={{"id","part"},{"type","mirror"},{"input","ring"},{"plane",plane()}};fails("invalid_model",[&]{validate_model(bad);});
}
void offsets_and_corners(const fs::path& root){
  auto doc=model(Json::array({rectangle("base"),{{"id","offset"},{"type","sketch_offset"},{"input","base"},{"distance",1}},extrude("offset")}),"part");
  BuiltModel outset(doc);near(outset.summary().at("volume_mm3"),5*(260+std::numbers::pi));require(has_curve(outset,"offset","circle"),"Arc join stays circular");near(BuiltModel(doc,outset.snapshot()).summary().at("volume_mm3"),outset.summary().at("volume_mm3"));
  doc["features"][1]["join"]="intersection";near(BuiltModel(doc).summary().at("volume_mm3"),1320);
  doc["features"][1]["distance"]=-1;near(BuiltModel(doc).summary().at("volume_mm3"),720);
  auto ring=model(Json::array({circle("outer",10),circle("inner",3),{{"id","ring"},{"type","sketch_cut"},{"left","outer"},{"right","inner"}},{{"id","offset"},{"type","sketch_offset"},{"input","ring"},{"distance",1}},extrude("offset")}),"part");
  BuiltModel thick(ring);near(thick.summary().at("volume_mm3"),585*std::numbers::pi);readback(thick,root/"offset-ring.step",585*std::numbers::pi);
  ring["features"][3]["distance"]=-1;near(BuiltModel(ring).summary().at("volume_mm3"),325*std::numbers::pi);
  auto collapsed=model(Json::array({circle("base",1),{{"id","offset"},{"type","sketch_offset"},{"input","base"},{"distance",-10}},extrude("offset")}),"part");
  fails("invalid_shape",[&]{BuiltModel bad(collapsed);});
  doc=model(Json::array({rectangle("base"),{{"id","rounded"},{"type","sketch_fillet"},{"input","base"},{"radius",2},{"vertices","all"}},extrude("rounded")}),"part");
  BuiltModel rounded(doc);near(rounded.summary().at("volume_mm3"),5*(184+4*std::numbers::pi));near(rounded.summary("base").at("area_mm2"),200);require(has_curve(rounded,"rounded","circle"),"2D fillet adds exact arcs");readback(rounded,root/"rounded.step",5*(184+4*std::numbers::pi));
  doc["features"][1]={{"id","rounded"},{"type","sketch_chamfer"},{"input","base"},{"distance",2},{"vertices","all"}};near(BuiltModel(doc).summary().at("volume_mm3"),960);
  doc["features"][1]["vertices"]=vertex("base",{0,0,0});near(BuiltModel(doc).summary().at("volume_mm3"),990);
  doc["features"][1]["vertices"]=vertex("base",{100,0,0});const auto missing=fails("selection_missing",[&]{BuiltModel bad(doc);});require(missing.details.at("feature_id")=="rounded","Vertex failure qualifies feature");
  doc["features"][1]["vertices"]=vertex("base",{5,10,0},30);fails("selection_ambiguous",[&]{BuiltModel bad(doc);});
  doc["features"][1]["vertices"]=vertex("base",{0,0,0});doc["features"][1]["distance"]=100;fails("invalid_shape",[&]{BuiltModel bad(doc);});
}
void transforms_and_solids(const fs::path& root){
  auto doc=model(Json::array({rectangle("base",10,20,plane({2,3,4})),{{"id","copy"},{"type","sketch_mirror"},{"input","base"},{"plane",plane({0,0,0},{1,0,0},{0,1,0})}},extrude("copy")}),"part");
  BuiltModel mirrored(doc);near(mirrored.summary().at("volume_mm3"),1000);near(mirrored.summary().at("bounds_mm").at("min")[0],-12);near(mirrored.summary().at("bounds_mm").at("max")[2],9);near(BuiltModel(doc,mirrored.snapshot()).summary().at("bounds_mm").at("max")[2],9);
  doc["features"][1]={{"id","copy"},{"type","sketch_transform"},{"input","base"},{"translation",{0,0,8}},{"rotation",{{"origin",{0,0,0}},{"axis",{1,0,0}},{"angle_deg",90}}}};
  BuiltModel rotated(doc);near(rotated.summary().at("volume_mm3"),1000);near(rotated.summary().at("bounds_mm").at("min")[1],-9);near(rotated.summary().at("bounds_mm").at("max")[2],31);
  near(BuiltModel(doc,rotated.snapshot()).summary().at("bounds_mm").at("min")[1],-9);
  auto solids=model(Json::array({{{"id","base"},{"type","box"},{"size",{10,20,10}},{"origin",{2,3,4}}},{{"id","copy"},{"type","mirror"},{"input","base"},{"plane",plane({0,0,0},{1,0,0},{0,1,0})}}}),"copy");
  BuiltModel body(solids);near(body.summary().at("volume_mm3"),2000);near(body.summary().at("bounds_mm").at("min")[0],-12);readback(body,root/"mirror.step",2000);
  solids["features"][1]={{"id","copy"},{"type","split"},{"input","base"},{"plane",plane({5,0,0},{1,0,0},{0,1,0})},{"keep","both"}};
  BuiltModel split(solids);near(split.summary().at("volume_mm3"),2000);require(split.summary().at("solid_count")==2,"Split keeps both exact solids");readback(split,root/"split.step",2000);near(BuiltModel(solids,split.snapshot()).summary().at("volume_mm3"),2000);
  solids["features"][1]["keep"]="top";near(BuiltModel(solids).summary().at("volume_mm3"),1400);
  solids["features"][1]["keep"]="bottom";near(BuiltModel(solids).summary().at("volume_mm3"),600);
  solids["features"][1]["plane"]["normal"]={-1,0,0};near(BuiltModel(solids).summary().at("volume_mm3"),1400);
  solids["features"][1]["plane"]["origin"]={100,0,0};fails("invalid_shape",[&]{BuiltModel bad(solids);});
  solids["features"][1]={{"id","tool"},{"type","box"},{"size",{10,20,10}},{"origin",{7,3,4}}};solids["features"].push_back({{"id","common"},{"type","intersection"},{"left","base"},{"right","tool"}});solids["output"]="common";
  BuiltModel common(solids);near(common.summary().at("volume_mm3"),1000);near(common.summary("base").at("volume_mm3"),2000);readback(common,root/"common.step",1000);
  solids["features"][1]["origin"]={100,0,0};fails("invalid_shape",[&]{BuiltModel bad(solids);});
}
void derived_projection(const fs::path& root){
  auto doc=model(Json::array({{{"id","body"},{"type","box"},{"size",{10,20,10}}},{{"id","profile"},{"type","sketch_face"},{"input","body"},{"faces",face("body")}},extrude("profile")}),"part");
  BuiltModel reuse(doc);near(reuse.summary().at("volume_mm3"),1000);near(reuse.summary().at("bounds_mm").at("min")[2],10);near(BuiltModel(doc,reuse.snapshot()).summary().at("volume_mm3"),1000);
  doc["features"][1]={{"id","profile"},{"type","sketch_projection"},{"input","body"},{"faces",face("body")},{"workplane",plane()}};
  BuiltModel flat(doc);near(flat.summary().at("volume_mm3"),1000);near(flat.summary().at("bounds_mm").at("min")[2],0);
  const double h=std::sqrt(.5);doc["features"][1]["workplane"]=plane({0,0,0},{0,h,h});
  BuiltModel tilted(doc);near(tilted.summary().at("volume_mm3"),1000*h);readback(tilted,root/"projected.step",1000*h);near(BuiltModel(doc,tilted.snapshot()).summary().at("volume_mm3"),1000*h);
  doc["features"][0]={{"id","body"},{"type","cylinder"},{"radius",3},{"height",4}};BuiltModel ellipse(doc);near(ellipse.summary().at("volume_mm3"),45*std::numbers::pi*h,2e-4);require(has_curve(ellipse,"profile","ellipse")||has_curve(ellipse,"profile","bspline"),"Oblique circular projection retains an exact conic representation");
  doc["features"][1]["workplane"]=plane({0,0,0},{1,0,0},{0,1,0});fails("invalid_model",[&]{BuiltModel bad(doc);});
  doc["features"][1]["workplane"]=plane();doc["features"][1]["faces"].erase("normal");fails("selection_ambiguous",[&]{BuiltModel bad(doc);});
  doc["features"][1]["faces"]=face("body",{1,0,0});fails("selection_missing",[&]{BuiltModel bad(doc);});
  // Explicit top annulus projects with its bore preserved.
  doc=model(Json::array({{{"id","base"},{"type","cylinder"},{"radius",10},{"height",4}},{{"id","body"},{"type","hole"},{"input","base"},{"origin",{0,0,0}},{"axis",{0,0,1}},{"radius",3},{"depth",4}},{{"id","profile"},{"type","sketch_projection"},{"input","body"},{"faces",face("body")},{"workplane",plane()}},extrude("profile")}),"part");
  near(BuiltModel(doc).summary().at("volume_mm3"),455*std::numbers::pi);
}
void service(const fs::path& root){
  set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));Service service(root/"service");
  auto doc=model(Json::array({rectangle("base"),{{"id","expanded"},{"type","sketch_offset"},{"input","base"},{"distance",{{"parameter","offset"}}},{"join","intersection"}},extrude("expanded")}),"part");doc["parameters"]={{"offset",1}};
  service.call("cad_create",{{"document_id","sketch"},{"model",doc}});
  const Json edits=Json::array({{{"op","set_parameter"},{"name","offset"},{"value",2}}});
  near(service.call("cad_preview",{{"document_id","sketch"},{"expected_revision",1},{"operations",edits}}).at("summary").at("volume_mm3"),1680);
  require(service.call("cad_read",{{"document_id","sketch"}}).at("revision")==1,"Preview preserves HEAD");
  service.call("cad_apply",{{"document_id","sketch"},{"expected_revision",1},{"operations",edits}});
  auto invalid=doc["features"][1];invalid["type"]="sketch_cut";invalid.erase("distance");invalid.erase("join");invalid.erase("input");invalid["left"]="base";invalid["right"]="base";
  const auto error=fails("invalid_shape",[&]{service.call("cad_apply",{{"document_id","sketch"},{"expected_revision",2},{"operations",Json::array({{{"op","replace_feature"},{"id","expanded"},{"feature",invalid}}})}});});require(error.details.at("feature_id")=="expanded","Failed geometry names feature");
  Service reopened(root/"service");require(reopened.call("cad_read",{{"document_id","sketch"}}).at("revision")==2,"Failed operation preserves revision after reopen");
  near(reopened.call("cad_query",{{"document_id","sketch"},{"revision",2}}).at("summary").at("volume_mm3"),1680);
  const auto exported=reopened.call("cad_export",{{"document_id","sketch"},{"revision",2},{"format","step"}});require(fs::is_regular_file(path_from_utf8(exported.at("path").get<std::string>())),"Reopened geometry exports");
  auto corner=model(Json::array({rectangle("base"),{{"id","corner"},{"type","sketch_chamfer"},{"input","base"},{"distance",2},{"vertices",vertex("base",{0,0,0})}},extrude("corner")}),"part");
  reopened.call("cad_create",{{"document_id","library"},{"model",corner}});
  reopened.call("cad_create",{{"document_id","consumer"},{"model",model(Json::array({{{"id","seed"},{"type","box"},{"size",{1,1,1}}}}),"seed")}});
  const auto captured=reopened.call("cad_apply",{{"document_id","consumer"},{"expected_revision",1},{"operations",Json::array({{{"op","set_component"},{"id","module"},{"source_document_id","library"},{"source_revision",1}},{{"op","set_output"},{"feature_id","module"}}})}});
  near(captured.at("summary").at("volume_mm3"),990);
  const auto captured_source=test::receipt_source(reopened,captured);
  const auto mapping=captured_source.at("components")[0].at("feature_map");
  for(const auto& feature:captured_source.at("features"))if(feature.at("id")==mapping.at("corner"))require(feature.at("vertices").at("feature_id")==mapping.at("base"),"Component corner selectors remap their source feature");
  auto changed=apply_operations(doc,edits);const auto old_keys=feature_cache_keys(doc),new_keys=feature_cache_keys(changed);require(old_keys.at("base")==new_keys.at("base"),"Unchanged sketch cache key stays stable");require(old_keys.at("expanded")!=new_keys.at("expanded")&&old_keys.at("part")!=new_keys.at("part"),"Dependent feature cache keys invalidate on parameter edit");
}
}
int main(){try{configure_kernel_logging();Temp temp;booleans(temp.path);offsets_and_corners(temp.path);transforms_and_solids(temp.path);derived_projection(temp.path);service(temp.path);std::cout<<checks<<" sketch and solid operation checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
