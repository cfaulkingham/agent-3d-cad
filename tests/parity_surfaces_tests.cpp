#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/cache.hpp"
#include "agentcad/hash.hpp"
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <GeomLProp_SLProps.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <thread>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <algorithm>
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
struct Temp{fs::path path=temporary_file(fs::temp_directory_path());Temp(){fs::remove(path);directory(path);}~Temp(){std::error_code ignored;fs::remove_all(path,ignored);}};
Json model(Json features,const std::string& output){return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",features},{"output",output}};}
Json line(Json a,Json b){return {{"type","line"},{"start",a},{"end",b}};}
Json polygon(Json points){Json result=Json::array();for(std::size_t i=0;i<points.size();++i)result.push_back(line(points[i],points[(i+1)%points.size()]));return result;}
Json plane(){return {{"id","patch"},{"type","surface_bezier"},{"control_points",{{{0,0,0},{0,20,0}},{{10,0,0},{10,20,0}}}}};}
Json cylindrical(){auto p=plane();p["control_points"]={{{10,0,0},{10,0,5}},{{10,10,0},{10,10,5}},{{0,10,0},{0,10,5}}};const auto w=std::sqrt(.5);p["weights"]={{1,1},{w,w},{1,1}};return p;}
TopoDS_Shape independent_step(const BuiltModel& built,const fs::path& path){built.export_file(path,"step");STEPControl_Reader r;require(r.ReadFile(path_to_utf8(path).c_str())==IFSelect_RetDone,"Independent STEP read");require(r.TransferRoots()>0,"Independent STEP transfer");require(BRepCheck_Analyzer(r.OneShape()).IsValid(),"Independent STEP validity");return r.OneShape();}
void crown(const fs::path& root){auto doc=parse_json(read_text(fs::path(CAD_SOURCE_DIR)/"examples/freeform-panel.create.json")).at("model");
 for(const auto* join:{"arc","intersection"})for(int sign:{-1,1}){auto d=doc;d["features"].push_back({{"id","body"},{"type","thicken"},{"input","trimmed"},{"thickness",sign},{"join",join}});d["output"]="body";BuiltModel solid(d);require(solid.summary().at("solid_count")==1,"Crown thickens into one valid solid");const double volume=solid.summary().at("volume_mm3");require(volume>250&&volume<270,"Crown material volume corresponds to 16x16 mm patch and 1 mm wall");const auto shape=independent_step(solid,root/(std::string(join)+std::to_string(sign)+".step"));GProp_GProps mass;BRepGProp::VolumeProperties(shape,mass);near(mass.Mass(),volume,1e-3);near(BuiltModel(d,solid.snapshot()).summary().at("volume_mm3"),volume,1e-5);require(!solid.topology().at("provenance").at("history").empty(),"Normalized thickening retains source topology history");}
}
void mixed_volume() {
 auto d=parse_json(read_text(fs::path(CAD_SOURCE_DIR)/"examples/freeform-panel.create.json")).at("model");
 d["features"].push_back({{"id","body"},{"type","thicken"},{"input","trimmed"},{"thickness",1},{"join","arc"}});d["output"]="body";
 const auto crown_mass=BuiltModel(d).summary();
 d["features"].push_back({{"id","cylinder"},{"type","cylinder"},{"radius",5},{"height",10}});
 d["features"].push_back({{"id","ellipse"},{"type","scale"},{"input","cylinder"},{"factors",{2,3,.5}},{"origin",{1,2,3}}});
 d["features"].push_back({{"id","mixed"},{"type","assembly"},{"parts",Json::array({
   Json{{"id","panel"},{"input","body"}},
   Json{{"id","elliptic"},{"input","ellipse"},{"placement",{{"translation",{50,60,70}},{"rotation",{{"origin",{0,0,0}},{"axis",{0,0,1}},{"angle_deg",90}}}}}}
 })}});d["output"]="mixed";
 const auto summary=BuiltModel(d).summary();
 const double cylinder_volume=750*std::numbers::pi,crown_volume=crown_mass.at("volume_mm3"),total=cylinder_volume+crown_volume;
 near(summary.at("volume_mm3"),total,1e-5);require(summary.at("solid_count")==2,"Mixed rational and polynomial assembly preserves both solids");
 const std::array<double,3> cylinder_center={54,59,74};
 for(int axis=0;axis<3;++axis)near(summary.at("center_of_mass_mm")[axis],
   (crown_volume*crown_mass.at("center_of_mass_mm")[axis].get<double>()+cylinder_volume*cylinder_center[axis])/total,1e-6);
 near(BuiltModel(d,BuiltModel(d).snapshot()).summary().at("volume_mm3"),total,1e-5);
}
void polygon_volume() {
 const auto boundary=polygon({{0,0},{8,0},{8,1},{10,1},{10,3},{8,3},{8,4},{10,4},{10,6},{8,6},{8,7},{0,7}});
 const auto lower=polygon({{1,1},{7,1},{7,3},{1,3}}),upper=polygon({{1,4},{7,4},{7,6},{1,6}});
 auto d=model(Json::array({
   Json{{"id","profile"},{"type","sketch"},{"workplane",{{"origin",{0,0,0}},{"normal",{0,0,1}},{"x_direction",{1,0,0}}}},
     {"profile",{{"type","wire"},{"segments",boundary},{"holes",Json::array({lower,upper})}}}},
   Json{{"id","part"},{"type","extrude"},{"input","profile"},{"distance",3}}
 }),"part");
 const auto summary=BuiltModel(d).summary();near(summary.at("volume_mm3"),120,1e-8);
 // Exact rectangle moment arithmetic: (56*4+8*9-24*4)/40=5.
 near(summary.at("center_of_mass_mm")[0],5,1e-8);near(summary.at("center_of_mass_mm")[1],3.5,1e-8);near(summary.at("center_of_mass_mm")[2],1.5,1e-8);
}
void imports(const fs::path& root){auto source=model(Json::array({plane()}),"patch");BuiltModel sheet(source);sheet.export_file(root/"surface.step","step");Service service(root/"imports");auto record=service.call("cad_import",{{"document_id","surface"},{"path",path_to_utf8(root/"surface.step")},{"geometry","surface"}});record["model"]=service.call("cad_read",{{"document_id","surface"},{"revision",record.at("revision")}}).at("model");require(record.at("model").at("features")[0].at("type")=="import_step_surface","Surface source intent is explicit");near(record.at("summary").at("area_mm2"),200);near(record.at("summary").at("volume_mm3"),0);require(record.at("summary").at("solid_count")==0,"Surface STEP does not imply solid");fails("invalid_shape",[&]{service.call("cad_import",{{"document_id","wrong"},{"path",path_to_utf8(root/"surface.step")}});});
 auto invalid=record.at("model");invalid["features"].push_back({{"id","wrong"},{"type","offset"},{"input","imported"},{"distance",1}});invalid["output"]="wrong";fails("invalid_model",[&]{validate_model(invalid);});
 fs::remove(root/"surface.step");Service reopened(root/"imports");near(reopened.call("cad_query",{{"document_id","surface"},{"revision",1}}).at("summary").at("area_mm2"),200);auto d=record.at("model");d["features"].push_back({{"id","body"},{"type","thicken"},{"input","imported"},{"thickness",2}});d["output"]="body";near(BuiltModel(d).summary().at("volume_mm3"),400);
 BuiltModel solid(model(Json::array({{{"id","box"},{"type","box"},{"size",{2,3,4}}}}),"box"));solid.export_file(root/"solid.step","step");fails("invalid_shape",[&]{service.call("cad_import",{{"document_id","wrongkind"},{"path",path_to_utf8(root/"solid.step")},{"geometry","surface"}});});
 const auto solid_shape=independent_step(solid,root/"fixture-solid.step");const auto shell=TopExp_Explorer(solid_shape,TopAbs_SHELL).Current();STEPControl_Writer writer;require(writer.Transfer(shell,STEPControl_AsIs)==IFSelect_RetDone,"Shell fixture transfer");require(writer.Write(path_to_utf8(root/"closed-shell.step").c_str())==IFSelect_RetDone,"Shell fixture save");const auto closed=service.call("cad_import",{{"document_id","closed"},{"path",path_to_utf8(root/"closed-shell.step")},{"geometry","surface"}});require(closed.at("summary").at("solid_count")==0,"Closed imported shell is nonmaterial");auto material=service.call("cad_read",{{"document_id","closed"},{"revision",closed.at("revision")}}).at("model");material["features"].push_back({{"id","material"},{"type","surface_solid"},{"input","imported"}});material["output"]="material";near(BuiltModel(material).summary().at("volume_mm3"),24);
}
void trims(const fs::path& root){const auto outer=polygon({{.1,.1},{.9,.1},{.9,.9},{.1,.9}}),hole=polygon({{.4,.4},{.6,.4},{.6,.6},{.4,.6}});auto d=model(Json::array({plane(),{{"id","trimmed"},{"type","surface_trim"},{"input","patch"},{"boundary",outer},{"holes",Json::array({hole})}}}),"trimmed");BuiltModel trimmed(d);near(trimmed.summary().at("area_mm2"),120);require(trimmed.topology().at("edges").size()==8,"Contour hole preserves both exact boundary wires");independent_step(trimmed,root/"trim.step");
 auto crossed=d;crossed["features"][1]["boundary"]=polygon({{.1,.1},{.9,.9},{.1,.9},{.9,.1}});fails("invalid_shape",[&]{BuiltModel invalid(crossed);});
 auto outside=d;outside["features"][1]["boundary"]=polygon({{-.1,.1},{.9,.1},{.9,.9},{-.1,.9}});fails("invalid_model",[&]{BuiltModel invalid(outside);});
 auto untrim=d;untrim["features"].push_back({{"id","refill"},{"type","surface_trim"},{"input","trimmed"},{"u_range",{.2,.8}},{"v_range",{.2,.8}}});untrim["output"]="refill";fails("invalid_model",[&]{BuiltModel invalid(untrim);});
 auto circular=d;circular["features"][1]["boundary"]=Json::array({{{"type","arc"},{"start",{.9,.5}},{"mid",{.5,.9}},{"end",{.1,.5}}},{{"type","arc"},{"start",{.1,.5}},{"mid",{.5,.1}},{"end",{.9,.5}}}});circular["features"][1].erase("holes");near(BuiltModel(circular).summary().at("area_mm2"),32*std::numbers::pi,1e-4);
 d["features"][0]=cylindrical();BuiltModel curved(d);require(curved.summary().at("area_mm2")>0,"Curved UV contour and hole build");independent_step(curved,root/"curved-trim.step");
}
void filling(const fs::path& root){Json boundary=Json::array();for(const auto& curve:polygon({{0,0,0},{10,0,0},{10,10,0},{0,10,0}}))boundary.push_back({{"curve",curve},{"continuity","C0"}});auto d=model(Json::array({{{"id","fill"},{"type","surface_fill"},{"boundaries",boundary},{"tolerance",1e-5}}}),"fill");near(BuiltModel(d).summary().at("area_mm2"),100,1e-3);
 const auto support=plane();BuiltModel p(model(Json::array({support}),"patch"));const auto topology=p.topology();Json edges=Json::array();const std::vector<std::array<double,3>> centers={{{5,0,0}},{{10,10,0}},{{5,20,0}},{{0,10,0}}};
 for(const auto& center:centers){for(const auto& edge:topology.at("edges")){const auto c=edge.at("center_mm").get<std::array<double,3>>();if(std::hypot(c[0]-center[0],c[1]-center[1])<1e-5)edges.push_back(edge.at("selector"));}}
 require(edges.size()==4,"Find stable geometric boundary references");
 for(const auto* continuity:{"G1","G2"}){Json constraints=Json::array();for(std::size_t i=0;i<edges.size();++i)constraints.push_back({{"input","patch"},{"edge",edges[i]},{"face",topology.at("faces")[0].at("selector")},{"continuity",continuity},{"reverse",false}});auto filled=model(Json::array({support,{{"id","fill"},{"type","surface_fill"},{"boundaries",constraints},{"tolerance",1e-5}}}),"fill");BuiltModel b(filled);near(b.summary().at("area_mm2"),200,1e-3);independent_step(b,root/(std::string(continuity)+".step"));require(b.topology().at("provenance").at("dependencies")==Json::array({"patch"}),"Filling dependencies retained");auto keys=feature_cache_keys(filled);filled["features"][0]["control_points"][1][0][0]=12;require(keys.at("fill")!=feature_cache_keys(filled).at("fill"),"Support edits invalidate filling cache");}
 auto open=d;open["features"][0]["boundaries"].erase(3);fails("invalid_model",[&]{BuiltModel invalid(open);});
}

void filling_support_intent(const fs::path& root) {
  const auto constraints_for=[](const Json& topology,const std::string& input,const std::vector<std::array<double,3>>& centers) {
    Json constraints=Json::array();
    for(const auto& center:centers)for(const auto& edge:topology.at("edges")) {
      const auto point=edge.at("center_mm").get<std::array<double,3>>();
      if(std::hypot(point[0]-center[0],point[1]-center[1],point[2]-center[2])<1e-6)
        constraints.push_back({{"input",input},{"edge",edge.at("selector")},{"face",topology.at("faces")[0].at("selector")},{"continuity","G1"}});
    }
    require(constraints.size()==centers.size(),"Fill intent has uniquely ordered boundary selectors");return constraints;
  };
  auto d=model(Json::array({plane()}),"patch");
  const auto constraints=constraints_for(BuiltModel(d).topology(),"patch",{{5,0,0},{10,10,0},{5,20,0},{0,10,0}});
  Json fill={{"id","fill"},{"type","surface_fill"},{"boundaries",constraints},{"tolerance",1e-5},{"angular_tolerance",1e-3}};
  d["features"].push_back(fill);d["output"]="fill";
  for(bool reverse:{false,true}) {
    auto oriented=d;if(reverse){std::reverse(oriented["features"][1]["boundaries"].begin(),oriented["features"][1]["boundaries"].end());for(auto& edge:oriented["features"][1]["boundaries"])edge["reverse"]=true;}
    auto face=independent_step(BuiltModel(oriented),root/(reverse?"fill-reversed.step":"fill-forward.step"));
    const auto support=TopoDS::Face(TopExp_Explorer(face,TopAbs_FACE).Current());
    double u0,u1,v0,v1;BRepTools::UVBounds(support,u0,u1,v0,v1);GeomLProp_SLProps normal(BRep_Tool::Surface(support),(u0+u1)/2,(v0+v1)/2,1,1e-9);
    require(normal.IsNormalDefined(),"STEP fill has a defined normal");
    const auto z=normal.Normal().Z()*(support.Orientation()==TopAbs_REVERSED?-1:1);near(z,reverse?-1:1,1e-8);
    oriented["features"].push_back({{"id","body"},{"type","thicken"},{"input","fill"},{"thickness",2}});oriented["output"]="body";
    const auto summary=BuiltModel(oriented).summary();near(summary.at("volume_mm3"),400,1e-5);
    near(summary.at("bounds_mm").at("min")[2],reverse?-2:0,1e-5);near(summary.at("bounds_mm").at("max")[2],reverse?0:2,1e-5);
  }
  // A point off the support is an achievable interior bulge. It must reach the
  // general plate solver, not be erased by returning the selected planar face.
  for(const auto* continuity:{"G1","G2"}) {
    auto bulged=d;bulged["features"][1]["points"]={{5,10,.1}};bulged["features"][1]["curvature_tolerance"]=1e-3;
    for(auto& boundary:bulged["features"][1]["boundaries"])boundary["continuity"]=continuity;
    BuiltModel bulge(bulged);require(bulge.summary().at("area_mm2").get<double>()>200.001,"Off-support interior point changes the fill geometry");
    const auto bulge_shape=independent_step(bulge,root/(std::string("fill-interior-bulge-")+continuity+".step"));
    const auto surface=BRep_Tool::Surface(TopoDS::Face(TopExp_Explorer(bulge_shape,TopAbs_FACE).Current()));
    GeomAPI_ProjectPointOnSurf projection(gp_Pnt(5,10,.1),surface);
    require(projection.IsDone()&&projection.NbPoints()>0&&projection.LowerDistance()<1e-5,"Independent STEP preserves the authored off-support interior point");
    for(int side=0;side<4;++side)for(int sample=0;sample<=16;++sample) {
      const double t=sample/16.0;const gp_Pnt point(side%2? (side==1?10:0):10*t,side%2?20*t:(side==0?0:20),0);
      GeomAPI_ProjectPointOnSurf boundary(point,surface);require(boundary.IsDone()&&boundary.NbPoints()>0&&boundary.LowerDistance()<1e-5,"STEP bulge preserves its entire rectangular boundary");
      double u,v;boundary.LowerDistanceParameters(u,v);GeomLProp_SLProps props(surface,u,v,2,1e-9);require(props.IsNormalDefined(),"STEP bulge boundary normal exists");
      require(std::acos(std::clamp(std::abs(props.Normal().Z()),0.0,1.0))<1e-3,"General-solver STEP boundary retains planar G1 support");
      if(std::string(continuity)=="G2") {require(props.IsCurvatureDefined(),"General-solver STEP G2 curvature exists");require(std::max(std::abs(props.MinCurvature()),std::abs(props.MaxCurvature()))<1e-3,"General-solver STEP boundary retains both planar support curvatures");}
    }
    near(BuiltModel(bulged,bulge.snapshot()).summary().at("area_mm2"),bulge.summary().at("area_mm2"),1e-6);
  }
  // Selecting only the outer boundary of a holed support explicitly fills that
  // loop; the unrelated inner trim must not be copied into the new face.
  auto trimmed=model(Json::array({plane(),{{"id","trimmed"},{"type","surface_trim"},{"input","patch"},{"boundary",polygon({{.1,.1},{.9,.1},{.9,.9},{.1,.9}})},{"holes",Json::array({polygon({{.4,.4},{.6,.4},{.6,.6},{.4,.6}})})}}}),"trimmed");
  auto outer=fill;outer["boundaries"]=constraints_for(BuiltModel(trimmed).topology(),"trimmed",{{5,2,0},{9,10,0},{5,18,0},{1,10,0}});
  trimmed["features"].push_back(outer);trimmed["output"]="fill";near(BuiltModel(trimmed).summary().at("area_mm2"),128,1e-4);
  // A common result tangent plane cannot match orthogonal supports at the exact
  // same corner within 1e-3 rad. This is a geometric contradiction, not a test
  // that relies on one particular native solver failing to converge.
  auto vertical=plane();vertical["id"]="vertical";vertical["control_points"]={{{0,0,0},{0,0,5}},{{10,0,0},{10,0,5}}};
  auto conflicting=model(Json::array({plane(),vertical}),"patch");
  const auto bottom=constraints_for(BuiltModel(model(Json::array({vertical}),"vertical")).topology(),"vertical",{{5,0,0}});
  auto impossible=fill;impossible["boundaries"][0]=bottom[0];conflicting["features"].push_back(impossible);conflicting["output"]="fill";
  const auto failure=fails("invalid_shape",[&]{BuiltModel rejected(conflicting);});require(failure.details.at("support_angle_rad").get<double>()>1.5,"Contradictory support planes are measured directly");
  Service service(root/"fill-intent-worker");const auto created=service.call("cad_create",{{"document_id","fill"},{"model",model(Json::array({plane(),vertical}),"patch")}});
  const auto before=service.call("cad_read",{{"document_id","fill"},{"revision",created.at("revision")}});
  fails("invalid_shape",[&]{service.call("cad_apply",{{"document_id","fill"},{"expected_revision",created.at("revision")},{"operations",Json::array({{{"op","add_feature"},{"feature",impossible}},{{"op","set_output"},{"feature_id","fill"}}})}});});
  require(service.call("cad_read",{{"document_id","fill"}})==before,"Contradictory worker fill preserves the complete prior revision");
  auto exact=d;const auto receipt=service.call("cad_create",{{"document_id","exact"},{"model",exact}});near(receipt.at("summary").at("area_mm2"),200,1e-5);
  require(service.call("cad_read",{{"document_id","exact"},{"revision",receipt.at("revision")}}).at("model")==exact,"Exact-support fill preserves every authored constraint in source");
}

void nonplanar_continuity(const fs::path& root,double position_tolerance=1e-5) {
  const auto patch=cylindrical();BuiltModel support(model(Json::array({patch}),"patch"));const auto topology=support.topology();
  std::vector<Json> edges=topology.at("edges").get<std::vector<Json>>();
  const auto order=[](const Json& edge){const auto c=edge.at("center_mm").get<std::array<double,3>>();return std::abs(c[2])<1e-5?0:std::abs(c[0])<1e-5?1:std::abs(c[2]-5)<1e-5?2:3;};
  std::sort(edges.begin(),edges.end(),[&](const Json& a,const Json& b){return order(a)<order(b);});
  Json boundaries=Json::array();for(const auto& edge:edges)boundaries.push_back({{"input","patch"},{"edge",edge.at("selector")},{"face",topology.at("faces")[0].at("selector")},{"continuity","G2"}});
  const auto d=model(Json::array({patch,{{"id","fill"},{"type","surface_fill"},{"boundaries",boundaries},{"tolerance",position_tolerance},{"angular_tolerance",1e-3},{"curvature_tolerance",1e-3},{"points",{{std::sqrt(50),std::sqrt(50),2.5}}}}}),"fill");
  BuiltModel filled(d);const auto shape=independent_step(filled,root/(position_tolerance<1e-5?"cylinder-G2-refined.step":"cylinder-G2.step"));const auto face=TopoDS::Face(TopExp_Explorer(shape,TopAbs_FACE).Current());const auto surface=BRep_Tool::Surface(face);
  GProp_GProps exact_area;BRepGProp::SurfaceProperties(shape,exact_area,1e-10);near(exact_area.Mass(),25*std::numbers::pi,1e-7);
  double max_distance=0,max_angle=0,max_curvature=0;
  // Independent analytic quarter-cylinder oracle after a fresh STEP read.
  // Boundary continuity does not constrain the whole interior to a cylinder.
  for(int side=0;side<4;++side)for(int sample=0;sample<=16;++sample){
    const double t=sample/16.0,angle=side%2==0?t*std::numbers::pi/2:(side==1?std::numbers::pi/2:0),z=side%2==1?t*5:(side==0?0:5);
    const gp_Pnt point(10*std::cos(angle),10*std::sin(angle),z);GeomAPI_ProjectPointOnSurf projection(point,surface);
    require(projection.IsDone()&&projection.NbPoints()>0,"Independent G2 boundary projection succeeds");max_distance=std::max(max_distance,projection.LowerDistance());
    double u,v;projection.LowerDistanceParameters(u,v);GeomLProp_SLProps props(surface,u,v,2,1e-9);require(props.IsNormalDefined()&&props.IsCurvatureDefined(),"Independent G2 differential geometry is defined");
    const gp_Dir radial(std::cos(angle),std::sin(angle),0),azimuth(-std::sin(angle),std::cos(angle),0),axis(0,0,1);max_angle=std::max(max_angle,std::acos(std::clamp(std::abs(props.Normal().Dot(radial)),0.0,1.0)));
    gp_Dir maximum,minimum;props.CurvatureDirections(maximum,minimum);
    const auto k=[&](const gp_Dir& a,const gp_Dir& b){return props.MaxCurvature()*maximum.Dot(a)*maximum.Dot(b)+props.MinCurvature()*minimum.Dot(a)*minimum.Dot(b);};
    max_curvature=std::max({max_curvature,std::abs(std::abs(k(azimuth,azimuth))-.1),std::abs(k(axis,axis)),std::abs(k(axis,azimuth))});
  }
  require(max_distance<position_tolerance,"Nonplanar G2 boundary position meets authored tolerance after STEP");require(max_angle<1e-3,"Nonplanar G2 tangent plane meets authored tolerance after STEP");require(max_curvature<1e-3,"Nonplanar G2 curvature tensor agrees with analytic cylinder after STEP");
  GeomAPI_ProjectPointOnSurf interior(gp_Pnt(std::sqrt(50),std::sqrt(50),2.5),surface);require(interior.IsDone()&&interior.LowerDistance()<position_tolerance,"G2 interior point is independently preserved");
  std::cout<<"G2 STEP errors (position tolerance="<<position_tolerance<<"): distance="<<max_distance<<" mm, angle="<<max_angle<<" rad, curvature="<<max_curvature<<" /mm\n";
  auto ambiguous=d;ambiguous["features"][1]["boundaries"][0]["edge"]={{"type","geometric"},{"feature_id","patch"},{"curve_kind",edges[0].at("selector").at("curve_kind")},{"expected_count",1}};fails("selection_ambiguous",[&]{BuiltModel invalid(ambiguous);});
}
void component_capture(const fs::path& root) {
  const auto patch=plane();BuiltModel support(model(Json::array({patch}),"patch"));const auto topology=support.topology();Json boundaries=Json::array();
  const std::vector<std::array<double,3>> centers={{{5,0,0}},{{10,10,0}},{{5,20,0}},{{0,10,0}}};
  for(const auto& center:centers)for(const auto& edge:topology.at("edges")){const auto c=edge.at("center_mm").get<std::array<double,3>>();if(std::hypot(c[0]-center[0],c[1]-center[1])<1e-5)boundaries.push_back({{"input","patch"},{"edge",edge.at("selector")},{"face",topology.at("faces")[0].at("selector")},{"continuity","G1"}});}
  const auto source=model(Json::array({patch,{{"id","fill"},{"type","surface_fill"},{"boundaries",boundaries},{"tolerance",1e-5}},{{"id","body"},{"type","thicken"},{"input","fill"},{"thickness",1}}}),"body");
  Service service(root/"components");service.call("cad_create",{{"document_id","library"},{"model",source}});service.call("cad_create",{{"document_id","consumer"},{"model",model(Json::array({{{"id","seed"},{"type","box"},{"size",{1,1,1}}}}),"seed")}});
  const auto captured=service.call("cad_apply",{{"document_id","consumer"},{"expected_revision",1},{"operations",Json::array({{{"op","set_component"},{"id","module"},{"source_document_id","library"},{"source_revision",1}},{{"op","set_output"},{"feature_id","module"}}})}});
  near(captured.at("summary").at("volume_mm3"),200,1e-3);const auto mapped=service.call("cad_read",{{"document_id","consumer"},{"revision",captured.at("revision")}}).at("model");const auto& names=mapped.at("components")[0].at("feature_map");require(names.size()==3,"Component captures filling support dependency closure");
  for(const auto& feature:mapped.at("features"))if(feature.at("id")==names.at("fill"))for(const auto& boundary:feature.at("boundaries")){require(boundary.at("input")==names.at("patch"),"Component remaps nested filling input");require(boundary.at("edge").at("feature_id")==names.at("patch")&&boundary.at("face").at("feature_id")==names.at("patch"),"Component remaps both support selectors");}
  Service portable(root/"portable");near(portable.call("cad_create",{{"document_id","copy"},{"model",mapped}}).at("summary").at("volume_mm3"),200,1e-3);
}

void networks(const fs::path& root){Json u=Json::array(),v=Json::array();for(int station:{0,5,10}){const double z=station==5?2:0;u.push_back({{"type","spline"},{"points",{{0,station,z},{5,station,z+2},{10,station,z}}}});v.push_back({{"type","spline"},{"points",{{station,0,z},{station,5,z+2},{station,10,z}}}});}
 auto d=model(Json::array({{{"id","network"},{"type","surface_gordon"},{"u_curves",u},{"v_curves",v},{"u_parameters",{0,.5,1}},{"v_parameters",{0,.5,1}},{"tolerance",1e-6}}}),"network");BuiltModel built(d);require(built.summary().at("area_mm2")>100,"Curved spline network produces nonplanar Gordon face");const auto shape=independent_step(built,root/"network.step");const auto face=TopoDS::Face(TopExp_Explorer(shape,TopAbs_FACE).Current());GeomAPI_ProjectPointOnSurf middle(gp_Pnt(5,5,4),BRep_Tool::Surface(face));require(middle.IsDone()&&middle.LowerDistance()<1e-5,"Independent STEP interpolates interior curve crossing");near(BuiltModel(d,built.snapshot()).summary().at("area_mm2"),built.summary().at("area_mm2"),1e-6);
 auto bad=d;bad["features"][0]["v_curves"][1]["points"][1][2]=5;fails("invalid_model",[&]{BuiltModel invalid(bad);});
}
void projections(const fs::path& root){const auto select=Json{{"type","geometric"},{"feature_id","patch"},{"surface_kind","bezier"},{"expected_count",1}};auto d=model(Json::array({cylindrical(),{{"id","path"},{"type","curve"},{"path",{{"type","wire"},{"segments",Json::array({line({15,2,2},{15,8,2})})}}}},{{"id","projected"},{"type","curve_project"},{"input","path"},{"target","patch"},{"faces",select},{"direction",{-1,0,0}}}}),"projected");BuiltModel curve(d);near(curve.summary().at("volume_mm3"),0);near(curve.summary().at("area_mm2"),0);require(curve.summary().at("face_count")==0,"Open projected wire stays a curve");const auto topology=curve.topology();near(topology.at("edges")[0].at("length_mm"),10*(std::asin(.8)-std::asin(.2)),1e-4);require(!curve.mesh().at("edges").empty(),"Curve output renders edge polylines");independent_step(curve,root/"projected-curve.step");fails("invalid_argument",[&]{curve.print_meshes();});fails("invalid_argument",[&]{curve.export_file(root/"curve.stl","stl");});
 auto material=d;material["features"].push_back({{"id","invalid"},{"type","offset"},{"input","projected"},{"distance",1}});material["output"]="invalid";fails("invalid_model",[&]{validate_model(material);});
 auto wrong=d;wrong["features"][2]["direction"]={1,0,0};fails("selection_missing",[&]{BuiltModel invalid(wrong);});
 d["features"][1]["path"]["segments"]=polygon({{15,2,1},{15,8,1},{15,8,4},{15,2,4}});d["features"][2]["type"]="surface_project";BuiltModel region(d);near(region.summary().at("area_mm2"),30*(std::asin(.8)-std::asin(.2)),1e-3);independent_step(region,root/"projected-region.step");
 const auto outer=polygon({{0,0},{6,0},{6,3},{0,3}}),hole=polygon({{2,1},{4,1},{4,2},{2,2}});d["features"][1]={{"id","path"},{"type","sketch"},{"workplane",{{"origin",{15,2,1}},{"normal",{1,0,0}},{"x_direction",{0,1,0}}}},{"profile",{{"type","wire"},{"segments",outer},{"holes",Json::array({hole})}}}};BuiltModel holed(d);near(holed.summary().at("area_mm2"),30*(std::asin(.8)-std::asin(.2))-10*(std::asin(.6)-std::asin(.4)),1e-3);independent_step(holed,root/"projected-hole.step");
}
Json circle_segments(double radius,double z,bool dimensions3=true) {
  if(dimensions3)return Json::array({{{"type","arc"},{"start",{radius,0,z}},{"mid",{0,radius,z}},{"end",{-radius,0,z}}},{{"type","arc"},{"start",{-radius,0,z}},{"mid",{0,-radius,z}},{"end",{radius,0,z}}}});
  return Json::array({{{"type","arc"},{"start",{radius,0}},{"mid",{0,radius}},{"end",{-radius,0}}},{{"type","arc"},{"start",{-radius,0}},{"mid",{0,-radius}},{"end",{radius,0}}}});
}
Json network(Json u,Json v){return {{"id","network"},{"type","surface_gordon"},{"u_curves",u},{"v_curves",v},{"tolerance",1e-6}};}
Json different_parameter_network() {
  auto n=network(Json::array({line({0,0,0},{10,0,0}),{{"type","bezier"},{"points",{{0,5,0},{2,5,0},{10,5,0}}}},line({0,10,0},{10,10,0})}),Json::array({line({0,0,0},{0,10,0}),line({5,0,0},{5,10,0}),line({10,0,0},{10,10,0})}));
  n["u_parameters"]={0,.5,1};n["v_parameters"]={0,.5,1};return n;
}
void generalized_networks(const fs::path& root) {
  auto planar=model(Json::array({different_parameter_network()}),"network");BuiltModel plane_net(planar);near(plane_net.summary().at("area_mm2"),100,1e-6);near(plane_net.summary().at("center_of_mass_mm")[0],5,1e-6);
  const auto plane_shape=independent_step(plane_net,root/"different-parameters.step");GProp_GProps area;BRepGProp::SurfaceProperties(plane_shape,area,1e-10);near(area.Mass(),100,1e-6);
  auto reversed=planar;reversed["features"][0]["u_curves"][2]=line({10,10,0},{0,10,0});near(BuiltModel(reversed).summary().at("area_mm2"),100,1e-6);
  for(const auto* kind:{"arc","weighted","closed","collapsed","periodic"}) {
    const std::string name=kind;const bool closed=name=="closed"||name=="collapsed"||name=="periodic";
    Json profiles=Json::array(),guides=Json::array();
    for(int z:name=="weighted"?std::vector<int>{0,2,5}:std::vector<int>{0,5}) {
      if(name=="arc")profiles.push_back({{"type","arc"},{"start",{10,0,z}},{"mid",{std::sqrt(50),std::sqrt(50),z}},{"end",{0,10,z}}});
      else if(name=="weighted")profiles.push_back({{"type","bezier"},{"points",{{10,0,z},{10,10,z},{0,10,z}}},{"weights",{1,std::sqrt(.5),1}}});
      else if(name=="collapsed"&&z==0)profiles.push_back({{"type","point"},{"point",{0,0,0}}});
      else if(name=="periodic")profiles.push_back({{"type","spline"},{"points",{{10,0,z},{0,10,z},{-10,0,z},{0,-10,z}}},{"periodic",true}});
      else profiles.push_back({{"type","wire"},{"segments",circle_segments(10,z)}});
    }
    for(int i=0;i<(closed?4:3);++i){const auto angle=i*std::numbers::pi/(closed?2:4);const auto x=10*std::cos(angle),y=10*std::sin(angle);guides.push_back(line(name=="collapsed"?Json{0,0,0}:Json{x,y,0},Json{x,y,5}));}
    auto d=model(Json::array({network(profiles,guides)}),"network");BuiltModel built(d);const auto shape=independent_step(built,root/(name+"-network.step"));
    const auto face=TopoDS::Face(TopExp_Explorer(shape,TopAbs_FACE).Current());const auto surface=BRep_Tool::Surface(face);double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
    if(name=="closed"){auto explicit_stations=d;explicit_stations["features"][0]["u_parameters"]={0,.25,.5,.75,1};explicit_stations["features"][0]["v_parameters"]={0,1};near(BuiltModel(explicit_stations).summary().at("area_mm2"),100*std::numbers::pi,1e-4);explicit_stations["features"][0]["u_parameters"]={0,.25,.5,1};fails("invalid_model",[&]{BuiltModel bad(explicit_stations);});}
    if(closed)for(int j=1;j<8;++j){const auto v=v0+(v1-v0)*j/8.0;gp_Pnt a,b;gp_Vec au,av,bu,bv;surface->D1(u0,v,a,au,av);surface->D1(u1,v,b,bu,bv);require(a.Distance(b)<1e-7,"Periodic network seam remains closed after STEP");require(gp_Dir(au.Crossed(av)).Dot(gp_Dir(bu.Crossed(bv)))>1-1e-8,"Periodic network seam retains a continuous tangent plane after STEP");}
    if(name!="periodic") {
      const double expected=name=="collapsed"?10*std::numbers::pi*std::sqrt(125):(closed?100:25)*std::numbers::pi;
      near(built.summary().at("area_mm2"),expected,1e-4);
      for(int i=0;i<=32;++i)for(int j=0;j<=8;++j){const auto p=surface->Value(u0+(u1-u0)*i/32.0,v0+(v1-v0)*j/8.0);near(std::hypot(p.X(),p.Y()),name=="collapsed"?2*p.Z():10,2e-6);require(p.Z()>-1e-6&&p.Z()<5+1e-6,"Gordon analytic height bounds after STEP");}
      if(closed)for(int j=1;j<8;++j)require(surface->Value(u0,v0+(v1-v0)*j/8.0).Distance(surface->Value(u1,v0+(v1-v0)*j/8.0))<1e-7,"Closed profile seam remains geometrically closed after STEP");
      if(name=="collapsed")require(surface->Value((u0+u1)/2,v0).Distance(gp_Pnt(0,0,0))<1e-7,"Collapsed profile is the authored apex, not a tiny replacement edge");
    } else {
      BuiltModel reference(model(Json::array({{{"id","curve"},{"type","curve"},{"path",{{"type","wire"},{"segments",Json::array({profiles[0]})}}}}}),"curve"));
      const auto reference_shape=independent_step(reference,root/"periodic-source.step");GProp_GProps length;BRepGProp::LinearProperties(reference_shape,length);near(built.summary().at("area_mm2"),length.Mass()*5,1e-4);
      BRepAdaptor_Curve curve(TopoDS::Edge(TopExp_Explorer(reference_shape,TopAbs_EDGE).Current()));
      for(int i=0;i<=64;++i)for(double z:{0.0,2.5,5.0}){auto p=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*i/64.0);p.SetZ(z);GeomAPI_ProjectPointOnSurf project(p,surface);require(project.IsDone()&&project.NbPoints()>0&&project.LowerDistance()<2e-6,"Periodic network retains complete independently read source curves");}
    }
    near(BuiltModel(d,built.snapshot()).summary().at("area_mm2"),built.summary().at("area_mm2"),1e-6);
  }
  auto invalid=planar;invalid["features"][0]["u_curves"][1]={{"type","point"},{"point",{5,5,0}}};fails("invalid_model",[&]{BuiltModel bad(invalid);});
  invalid=planar;invalid["features"][0]["v_curves"][1]=line({5,0,1},{5,10,1});fails("invalid_model",[&]{BuiltModel bad(invalid);});
  // One curve crossing the same rail more than once is not an ordered network.
  invalid=planar;invalid["features"][0]["u_curves"][1]={{"type","bezier"},{"points",{{0,4,0},{20,4,0},{-10,6,0},{10,6,0}}}};fails("selection_ambiguous",[&]{BuiltModel bad(invalid);});
  // This second ambiguous grid agrees exactly at all explicit stations. The
  // three crossings of its middle rail must still prevent polynomial shortcutting.
  invalid["features"][0]["u_curves"][2]=line({0,8,0},{10,12,0});invalid["features"][0]["v_curves"][0]=line({0,0,0},{0,8,0});invalid["features"][0]["v_curves"][2]=line({10,0,0},{10,12,0});fails("selection_ambiguous",[&]{BuiltModel bad(invalid);});
  Service service(root/"network-worker");service.call("cad_create",{{"document_id","network"},{"model",planar}});const auto before=service.call("cad_read",{{"document_id","network"}});
  fails("invalid_model",[&]{service.call("cad_apply",{{"document_id","network"},{"expected_revision",1},{"operations",Json::array({{{"op","replace_feature"},{"id","network"},{"feature",network(Json::array({line({0,0,0},{10,0,0}),line({0,10,0},{10,10,0})}),Json::array({line({0,0,1},{0,10,1}),line({10,0,1},{10,10,1})}))}}})}});});require(service.call("cad_read",{{"document_id","network"}})==before,"Failed generalized Gordon worker edit preserves exact revision and source");
  auto changed=planar;changed["features"][0]["u_parameters"][1]=.4;require(feature_cache_keys(planar).at("network")!=feature_cache_keys(changed).at("network"),"Gordon station edit invalidates its cached geometry");near(BuiltModel(changed).summary().at("area_mm2"),100,1e-6);
  const auto dispatch=[&](const Json& request){for(int retry=0;;++retry){try{return service.call("cad_job",request);}catch(const Error& e){if(e.code!="workspace_busy"||retry>=1000)throw;}std::this_thread::sleep_for(std::chrono::milliseconds(5));}};
  dispatch({{"action","submit"},{"request_id","network-job"},{"tool","cad_query"},{"arguments",{{"document_id","network"},{"revision",1}}}});Json job;for(int i=0;i<1000;++i){job=dispatch({{"action","get"},{"job_id","network-job"}});if(job.at("state")=="succeeded"||job.at("state")=="failed")break;std::this_thread::sleep_for(std::chrono::milliseconds(5));}require(job.at("state")=="succeeded","Durable generalized Gordon query succeeds");near(job.at("result").at("summary").at("area_mm2"),100,1e-6);
  planar["features"].push_back({{"id","body"},{"type","thicken"},{"input","network"},{"thickness",1}});planar["output"]="body";service.call("cad_create",{{"document_id","library"},{"model",planar}});service.call("cad_create",{{"document_id","consumer"},{"model",model(Json::array({{{"id","seed"},{"type","box"},{"size",{1,1,1}}}}),"seed")}});
  const auto capture=service.call("cad_apply",{{"document_id","consumer"},{"expected_revision",1},{"operations",Json::array({{{"op","set_component"},{"id","module"},{"source_document_id","library"},{"source_revision",1}},{{"op","set_output"},{"feature_id","module"}}})}});near(capture.at("summary").at("volume_mm3"),100,1e-4);
  const auto saved=service.call("cad_read",{{"document_id","consumer"},{"revision",capture.at("revision")}});Service portable(root/"portable-general-network");near(portable.call("cad_create",{{"document_id","copy"},{"model",saved.at("model")}}).at("summary").at("volume_mm3"),100,1e-4);
}
Json sphere_features() {
  return Json::array({{{"id","profile"},{"type","sketch"},{"workplane",{{"origin",{0,0,0}},{"normal",{0,0,1}},{"x_direction",{1,0,0}}}},{"profile",{{"type","wire"},{"segments",Json::array({{{"type","arc"},{"start",{0,-10}},{"mid",{10,0}},{"end",{0,10}}},line({0,10},{0,-10})})}}}},{{"id","target"},{"type","revolve"},{"input","profile"},{"axis",{{"origin",{0,0,0}},{"direction",{0,1,0}}}},{"angle_deg",360}}});
}
void projection_branches(const fs::path& root) {
  for(const auto* kind:{"cylinder","sphere"})for(const auto* branch:{"nearest","farthest"}) {
    const bool sphere=std::string(kind)=="sphere",near_branch=std::string(branch)=="nearest";Json features=sphere?sphere_features():Json::array({{{"id","target"},{"type","cylinder"},{"radius",10},{"height",5}}});
    const auto outer=sphere?circle_segments(3,0,false):polygon({{0,0},{6,0},{6,3},{0,3}}),hole=sphere?circle_segments(1,0,false):polygon({{2,1},{4,1},{4,2},{2,2}});
    features.push_back({{"id","path"},{"type","sketch"},{"workplane",{{"origin",sphere?Json{15,0,0}:Json{15,2,1}},{"normal",{1,0,0}},{"x_direction",{0,1,0}}}},{"profile",{{"type","wire"},{"segments",outer},{"holes",Json::array({hole})}}}});
    features.push_back({{"id","projected"},{"type","surface_project"},{"input","path"},{"target","target"},{"faces",{{"type","geometric"},{"feature_id","target"},{"surface_kind",kind},{"expected_count",1}}},{"direction",{-1,0,0}},{"branch",branch}});
    auto d=model(features,"projected");BuiltModel region(d);const double expected=sphere?20*std::numbers::pi*(std::sqrt(99)-std::sqrt(91)):30*(std::asin(.8)-std::asin(.2))-10*(std::asin(.6)-std::asin(.4));near(region.summary().at("area_mm2"),expected,1e-4);
    require(region.summary().at("center_of_mass_mm")[0].get<double>()*(near_branch?1:-1)>5,"Projected region lies on the requested forward branch");
    const auto shape=independent_step(region,root/(std::string(kind)+"-"+branch+"-hole.step"));GProp_GProps area;BRepGProp::SurfaceProperties(shape,area,1e-10);near(area.Mass(),expected,1e-4);near(BuiltModel(d,region.snapshot()).summary().at("area_mm2"),expected,1e-4);
    if(sphere){const auto step_face=TopoDS::Face(TopExp_Explorer(shape,TopAbs_FACE).Current());const auto support=BRep_Tool::Surface(step_face);const gp_Pnt p((near_branch?1:-1)*std::sqrt(96),2,0);GeomAPI_ProjectPointOnSurf projected(p,support);require(projected.IsDone()&&projected.LowerDistance()<1e-6,"Sphere projection retains its analytic support after STEP");double u,v;projected.LowerDistanceParameters(u,v);GeomLProp_SLProps geometry(support,u,v,1,1e-9);auto normal=geometry.Normal();if(step_face.Orientation()==TopAbs_REVERSED)normal.Reverse();require(normal.Dot(gp_Dir(p.X(),p.Y(),p.Z()))>1-1e-8,"Both projected spherical branches retain outward target orientation after STEP");}
    auto unique=d;unique["features"].back().erase("branch");fails("selection_ambiguous",[&]{BuiltModel bad(unique);});
    unique["features"].back()["branch"]="unique";fails("selection_ambiguous",[&]{BuiltModel bad(unique);});
    auto open=d;open["features"][open["features"].size()-2]={{"id","path"},{"type","curve"},{"path",{{"type","wire"},{"segments",Json::array({line(sphere?Json{15,2,0}:Json{15,2,2},sphere?Json{15,8,0}:Json{15,8,2})})}}}};open["features"].back()["type"]="curve_project";BuiltModel projected(open);near(projected.curve_samples(Json::object()).at("length_mm"),10*(std::asin(.8)-std::asin(.2)),1e-4);independent_step(projected,root/(std::string(kind)+"-"+branch+"-curve.step"));
    auto tangent=open;tangent["features"][tangent["features"].size()-2]["path"]["segments"]=Json::array({line(sphere?Json{15,10,0}:Json{15,10,1},sphere?Json{20,10,0}:Json{15,10,4})});fails("selection_ambiguous",[&]{BuiltModel bad(tangent);});
    auto discontinuous=open;discontinuous["features"][discontinuous["features"].size()-2]["path"]["segments"]=Json::array({line({15,2,2},{15,11,2})});fails("selection_missing",[&]{BuiltModel bad(discontinuous);});
    if(near_branch){auto switching=open;switching["features"][switching["features"].size()-2]["path"]["segments"]=Json::array({line({15,2,2},{5,8,2})});fails("selection_missing",[&]{BuiltModel bad(switching);});}
    const auto keys=feature_cache_keys(d);d["features"].back()["branch"]=near_branch?"farthest":"nearest";require(keys.at("projected")!=feature_cache_keys(d).at("projected"),"Projection branch changes invalidate the dependent geometry");
  }
}

