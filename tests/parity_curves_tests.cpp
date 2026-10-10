#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/cache.hpp"
#include <STEPControl_Reader.hxx>
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
void require(bool ok,const std::string& message){++checks;if(!ok)throw std::runtime_error(message);}
void near(double actual,double expected,double tolerance=1e-6){require(std::abs(actual-expected)<tolerance,"Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
void point_near(const Json& point,const std::array<double,3>& expected,double tolerance=1e-6){for(int i=0;i<3;++i)near(point[i],expected[i],tolerance);}
Error fails(const std::string& code,const std::function<void()>& action){try{action();}catch(const Error& e){require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what());return e;}throw std::runtime_error("Expected "+code);}
struct Temp{fs::path path=temporary_file(fs::temp_directory_path());Temp(){fs::remove(path);directory(path);}~Temp(){std::error_code ignored;fs::remove_all(path,ignored);}};
Json model(Json features,const std::string& output){return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",features},{"output",output}};}
Json line(Json start,Json end){return {{"type","line"},{"start",start},{"end",end}};}
Json curve(Json segments){return {{"id","curve"},{"type","curve"},{"path",{{"type","wire"},{"segments",segments}}}};}
Json frame(){return {{"origin",{0,0,0}},{"normal",{0,0,1}},{"x_direction",{1,0,0}}};}
Json rational(){return {{"type","bezier"},{"points",{{2,0,0},{2,2,0},{0,2,0}}},{"weights",{1,std::sqrt(.5),1}}};}
TopoDS_Shape step(const BuiltModel& built,const fs::path& path){built.export_file(path,"step");STEPControl_Reader reader;require(reader.ReadFile(path_to_utf8(path).c_str())==IFSelect_RetDone&&reader.TransferRoots()>0,"Independent STEP transfer succeeds");auto shape=reader.OneShape();require(BRepCheck_Analyzer(shape).IsValid(),"Independent STEP result valid");return shape;}
void on_shape(const TopoDS_Shape& shape,const gp_Pnt& point,double tolerance=1e-6){BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(point).Vertex(),shape);require(distance.IsDone()&&distance.Value()<tolerance,"Independent STEP contains analytic curve point");}
void helix(const fs::path& root){for(const auto* hand:{"right","left"}){auto f=frame();f["origin"]={3,4,5};f["normal"]={0,1,0};auto d=model(Json::array({{{"id","helix"},{"type","curve_helix"},{"frame",f},{"radius",2},{"pitch",3},{"turns",2.25},{"handedness",hand}}}),"helix");BuiltModel built(d);Json stations=Json::array();for(int i=0;i<=12;++i)stations.push_back(i/12.0);const auto samples=built.curve_samples({{"stations",stations}});const auto length=std::hypot(4*std::numbers::pi,3)*2.25;near(samples.at("length_mm"),length,1e-6);near(built.summary().at("volume_mm3"),0);const auto exported=step(built,root/(std::string(hand)+".step"));GProp_GProps props;BRepGProp::LinearProperties(exported,props);near(props.Mass(),length,1e-6);
 for(const auto& sample:samples.at("samples")){const double t=sample.at("fraction"),sign=std::string(hand)=="left"?-1:1,theta=sign*2*std::numbers::pi*2.25*t;const std::array<double,3> p={3+2*std::cos(theta),4+6.75*t,5-2*std::sin(theta)};point_near(sample.at("point_mm"),p,1e-6);on_shape(exported,gp_Pnt(p[0],p[1],p[2]));const auto scale=std::hypot(2,3/(2*std::numbers::pi));point_near(sample.at("tangent"),{-2*sign*std::sin(theta)/scale,3/(2*std::numbers::pi)/scale,-2*sign*std::cos(theta)/scale},1e-5);near(sample.at("curvature_per_mm"),2/(4+std::pow(3/(2*std::numbers::pi),2)),2e-5);}
 near(BuiltModel(d,built.snapshot()).curve_samples(Json::object()).at("length_mm"),length,1e-6);auto invalid=d;invalid["features"][0]["turns"]=257;fails("invalid_model",[&]{validate_model(invalid);});}
}
void weighted_and_trim(const fs::path& root){auto segment=rational();segment["weights"][1]={{"parameter","weight"}};auto d=model(Json::array({curve(Json::array({segment}))}),"curve");d["parameters"]["weight"]=std::sqrt(.5);BuiltModel built(d);const auto sampled=built.curve_samples({{"stations",{0,.5,1}}});near(sampled.at("length_mm"),std::numbers::pi);point_near(sampled.at("samples")[1].at("point_mm"),{std::sqrt(2),std::sqrt(2),0});near(sampled.at("samples")[1].at("curvature_per_mm"),.5);const auto shape=step(built,root/"rational.step");on_shape(shape,gp_Pnt(std::sqrt(2),std::sqrt(2),0));
 d["features"].push_back({{"id","trim"},{"type","curve_trim"},{"input","curve"},{"start",.25},{"end",.75}});d["output"]="trim";BuiltModel trim(d);near(trim.curve_samples(Json::object()).at("length_mm"),std::numbers::pi/2);const auto keys=feature_cache_keys(d);d["parameters"]["weight"]=.8;require(keys.at("trim")!=feature_cache_keys(d).at("trim"),"Weight edits invalidate dependent curve trim");
 auto bad=d;bad["parameters"]["weight"]=0;fails("invalid_model",[&]{validate_model(bad);});bad=d;bad["features"][0]["path"]["segments"][0]["weights"]={1,1};fails("invalid_model",[&]{validate_model(bad);});
 auto lines=model(Json::array({curve(Json::array({line({0,0,0},{3,0,0}),line({3,0,0},{10,0,0})})),{{"id","trim"},{"type","curve_trim"},{"input","curve"},{"start",.2},{"end",.8}}}),"trim");const auto samples=BuiltModel(lines).curve_samples({{"stations",{0,1.0/6,1}}});near(samples.at("length_mm"),6);point_near(samples.at("samples")[0].at("point_mm"),{2,0,0});point_near(samples.at("samples")[2].at("point_mm"),{8,0,0});
 auto p=rational();p["points"]={{2,0},{2,2},{0,2}};auto profile=Json{{"type","wire"},{"segments",Json::array({p,line({0,2},{0,0}),line({0,0},{2,0})})}};auto solid=model(Json::array({{{"id","sketch"},{"type","sketch"},{"workplane",frame()},{"profile",profile}},{{"id","body"},{"type","extrude"},{"input","sketch"},{"distance",3}}}),"body");near(BuiltModel(solid).summary().at("volume_mm3"),3*std::numbers::pi,1e-5);
}
void tangent_constraints(){auto d=model(Json::array({curve(Json::array({line({0,0,0},{2,0,0})})),{{"id","arc"},{"type","curve_tangent_arc"},{"input","curve"},{"position",1},{"end",{3,1,0}}}}),"arc");BuiltModel arc(d);const auto samples=arc.curve_samples({{"stations",{0,1}}});near(samples.at("length_mm"),std::numbers::pi/2);point_near(samples.at("samples")[0].at("tangent"),{1,0,0});point_near(samples.at("samples")[1].at("tangent"),{0,1,0});
 d["features"].push_back({{"id","line"},{"type","curve_tangent_line"},{"input","arc"},{"position",1},{"length",3}});d["output"]="line";point_near(BuiltModel(d).curve_samples({{"stations",{1}}}).at("samples")[0].at("point_mm"),{3,4,0});
 auto bad=d;bad["features"][1]["end"]={3,0,0};fails("invalid_model",[&]{BuiltModel failed(bad);});
 auto cusp=model(Json::array({curve(Json::array({line({0,0,0},{1,0,0}),line({1,0,0},{1,1,0})}))}),"curve");BuiltModel corner(cusp);fails("selection_ambiguous",[&]{corner.curve_samples({{"stations",{.5}}});});
 const auto segment=Json{{"type","tangent_arc"},{"start",{0,0,0}},{"end",{1,1,0}},{"tangent",{1,0,0}}};near(BuiltModel(model(Json::array({curve(Json::array({segment}))}),"curve")).curve_samples(Json::object()).at("length_mm"),std::numbers::pi/2);
}
void selectors_and_worker(const fs::path& root){auto d=model(Json::array({{{"id","cylinder"},{"type","cylinder"},{"radius",2},{"height",5}}}),"cylinder");BuiltModel cylinder(d);Json edge={{"type","geometric"},{"feature_id","cylinder"},{"curve_kind","circle"},{"expected_count",1},{"radius",{{"value",2},{"tolerance",1e-6}}},{"axis",{{"vector",{0,0,-1}},{"tolerance",1e-6}}},{"closed",true},{"center",{{"point",{0,0,5}},{"tolerance",1e-6}}}};near(cylinder.curve_samples({{"edge",edge},{"stations",{0,.5,1}}}).at("length_mm"),4*std::numbers::pi);auto ambiguous=edge;ambiguous.erase("center");fails("selection_ambiguous",[&]{cylinder.curve_samples({{"edge",ambiguous}});});
 d["features"].push_back({{"id","curve"},{"type","curve_extract"},{"input","cylinder"},{"edges",edge}});d["output"]="curve";near(BuiltModel(d).curve_samples(Json::object()).at("length_mm"),4*std::numbers::pi);Service service(root/"worker");service.call("cad_create",{{"document_id","curve"},{"model",d}});const auto report=service.call("cad_query",{{"document_id","curve"},{"revision",1},{"kind","curve"},{"curve",{{"stations",{0,.25,1}}}}});require(report.at("revision")==1&&report.contains("evaluation_id")&&report.at("feature_id")=="curve","Curve samples carry committed source/evaluation identity");near(report.at("curve").at("length_mm"),4*std::numbers::pi);
 const auto before=service.call("cad_read",{{"document_id","curve"}});auto feature=d.at("features")[1];feature["edges"]=ambiguous;fails("selection_ambiguous",[&]{service.call("cad_apply",{{"document_id","curve"},{"expected_revision",1},{"operations",Json::array({{{"op","replace_feature"},{"id","curve"},{"feature",feature}}})}});});require(service.call("cad_read",{{"document_id","curve"}})==before,"Failed curve selection preserves revision and source bytes");
 const auto straight=model(Json::array({curve(Json::array({line({1,2,3},{4,6,3})}))}),"curve");Json ends={{"type","geometric"},{"feature_id","curve"},{"curve_kind","line"},{"expected_count",1},{"endpoints",{{"points",{{4,6,3},{1,2,3}}},{"tolerance",1e-6}}},{"closed",false}};near(BuiltModel(straight).curve_samples({{"edge",ends}}).at("length_mm"),5);
}
}
int main(){try{configure_kernel_logging();set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));Temp temp;helix(temp.path);weighted_and_trim(temp.path);tangent_constraints();selectors_and_worker(temp.path);std::cout<<checks<<" curve parity checks passed\n";return 0;}catch(const Error& e){std::cerr<<e.code<<": "<<e.what()<<" "<<e.details.dump()<<"\n";return 1;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
