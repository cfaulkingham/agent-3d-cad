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
#include <TopExp_Explorer.hxx>
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
Json model(Json features,const std::string& output){return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",features},{"output",output}};}
Json bezier(const std::string& id,Json points){return {{"id",id},{"type","surface_bezier"},{"control_points",points}};}
Json rectangle(const std::string& id="patch"){return bezier(id,{{{0,0,0},{0,20,0}},{{10,0,0},{10,20,0}}});}
Json cylindrical(const std::string& id="patch"){
  auto feature=bezier(id,{{{10,0,0},{10,0,5}},{{10,10,0},{10,10,5}},{{0,10,0},{0,10,5}}});const auto weight=std::sqrt(.5);
  feature["weights"]={{1,1},{weight,weight},{1,1}};return feature;
}
Json bspline(Json feature){feature["type"]="surface_bspline";feature["degree_u"]=feature.at("control_points").size()-1;feature["degree_v"]=1;feature["knots_u"]={0,1};feature["knots_v"]={0,1};feature["multiplicities_u"]={feature.at("degree_u").get<int>()+1,feature.at("degree_u").get<int>()+1};feature["multiplicities_v"]={2,2};return feature;}
void step(const BuiltModel& built,const fs::path& path,double area,double volume=0){built.export_file(path,"step");STEPControl_Reader reader;require(reader.ReadFile(path_to_utf8(path).c_str())==IFSelect_RetDone,"Independent STEP reads");require(reader.TransferRoots()>0,"STEP transfers");require(BRepCheck_Analyzer(reader.OneShape()).IsValid(),"Independent STEP valid");GProp_GProps surface,body;BRepGProp::SurfaceProperties(reader.OneShape(),surface);BRepGProp::VolumeProperties(reader.OneShape(),body);near(surface.Mass(),area,5e-4);if(volume)near(body.Mass(),volume,5e-4);else {int solids=0;for(TopExp_Explorer it(reader.OneShape(),TopAbs_SOLID);it.More();it.Next())++solids;require(solids==0,"Surface STEP remains a surface, not an implied solid");}}
void patches(const fs::path& root){
  auto doc=model(Json::array({rectangle()}),"patch");BuiltModel planar(doc);near(planar.summary().at("area_mm2"),200);near(planar.summary().at("volume_mm3"),0);require(planar.summary().at("solid_count")==0,"Explicit patch is not solid material");require(planar.topology().at("faces")[0].at("surface_kind")=="bezier","Bezier surface remains native exact geometry");
  const auto mesh=planar.mesh();require(mesh.at("triangles").size()>=2,"Surface previews have complete triangles");step(planar,root/"patch.step",200);planar.export_file(root/"patch.stl","stl");require(fs::file_size(root/"patch.stl")>=184,"Open surface STL has triangles");fails("invalid_argument",[&]{planar.print_meshes();});near(BuiltModel(doc,planar.snapshot()).summary().at("area_mm2"),200);
  const auto drawing=planar.drawing({{"views",Json::array({{{"id","top"},{"orientation","top"}}})}});require(!drawing.at("views")[0].at("entities").empty(),"Surface drawing projects exact boundaries");
  auto splined=doc;splined["features"][0]=bspline(rectangle());BuiltModel b(splined);near(b.summary().at("area_mm2"),200);require(b.topology().at("faces")[0].at("surface_kind")=="bspline","B-spline surface remains native");
  auto interior=bspline(bezier("patch",{{{0,0,0},{0,20,0}},{{5,0,0},{5,20,0}},{{10,0,0},{10,20,0}}}));
  interior["degree_u"]=1;interior["knots_u"]={0,.25,1};interior["multiplicities_u"]={2,1,2};
  auto interior_doc=model(Json::array({interior,{{"id","trimmed"},{"type","surface_trim"},{"input","patch"},{"u_range",{0,.25}},{"v_range",{0,1}}}}),"trimmed");
  near(BuiltModel(interior_doc).summary().at("area_mm2"),100);near(BuiltModel(interior_doc).summary().at("bounds_mm").at("max")[0],5);
  auto invalid_grid=doc;invalid_grid["features"][0]["control_points"][0].push_back({0,0,0});fails("invalid_model",[&]{validate_model(invalid_grid);});
  doc["features"][0]=cylindrical();BuiltModel curved(doc);near(curved.summary().at("area_mm2"),25*std::numbers::pi,1e-4);step(curved,root/"curved.step",25*std::numbers::pi);near(BuiltModel(doc,curved.snapshot()).summary().at("area_mm2"),25*std::numbers::pi,1e-4);require(curved.mesh().at("triangles").size()>2,"Curved rational surface tessellates");
  doc["features"][0]=bspline(cylindrical());near(BuiltModel(doc).summary().at("area_mm2"),25*std::numbers::pi,1e-4);
  auto invalid=doc;invalid["features"][0]["weights"][1][0]=0;fails("invalid_model",[&]{validate_model(invalid);});
  invalid=doc;invalid["features"][0]["knots_u"]={0,0};fails("invalid_model",[&]{validate_model(invalid);});
  invalid=doc;invalid["features"][0]["multiplicities_u"]={2,2};fails("invalid_model",[&]{validate_model(invalid);});
  invalid=model(Json::array({bezier("patch",{{{0,0,0},{0,0,0}},{{0,0,0},{0,0,0}}})}),"patch");fails("invalid_shape",[&]{BuiltModel bad(invalid);});
  invalid=model(Json::array({rectangle(),{{"id","wrong"},{"type","mirror"},{"input","patch"},{"plane",{{"origin",{0,0,0}},{"normal",{0,0,1}},{"x_direction",{1,0,0}}}}}}),"wrong");fails("invalid_model",[&]{validate_model(invalid);});
  invalid=model(Json::array({rectangle(),{{"id","wrong"},{"type","assembly"},{"parts",Json::array({{{"id","sheet"},{"input","patch"}}})}}}),"wrong");fails("invalid_model",[&]{validate_model(invalid);});
}
void trim_and_thicken(const fs::path& root){
  auto doc=model(Json::array({rectangle(),{{"id","trimmed"},{"type","surface_trim"},{"input","patch"},{"u_range",{.25,.75}},{"v_range",{.25,.75}}}}),"trimmed");BuiltModel trimmed(doc);near(trimmed.summary().at("area_mm2"),50);near(trimmed.summary().at("bounds_mm").at("min")[0],2.5);near(trimmed.summary().at("bounds_mm").at("max")[1],15);step(trimmed,root/"trimmed.step",50);near(BuiltModel(doc,trimmed.snapshot()).summary().at("area_mm2"),50);
  auto bad=doc;bad["features"][1]["u_range"]={-.1,.75};fails("invalid_model",[&]{BuiltModel invalid(bad);});
  doc["features"].push_back({{"id","wall"},{"type","thicken"},{"input","trimmed"},{"thickness",2}});doc["output"]="wall";BuiltModel wall(doc);near(wall.summary().at("volume_mm3"),100);step(wall,root/"wall.step",160,100);
  near(wall.summary().at("bounds_mm").at("max")[2],2);near(wall.summary().at("bounds_mm").at("min")[2],0);
  doc["features"][2]["thickness"]=-2;BuiltModel reverse(doc);near(reverse.summary().at("volume_mm3"),100);near(reverse.summary().at("bounds_mm").at("min")[2],-2);near(reverse.summary().at("bounds_mm").at("max")[2],0);doc["features"][2]["thickness"]=2;
  doc["features"][0]=cylindrical();doc["features"][1]["u_range"]={0,1};doc["features"][1]["v_range"]={.2,.8};doc["output"]="trimmed";near(BuiltModel(doc).summary().at("area_mm2"),15*std::numbers::pi,1e-4);
  doc["features"].erase(1);doc["features"][1]["input"]="patch";doc["output"]="wall";BuiltModel tube(doc);near(tube.summary().at("volume_mm3"),55*std::numbers::pi,1e-3);
  doc["features"][1]["thickness"]=-2;near(BuiltModel(doc).summary().at("volume_mm3"),45*std::numbers::pi,1e-3);
  doc["features"][1]["thickness"]=2;doc["features"][1]["faces"]={{"type","geometric"},{"feature_id","patch"},{"surface_kind","bezier"},{"expected_count",1}};near(BuiltModel(doc).summary().at("volume_mm3"),55*std::numbers::pi,1e-3);
}
Json box_shell(){
  return model(Json::array({
    bezier("bottom",{{{0,0,0},{10,0,0}},{{0,20,0},{10,20,0}}}),
    bezier("top",{{{0,0,5},{0,20,5}},{{10,0,5},{10,20,5}}}),
    bezier("front",{{{0,0,0},{0,0,5}},{{10,0,0},{10,0,5}}}),
    bezier("back",{{{10,20,0},{10,20,5}},{{0,20,0},{0,20,5}}}),
    bezier("left",{{{0,20,0},{0,20,5}},{{0,0,0},{0,0,5}}}),
    bezier("right",{{{10,0,0},{10,0,5}},{{10,20,0},{10,20,5}}}),
    {{"id","shell"},{"type","surface_shell"},{"inputs",{"bottom","top","front","back","left","right"}},{"tolerance",1e-7},{"closed",true}}}),"shell");
}
void shells(const fs::path& root){
  auto doc=box_shell();BuiltModel shell(doc);near(shell.summary().at("area_mm2"),700);require(shell.summary().at("solid_count")==0,"Closed shell remains shell until explicit materialization");step(shell,root/"closed-shell.step",700);near(BuiltModel(doc,shell.snapshot()).summary().at("area_mm2"),700);require(shell.topology().at("provenance").at("dependencies").size()==6,"Shell records every input dependency");
  doc["features"].push_back({{"id","body"},{"type","surface_solid"},{"input","shell"}});doc["output"]="body";BuiltModel solid(doc);near(solid.summary().at("volume_mm3"),1000);step(solid,root/"solid.step",700,1000);require(solid.print_meshes().size()==1,"Explicit surface solid exports closed print mesh");
  auto open=box_shell();open["features"][6]["inputs"]={"bottom","front"};open["features"][6]["closed"]=false;BuiltModel connected(open);near(connected.summary().at("area_mm2"),250);step(connected,root/"open-shell.step",250);
  open["features"][6]["closed"]=true;fails("invalid_shape",[&]{BuiltModel invalid(open);});
  open["features"][6]["closed"]=false;open["features"][6]["inputs"]={"bottom","top"};fails("invalid_shape",[&]{BuiltModel invalid(open);});
  open["features"][6]["inputs"]={"bottom","front"};open["features"].push_back({{"id","body"},{"type","surface_solid"},{"input","shell"}});open["output"]="body";fails("invalid_model",[&]{BuiltModel invalid(open);});
  auto mismatch=box_shell();mismatch["features"][6]["closed"]=false;fails("invalid_shape",[&]{BuiltModel invalid(mismatch);});
  auto nonmanifold=box_shell();nonmanifold["features"].insert(nonmanifold["features"].begin()+6,bezier("third",{{{0,0,0},{0,-10,10}},{{10,0,0},{10,-10,10}}}));
  nonmanifold["features"][7]["inputs"]={"bottom","front","third"};nonmanifold["features"][7]["closed"]=false;fails("invalid_shape",[&]{BuiltModel invalid(nonmanifold);});
  auto duplicated=box_shell();duplicated["features"][6]["inputs"]={"bottom","bottom"};fails("invalid_model",[&]{validate_model(duplicated);});
  auto inward=doc;for(int i=0;i<6;++i)std::swap(inward["features"][i]["control_points"][0],inward["features"][i]["control_points"][1]);
  fails("invalid_shape",[&]{BuiltModel invalid(inward);});inward["features"][7]["reverse"]=true;near(BuiltModel(inward).summary().at("volume_mm3"),1000);
  const auto keys=feature_cache_keys(doc);auto changed=doc;changed["features"][0]["control_points"][0][0][0]=1;
  const auto changed_keys=feature_cache_keys(changed);require(keys.at("shell")!=changed_keys.at("shell")&&keys.at("body")!=changed_keys.at("body"),"Surface shell array dependencies invalidate downstream caches");
}
void lifecycle(const fs::path& root){
  set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));Service service(root/"service");auto doc=model(Json::array({rectangle()}),"patch");doc["parameters"]={{"width",10}};for(auto& point:doc["features"][0]["control_points"][1])point[0]={{"parameter","width"}};
  service.call("cad_create",{{"document_id","surface"},{"model",doc}});const Json edits=Json::array({{{"op","set_parameter"},{"name","width"},{"value",15}}});
  near(service.call("cad_preview",{{"document_id","surface"},{"expected_revision",1},{"operations",edits}}).at("summary").at("area_mm2"),300);require(service.call("cad_read",{{"document_id","surface"}}).at("revision")==1,"Surface preview preserves HEAD");
  service.call("cad_apply",{{"document_id","surface"},{"expected_revision",1},{"operations",edits}});
  auto failed=doc["features"][0];failed["control_points"]={{{0,0,0},{0,0,0}},{{0,0,0},{0,0,0}}};const auto error=fails("invalid_shape",[&]{service.call("cad_apply",{{"document_id","surface"},{"expected_revision",2},{"operations",Json::array({{{"op","replace_feature"},{"id","patch"},{"feature",failed}}})}});});require(error.details.at("feature_id")=="patch","Surface failure qualifies the feature");
  Service reopened(root/"service");require(reopened.call("cad_read",{{"document_id","surface"}}).at("revision")==2,"Failed surface geometry preserves revision after reopen");near(reopened.call("cad_query",{{"document_id","surface"},{"revision",2}}).at("summary").at("area_mm2"),300);reopened.call("cad_export",{{"document_id","surface"},{"revision",2},{"format","stl"}});
  const auto changed=apply_operations(doc,edits);require(feature_cache_keys(doc).at("patch")!=feature_cache_keys(changed).at("patch"),"Surface control parameters invalidate geometry cache");
  fails("invalid_argument",[&]{reopened.call("cad_export",{{"document_id","surface"},{"revision",2},{"format","3mf"}});});
  auto library=box_shell();library["features"].push_back({{"id","body"},{"type","surface_solid"},{"input","shell"}});library["output"]="body";
  reopened.call("cad_create",{{"document_id","surface-library"},{"model",library}});
  auto consumer=model(Json::array({{{"id","seed"},{"type","box"},{"size",{1,1,1}}}}),"seed");reopened.call("cad_create",{{"document_id","consumer"},{"model",consumer}});
  const auto captured=reopened.call("cad_apply",{{"document_id","consumer"},{"expected_revision",1},{"operations",Json::array({
    {{"op","set_component"},{"id","module"},{"source_document_id","surface-library"},{"source_revision",1}},{{"op","set_output"},{"feature_id","module"}}})}});
  near(captured.at("summary").at("volume_mm3"),1000);const auto captured_source=test::receipt_source(reopened,captured);const auto& component=captured_source.at("components")[0];require(component.at("feature_map").size()==8,"Surface solid components capture complete patch and shell dependencies");
  const auto imported_shell=component.at("feature_map").at("shell");bool remapped=false;for(const auto& feature:captured_source.at("features"))if(feature.at("id")==imported_shell){remapped=feature.at("inputs")[0]==component.at("feature_map").at("bottom");}require(remapped,"Surface shell dependency arrays are remapped during portable component capture");
  service.call("cad_create",{{"document_id","surface-only-library"},{"model",model(Json::array({rectangle()}),"patch")}});
  fails("invalid_model",[&]{service.call("cad_apply",{{"document_id","consumer"},{"expected_revision",2},{"operations",Json::array({{{"op","set_component"},{"id","sheet"},{"source_document_id","surface-only-library"},{"source_revision",1}}})}});});
}
}
int main(){try{configure_kernel_logging();Temp temp;patches(temp.path);trim_and_thicken(temp.path);shells(temp.path);lifecycle(temp.path);std::cout<<checks<<" exact surface checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