void reversed_projection_orientation(const fs::path& root) {
  auto d=model(Json::array({{{"id","target"},{"type","box"},{"size",{10,10,10}}},{{"id","path"},{"type","sketch"},{"workplane",{{"origin",{1,2,-5}},{"normal",{0,0,1}},{"x_direction",{1,0,0}}}},{"profile",{{"type","rectangle"},{"width",2},{"height",3}}}},{{"id","projected"},{"type","surface_project"},{"input","path"},{"target","target"},{"faces",{{"type","geometric"},{"feature_id","target"},{"surface_kind","plane"},{"expected_count",1},{"normal",{{"vector",{0,0,-1}},{"tolerance",1e-6}}}}},{"direction",{0,0,1}}}}),"projected");
  BuiltModel projected(d);near(projected.topology().at("faces")[0].at("normal")[2],-1,1e-8);near(projected.summary().at("area_mm2"),6,1e-8);
  for(int sign:{-1,1}){auto material=d;material["features"].push_back({{"id","body"},{"type","thicken"},{"input","projected"},{"thickness",sign}});material["output"]="body";BuiltModel built(material);const auto summary=built.summary();near(summary.at("volume_mm3"),6,1e-8);near(summary.at("bounds_mm").at("min")[2],sign>0?-1:0,1e-8);near(summary.at("bounds_mm").at("max")[2],sign>0?0:1,1e-8);near(summary.at("center_of_mass_mm")[2],-.5*sign,1e-8);const auto shape=independent_step(built,root/("reversed-projection-"+std::to_string(sign)+".step"));GProp_GProps mass;BRepGProp::VolumeProperties(shape,mass,1e-9);near(mass.Mass(),6,1e-8);near(mass.CentreOfMass().Z(),-.5*sign,1e-8);}
}
void rollback(const fs::path& root){auto d=model(Json::array({plane()}),"patch");Service service(root/"rollback");service.call("cad_create",{{"document_id","surface"},{"model",d}});const auto before=service.call("cad_read",{{"document_id","surface"}});const auto bad=Json{{"id","trimmed"},{"type","surface_trim"},{"input","patch"},{"boundary",polygon({{-.1,.1},{.9,.1},{.9,.9},{-.1,.9}})}};fails("invalid_model",[&]{service.call("cad_apply",{{"document_id","surface"},{"expected_revision",1},{"operations",Json::array({{{"op","add_feature"},{"feature",bad}},{{"op","set_output"},{"feature_id","trimmed"}}})}});});require(service.call("cad_read",{{"document_id","surface"}})==before,"Failed worker surface edit preserves committed revision and bytes");}
}
int main(){try{configure_kernel_logging();set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));Temp temp;crown(temp.path);mixed_volume();polygon_volume();imports(temp.path);trims(temp.path);filling(temp.path);filling_support_intent(temp.path);nonplanar_continuity(temp.path);nonplanar_continuity(temp.path,1e-6);component_capture(temp.path);networks(temp.path);projections(temp.path);generalized_networks(temp.path);projection_branches(temp.path);reversed_projection_orientation(temp.path);rollback(temp.path);std::cout<<checks<<" surface parity checks passed\n";return 0;}catch(const Error& e){std::cerr<<e.code<<": "<<e.what()<<" "<<e.details.dump()<<"\n";return 1;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
