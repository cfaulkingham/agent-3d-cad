#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include "agentcad/cache.hpp"
#include "agentcad/jobs.hpp"
#include <chrono>
#include <thread>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <gp_Elips.hxx>
#include <gp_Ax2.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
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
void near(double actual,double expected,double tolerance=1e-6){require(std::abs(actual-expected)<tolerance,"Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
void point(const Json& value,const std::array<double,3>& expected,double tolerance=1e-6){for(int i=0;i<3;++i)near(value[i],expected[i],tolerance);}
void fails(const std::string& code,const std::function<void()>& action){try{action();}catch(const Error& e){require(e.code==code,"Expected "+code+", received "+e.code+": "+e.what());return;}throw std::runtime_error("Expected "+code);}
struct Temp{fs::path path=temporary_file(fs::temp_directory_path());Temp(){fs::remove(path);directory(path);}~Temp(){std::error_code ignored;fs::remove_all(path,ignored);}};
Json frame(){return {{"origin",{0,0,0}},{"normal",{0,0,1}},{"x_direction",{1,0,0}}};}
Json model(Json features){return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",features},{"output",features.back().at("id")}};}
Json circle(std::string id,double x=0){auto plane=frame();plane["origin"]={x,0,0};return {{"id",id},{"type","sketch"},{"workplane",plane},{"profile",{{"type","circle"},{"radius",1}}}};}
Json curve(std::string id,Json segments){return {{"id",id},{"type","curve"},{"path",{{"type","wire"},{"segments",segments}}}};}
Json line(Json a,Json b){return {{"type","line"},{"start",a},{"end",b}};}
Json selector(std::string id,std::string kind){return {{"type","geometric"},{"feature_id",id},{"curve_kind",kind},{"expected_count",1}};}
Json constraint(std::string id,std::string kind){return {{"edge",selector(id,kind)}};}
Json solution(Json p,double tolerance=1e-6){return {{"point",p},{"tolerance",tolerance}};}
Json point_tangent(){return model(Json::array({circle("circle"),{{"id","part"},{"type","curve_constrained_line"},{"workplane",frame()},{"constraints",Json::array({constraint("circle","circle"),Json{{"point",{2,0,0}}}})},{"solution",solution({1.25,std::sqrt(3.)/4,0})}}}));}
Json tangent_arc(){return model(Json::array({curve("x",Json::array({line({-2,0,0},{2,0,0})})),curve("y",Json::array({line({0,-2,0},{0,2,0})})),{{"id","part"},{"type","curve_constrained_arc"},{"workplane",frame()},{"constraints",Json::array({constraint("x","line"),constraint("y","line")})},{"radius",.5},{"solution",solution({.5-.5/std::sqrt(2.),.5-.5/std::sqrt(2.),0})}}}));}
TopoDS_Shape read_step(const BuiltModel& built,const fs::path& path){built.export_file(path,"step");STEPControl_Reader reader;require(reader.ReadFile(path_to_utf8(path).c_str())==IFSelect_RetDone&&reader.TransferRoots()>0,"Independent STEP read succeeds");const auto result=reader.OneShape();require(BRepCheck_Analyzer(result).IsValid(),"Independent STEP B-rep is valid");return result;}
void on_shape(const TopoDS_Shape& shape,const gp_Pnt& point){BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(point).Vertex(),shape);require(distance.IsDone()&&distance.Value()<1e-6,"Independent STEP contains analytic point");}
void tangent_lines(const fs::path& root){
  for(double sign:{-1.,1.}){auto intent=point_tangent();intent["features"][1]["solution"]["point"][1]=sign*std::sqrt(3.)/4;BuiltModel built(intent);const auto result=built.curve_samples({{"stations",{0,.5,1}}});near(result.at("length_mm"),std::sqrt(3.));point(result.at("samples")[0].at("point_mm"),{.5,sign*std::sqrt(3.)/2,0});point(result.at("samples")[2].at("point_mm"),{2,0,0});const auto& tangent=result.at("samples")[0].at("tangent");near(.5*tangent[0].get<double>()+sign*std::sqrt(3.)/2*tangent[1].get<double>(),0);
    const auto step=read_step(built,root/(sign>0?"tangent-upper.step":"tangent-lower.step"));on_shape(step,gp_Pnt(.5,sign*std::sqrt(3.)/2,0));GProp_GProps length;BRepGProp::LinearProperties(step,length);near(length.Mass(),std::sqrt(3.));}
  // The rational circle is authored as a Bezier, exercising the general native solver.
  auto rational=point_tangent();rational["features"][0]=curve("circle",Json::array({Json{{"type","bezier"},{"points",{{2,0,0},{2,2,0},{0,2,0}}},{"weights",{1,std::sqrt(.5),1}}}}));rational["features"][1]["constraints"][0]["edge"]["curve_kind"]="bezier";rational["features"][1]["constraints"][1]["point"]={4,0,0};rational["features"][1]["solution"]["point"]={2.5,std::sqrt(3.)/2,0};BuiltModel rational_line(rational);const auto solved=rational_line.curve_samples({{"stations",{0,1}}});near(solved.at("length_mm"),2*std::sqrt(3.));point(solved.at("samples")[0].at("point_mm"),{1,std::sqrt(3.),0});on_shape(read_step(rational_line,root/"rational-tangent.step"),gp_Pnt(1,std::sqrt(3.),0));
  auto placed=point_tangent();const Json plane={{"origin",{3,4,5}},{"normal",{0,1,0}},{"x_direction",{1,0,0}}};placed["features"][0]["workplane"]=plane;placed["features"][1]["workplane"]=plane;placed["features"][1]["constraints"][1]["point"]={5,4,5};placed["features"][1]["solution"]["point"]={4.25,4,5-std::sqrt(3.)/4};point(BuiltModel(placed).curve_samples({{"stations",{0}}}).at("samples")[0].at("point_mm"),{3.5,4,5-std::sqrt(3.)/2});
  auto reversed=point_tangent();std::swap(reversed["features"][1]["constraints"][0],reversed["features"][1]["constraints"][1]);point(BuiltModel(reversed).curve_samples({{"stations",{0}}}).at("samples")[0].at("point_mm"),{2,0,0});
  auto common=model(Json::array({circle("a"),circle("b",3),{{"id","part"},{"type","curve_constrained_line"},{"workplane",frame()},{"constraints",Json::array({constraint("a","circle"),constraint("b","circle")})},{"solution",solution({1.5,1,0})}}}));near(BuiltModel(common).curve_samples(Json::object()).at("length_mm"),3);
  common["features"].back()["solution"]=solution({1.5,0,0});fails("selection_ambiguous",[&]{BuiltModel invalid(common);});
  common["features"].back()["constraints"][0]["qualifier"]="outside";near(BuiltModel(common).curve_samples(Json::object()).at("length_mm"),std::sqrt(5.));
  auto invalid=point_tangent();invalid["features"][1]["solution"]["tolerance"]=10;fails("selection_ambiguous",[&]{BuiltModel wrong(invalid);});
  invalid=point_tangent();invalid["features"][1]["constraints"][1]["point"]={0,0,0};fails("selection_missing",[&]{BuiltModel wrong(invalid);});
  invalid=point_tangent();invalid["features"][1]["constraints"][1]["point"]={2,0,1};fails("invalid_model",[&]{BuiltModel wrong(invalid);});
  invalid=point_tangent();invalid["features"][1]["constraints"][0]["edge"]["feature_id"]="part";fails("invalid_model",[&]{validate_model(invalid);});
}
void tangent_arcs(const fs::path& root){
  auto intent=tangent_arc();BuiltModel built(intent);const auto sample=built.curve_samples({{"stations",{0,.5,1}}});near(sample.at("length_mm"),std::numbers::pi/4);point(sample.at("samples")[0].at("point_mm"),{.5,0,0});point(sample.at("samples")[2].at("point_mm"),{0,.5,0});near(sample.at("samples")[1].at("curvature_per_mm"),2);
  const auto shape=read_step(built,root/"arc.step");GProp_GProps length;BRepGProp::LinearProperties(shape,length);near(length.Mass(),std::numbers::pi/4);on_shape(shape,gp_Pnt(.5-.5/std::sqrt(2.),.5-.5/std::sqrt(2.),0));
  intent["features"].back()["solution"]=solution({.5+.5/std::sqrt(2.),.5+.5/std::sqrt(2.),0});near(BuiltModel(intent).curve_samples(Json::object()).at("length_mm"),3*std::numbers::pi/4);
  auto mixed=tangent_arc();mixed["features"].erase(mixed["features"].begin()+1);mixed["features"].back()["radius"]=1;mixed["features"].back()["constraints"][1]=Json{{"point",{1,1,0}}};mixed["features"].back()["solution"]=solution({std::sqrt(.5),1-std::sqrt(.5),0});near(BuiltModel(mixed).curve_samples(Json::object()).at("length_mm"),std::numbers::pi/2);
  auto two=mixed;two["features"].erase(two["features"].begin());two["features"].back()["constraints"]=Json::array({Json{{"point",{0,0,0}}},Json{{"point",{1,0,0}}}});two["features"].back()["solution"]=solution({.5,std::sqrt(3.)/2-1,0});near(BuiltModel(two).curve_samples(Json::object()).at("length_mm"),std::numbers::pi/3);
  auto invalid=tangent_arc();invalid["features"][0]["path"]["segments"][0]["end"]={0,0,0};fails("selection_missing",[&]{BuiltModel wrong(invalid);});
  invalid=tangent_arc();invalid["features"].back()["radius"]=0;fails("invalid_model",[&]{validate_model(invalid);});
}
Json round_model(bool invert){auto edges=selector("profile","line");edges["center"]={{"point",{10,2,0}},{"tolerance",1e-6}};return model(Json::array({{{"id","profile"},{"type","sketch"},{"workplane",frame()},{"profile",{{"type","rectangle"},{"width",10},{"height",4}}}},{{"id","rounded"},{"type","sketch_full_round"},{"input","profile"},{"edges",edges},{"invert",invert}},{{"id","body"},{"type","extrude"},{"input","rounded"},{"distance",1}}}));}
void inverted_round(const fs::path& root){for(bool invert:{false,true}){auto intent=round_model(invert);BuiltModel built(intent);const auto expected=32+(invert?-2:2)*std::numbers::pi;near(built.summary().at("volume_mm3"),expected);GProp_GProps volume;BRepGProp::VolumeProperties(read_step(built,root/(invert?"concave.step":"convex.step")),volume);near(volume.Mass(),expected);near(BuiltModel(intent,built.snapshot()).summary().at("volume_mm3"),expected);}}
Json bezier(Json points,Json weights=Json()){Json result={{"type","bezier"},{"points",points}};if(!weights.is_null())result["weights"]=weights;return result;}
Json hull(Json sources,Json ids){sources.push_back({{"id","hull"},{"type","sketch_hull"},{"inputs",ids},{"workplane",frame()},{"contact_tolerance",1e-5}});sources.push_back({{"id","body"},{"type","extrude"},{"input","hull"},{"distance",1}});return model(sources);}
Json ellipse(std::string id,double x,double y){const double w=std::sqrt(.5);return curve(id,Json::array({bezier({{x+2,y,0},{x+2,y+1,0},{x,y+1,0}},{1,w,1}),bezier({{x,y+1,0},{x-2,y+1,0},{x-2,y,0}},{1,w,1}),bezier({{x-2,y,0},{x-2,y-1,0},{x,y-1,0}},{1,w,1}),bezier({{x,y-1,0},{x+2,y-1,0},{x+2,y,0}},{1,w,1})}));}
void general_hulls(const fs::path& root){
  std::vector<std::pair<Json,double>> fixtures;
  fixtures.push_back({hull(Json::array({curve("quarter",Json::array({bezier({{2,0,0},{2,2,0},{0,2,0}},{1,std::sqrt(.5),1})}))}),{"quarter"}),std::numbers::pi-2});
  fixtures.push_back({hull(Json::array({curve("ellipse",Json::array({bezier({{4,0,0},{4,2,0},{0,2,0}},{1,std::sqrt(.5),1})}))}),{"ellipse"}),2*std::numbers::pi-4});
  fixtures.push_back({hull(Json::array({curve("arch",Json::array({bezier({{0,0,0},{0,2,0},{2,2,0},{2,0,0}})}))}),{"arch"}),2.4});
  fixtures.push_back({hull(Json::array({curve("spline",Json::array({Json{{"type","spline"},{"points",{{-1,0,0},{0,1,0},{1,0,0}}}}}))}),{"spline"}),4./3});
  fixtures.push_back({hull(Json::array({ellipse("left",0,0),ellipse("right",6,3)}),{"left","right"}),2*std::numbers::pi+12*std::sqrt(2.)});
  fixtures.push_back({hull(Json::array({ellipse("ellipse",0,0)}),{"ellipse"}),2*std::numbers::pi});
  int index=0;for(const auto& [intent,expected]:fixtures){BuiltModel built(intent);near(built.summary().at("volume_mm3"),expected,2e-5);require(built.summary().at("solid_count")==1,"Curved hull produces one exact solid");const auto topo=built.topology("hull");bool retained=false;for(const auto& edge:topo.at("edges"))if(edge.at("curve_kind")=="bezier"||edge.at("curve_kind")=="bspline")retained=true;require(retained,"Hull retains curved native boundary, not a sampled polygon");GProp_GProps volume;BRepGProp::VolumeProperties(read_step(built,root/("hull"+std::to_string(index++)+".step")),volume);near(volume.Mass(),expected,2e-5);}
  // Independently derived supporting contacts of two identical translated ellipses.
  const auto combined=read_step(BuiltModel(fixtures[4].first),root/"ellipses-contact.step");const double root2=std::sqrt(2.);for(double sign:{-1.,1.}){on_shape(combined,gp_Pnt(sign*root2,-sign/root2,.5));on_shape(combined,gp_Pnt(6+sign*root2,3-sign/root2,.5));on_shape(combined,gp_Pnt(3+sign*root2,1.5-sign/root2,.5));}
  auto invalid=fixtures[0].first;invalid["features"][0]["path"]["segments"][0]["points"][1][2]=1;fails("invalid_model",[&]{BuiltModel wrong(invalid);});
  invalid=fixtures[0].first;invalid["features"][1]["contact_tolerance"]=0;fails("invalid_model",[&]{validate_model(invalid);});
}
void imported_ellipse(const fs::path& root){
  // Independent external STEP fixture carries a native ellipse, not a Bezier encoding.
  const auto edge=BRepBuilderAPI_MakeEdge(gp_Elips(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(0,0,1)),2,1)).Edge();
  const auto face=BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(edge).Wire()).Face();STEPControl_Writer writer;
  require(writer.Transfer(face,STEPControl_AsIs)==IFSelect_RetDone,"Independent ellipse fixture transfers");
  const auto path=root/"external-ellipse.step";require(writer.Write(path_to_utf8(path).c_str())==IFSelect_RetDone,"Independent ellipse fixture writes");
  Service service(root/"ellipse-import");const auto receipt=service.call("cad_import",{{"document_id","ellipse"},{"path",path_to_utf8(path)},{"geometry","surface"}});
  auto intent=service.call("cad_read",{{"document_id","ellipse"},{"revision",receipt.at("revision")}}).at("model");const std::string source=intent.at("features")[0].at("id");
  intent["features"].push_back({{"id","edge"},{"type","curve_extract"},{"input",source},{"edges",selector(source,"ellipse")}});
  auto tangent=intent;tangent["features"].push_back({{"id","tangent"},{"type","curve_constrained_line"},{"workplane",frame()},{"constraints",Json::array({constraint("edge","ellipse"),Json{{"point",{4,0,0}}}})},{"solution",solution({2.5,std::sqrt(3.)/4,0})}});tangent["output"]="tangent";const auto samples=BuiltModel(tangent).curve_samples({{"stations",{0,1}}});near(samples.at("length_mm"),std::sqrt(39.)/2);point(samples.at("samples")[0].at("point_mm"),{1,std::sqrt(3.)/2,0});
  auto enclosed=hull(intent.at("features"),{"edge"});BuiltModel built(enclosed);near(built.summary().at("volume_mm3"),2*std::numbers::pi);bool ellipse=false;const auto topology=built.topology("hull");for(const auto& e:topology.at("edges"))if(e.at("curve_kind")=="ellipse")ellipse=true;require(ellipse,"General hull retains native ellipse geometry");
  GProp_GProps volume;BRepGProp::VolumeProperties(read_step(built,root/"ellipse-hull.step"),volume);near(volume.Mass(),2*std::numbers::pi);
}
void lifecycle(const fs::path& root){
  auto intent=tangent_arc();intent["parameters"]["radius"]=.5;intent["features"].back()["radius"]={{"parameter","radius"}};intent["features"].back()["solution"]["tolerance"]=.1;intent["features"].push_back({{"id","trace"},{"type","sketch_trace"},{"input","part"},{"workplane",frame()},{"width",.1}});intent["features"].push_back({{"id","body"},{"type","extrude"},{"input","trace"},{"distance",1}});intent["output"]="body";
  Service service(root/"workspace");auto created=service.call("cad_create",{{"document_id","source"},{"model",intent}});near(created.at("summary").at("volume_mm3"),std::numbers::pi/40);const auto before=service.call("cad_read",{{"document_id","source"},{"revision",1}});
  auto edited=service.call("cad_apply",{{"document_id","source"},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","radius"},{"value",.6}}})}});near(edited.at("summary").at("volume_mm3"),.03*std::numbers::pi);const auto saved=service.call("cad_read",{{"document_id","source"},{"revision",2}});
  fails("invalid_model",[&]{service.call("cad_apply",{{"document_id","source"},{"expected_revision",2},{"operations",Json::array({{{"op","set_parameter"},{"name","radius"},{"value",-1}}})}});});require(service.call("cad_read",{{"document_id","source"}})==saved,"Failed constrained edit preserves exact HEAD");require(service.call("cad_read",{{"document_id","source"},{"revision",1}})==before,"Successful edit preserves immutable source");
  Json diagnostics;evaluate_model(root,intent,{{"kind","view"}},&diagnostics);fs::remove(root/".cache"/(geometry_cache_key(intent)+".json"));evaluate_model(root,intent,{{"kind","view"}},&diagnostics);require(diagnostics.at("feature_hits").at("part")==true,"Constrained feature reuses native cache");const auto keys=feature_cache_keys(intent);intent["features"][0]["path"]["segments"][0]["start"][0]=-3;require(keys.at("part")!=feature_cache_keys(intent).at("part"),"Source edge edit invalidates constrained dependency");
  service.call("cad_create",{{"document_id","consumer"},{"model",model(Json::array({{{"id","seed"},{"type","box"},{"size",{1,1,1}}}}))}});
  const auto receipt=service.call("cad_apply",{{"document_id","consumer"},{"expected_revision",1},{"operations",Json::array({{{"op","set_component"},{"id","module"},{"source_document_id","source"},{"source_revision",2}},{{"op","set_output"},{"feature_id","module"}}})}});const auto portable=service.call("cad_read",{{"document_id","consumer"},{"revision",receipt.at("revision")}}).at("model");Service elsewhere(root/"portable");near(elsewhere.call("cad_create",{{"document_id","restored"},{"model",portable}}).at("summary").at("volume_mm3"),.03*std::numbers::pi);require(portable.at("components")[0].at("feature_map").size()==5,"Component captures both qualified tangency sources and material chain");
  const auto dispatch=[&](const Json& request){for(int retry=0;;++retry){try{return service.call("cad_job",request);}catch(const Error& e){if(e.code!="workspace_busy"||retry>=1000)throw;}std::this_thread::sleep_for(std::chrono::milliseconds(5));}};
  dispatch({{"action","submit"},{"request_id","tangent-edit"},{"tool","cad_apply"},{"arguments",{{"document_id","source"},{"expected_revision",2},{"operations",Json::array({{{"op","set_parameter"},{"name","radius"},{"value",.55}}})}}}});Json job;for(int i=0;i<1000;++i){job=dispatch({{"action","get"},{"job_id","tangent-edit"}});if(job.at("state")=="succeeded"||job.at("state")=="failed")break;std::this_thread::sleep_for(std::chrono::milliseconds(5));}require(job.at("state")=="succeeded","Durable constrained curve edit succeeds");near(job.at("result").at("summary").at("volume_mm3"),.0275*std::numbers::pi);require(!job.at("result").contains("model"),"Durable mutation returns compact receipt");
  auto editable=hull(Json::array({curve("arch",Json::array({bezier({{0,0,0},{0,2,0},{2,2,0},{2,0,0}})}))}),{"arch"});editable["parameters"]["height"]=2;editable["features"][0]["path"]["segments"][0]["points"][1][1]={{"parameter","height"}};editable["features"][0]["path"]["segments"][0]["points"][2][1]={{"parameter","height"}};
  service.call("cad_create",{{"document_id","hull"},{"model",editable}});
  near(service.call("cad_apply",{{"document_id","hull"},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","height"},{"value",3}}})}}).at("summary").at("volume_mm3"),3.6);
  const auto hull_before=service.call("cad_read",{{"document_id","hull"}});
  auto displaced=editable.at("features")[1];displaced["workplane"]["origin"]={0,0,1};
  fails("invalid_model",[&]{service.call("cad_apply",{{"document_id","hull"},{"expected_revision",2},{"operations",Json::array({{{"op","replace_feature"},{"id","hull"},{"feature",displaced}}})}});});
  require(service.call("cad_read",{{"document_id","hull"}})==hull_before,"Failed hull edit preserves HEAD");
  service.call("cad_create",{{"document_id","round"},{"model",round_model(false)}});near(service.call("cad_apply",{{"document_id","round"},{"expected_revision",1},{"operations",Json::array({{{"op","replace_feature"},{"id","rounded"},{"feature",round_model(true).at("features")[1]}}})}}).at("summary").at("volume_mm3"),32-2*std::numbers::pi);
}
}
int main(){try{configure_kernel_logging();set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));Temp temp;tangent_lines(temp.path);tangent_arcs(temp.path);inverted_round(temp.path);general_hulls(temp.path);imported_ellipse(temp.path);lifecycle(temp.path);std::cout<<checks<<" curve completion checks passed\n";return 0;}catch(const Error& e){std::cerr<<e.code<<": "<<e.what()<<" "<<e.details.dump()<<"\n";return 1;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
