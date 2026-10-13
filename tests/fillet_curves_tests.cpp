#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include "agentcad/cache.hpp"
#include "agentcad/jobs.hpp"
#include <STEPControl_Reader.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomLProp_SLProps.hxx>
#include <TopExp.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_List.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <functional>
#include <iostream>
#include <numbers>
#include <chrono>
#include <thread>
using namespace agentcad;
namespace {
int checks=0;
void require(bool ok,const std::string& message){++checks;if(!ok)throw std::runtime_error(message);}
void near(double actual,double expected,double tolerance=1e-6){require(std::abs(actual-expected)<tolerance,"Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
struct Temp {fs::path path=temporary_directory(fs::canonical(fs::temp_directory_path()));~Temp(){std::error_code ignored;fs::remove_all(path,ignored);}};
Json model(Json features,const std::string& output="part"){return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",features},{"output",output}};}
Json selector(){return {{"type","geometric"},{"feature_id","base"},{"curve_kind","line"},{"expected_count",1},{"center",{{"point",{20,30,20}},{"tolerance",1e-6}}}};}
Json fillet(){return model(Json::array({{{"id","base"},{"type","box"},{"size",{20,30,40}}},
  {{"id","part"},{"type","fillet"},{"input","base"},{"edges",selector()},{"radius",2},{"radius_end",4},{"start",{{"point",{20,30,0}},{"tolerance",1e-6}}}}}));}
Error fails(const std::function<void()>& action,const std::string& feature="part"){try{action();}catch(const Error& e){require(e.details.value("feature_id",std::string())==feature,"Feature-qualified failure: "+e.json().dump());return e;}throw std::runtime_error("Expected failure");}
TopoDS_Shape read_step(const BuiltModel& built,const fs::path& path){built.export_file(path,"step");STEPControl_Reader reader;require(reader.ReadFile(path_to_utf8(path).c_str())==IFSelect_RetDone&&reader.TransferRoots()>0,"Independent STEP import succeeds");auto shape=reader.OneShape();require(BRepCheck_Analyzer(shape).IsValid(),"STEP shape is valid");return shape;}
double mass(const TopoDS_Shape& shape){GProp_GProps result;BRepGProp::VolumeProperties(shape,result);return result.Mass();}
void variable_fillet(const fs::path& root){
  for(bool reverse:{false,true}) {
    auto intent=fillet();if(reverse)intent["features"].back()["start"]["point"]={20,30,40};
    BuiltModel built(intent);const auto shape=read_step(built,root/(reverse?"reverse.step":"variable.step"));
    require(mass(shape)>23000&&mass(shape)<24000,"Variable fillet removes bounded material");
    // Endpoint radii and the symmetric midpoint are the public contract;
    // OCCT smoothly interpolates its radius law, rather than using a linear
    // cross-section radius at every intermediate Z coordinate.
    for(double z:{0.,20.,40.}) {
      const double radius=reverse?4-z/20:2+z/20;
      for(const auto& p:std::vector<gp_Pnt>{{20-radius,30,z},{20,30-radius,z}}) {
        double distance=1e9;
        for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
          const auto face=TopoDS::Face(it.Current());if(BRepAdaptor_Surface(face).GetType()==GeomAbs_Plane)continue;
          BRepExtrema_DistShapeShape probe(BRepBuilderAPI_MakeVertex(p).Vertex(),face);
          require(probe.IsDone(),"Blend boundary distance evaluated");distance=std::min(distance,probe.Value());
        }
        near(distance,0,2e-5);
      }
    }
    near(BuiltModel(intent,built.snapshot()).summary().at("volume_mm3"),built.summary().at("volume_mm3"),1e-6);
  }
  for(int variant=0;variant<5;++variant){auto bad=fillet();auto& feature=bad["features"].back();
    if(variant==0)feature.erase("start");if(variant==1)feature["radius_end"]=-1;if(variant==2)feature["edges"]="all";
    if(variant==3)feature["start"]["point"]={1,2,3};if(variant==4)feature["start"]["tolerance"]=100;
    fails([&]{BuiltModel rejected(bad);});
  }
}
Json helix(bool left=false){
  const double radius=6,pitch=4,speed=std::hypot(2*std::numbers::pi*radius,pitch);
  return model(Json::array({
    {{"id","spine"},{"type","curve_helix"},{"frame",{{"origin",{0,0,0}},{"normal",{0,0,1}},{"x_direction",{1,0,0}}}},{"radius",radius},{"pitch",pitch},{"turns",1.25},{"handedness",left?"left":"right"}},
    {{"id","profile"},{"type","sketch"},{"workplane",{{"origin",{radius,0,0}},{"normal",{0,(left?-1:1)*2*std::numbers::pi*radius/speed,pitch/speed}},{"x_direction",{1,0,0}}}},{"profile",{{"type","circle"},{"radius",.5}}}},
    {{"id","part"},{"type","sweep"},{"input","profile"},{"path","spine"},{"orientation","frenet"}}
  }));
}
void saved_sweeps(const fs::path& root){
  for(bool left:{false,true}) {
    const auto intent=helix(left);BuiltModel built(intent);const auto expected=std::numbers::pi*.25*std::hypot(12*std::numbers::pi,4)*1.25;
    near(built.summary().at("volume_mm3"),expected,1e-3);
    near(mass(read_step(built,root/(left?"left.step":"right.step"))),expected,1e-3);
    const auto deps=built.topology().at("provenance").at("dependencies");require(std::find(deps.begin(),deps.end(),"spine")!=deps.end(),"Saved sweep reports path dependency");
    near(BuiltModel(intent,built.snapshot()).summary().at("volume_mm3"),expected,1e-3);
  }
  auto straight=helix();straight["features"][0]={{"id","spine"},{"type","curve"},{"path",{{"type","wire"},{"segments",Json::array({{{"type","line"},{"start",{0,0,0}},{"end",{0,0,10}}}})}}}};
  straight["features"][1]["workplane"]={{"origin",{0,0,0}},{"normal",{0,0,1}},{"x_direction",{1,0,0}}};
  near(BuiltModel(straight).summary().at("volume_mm3"),2.5*std::numbers::pi);
  auto trimmed=straight;trimmed["features"].insert(trimmed["features"].begin()+1,Json{{"id","trim"},{"type","curve_trim"},{"input","spine"},{"start",.2},{"end",.8}});
  trimmed["features"][2]["workplane"]["origin"]={0,0,2};trimmed["features"].back()["path"]="trim";
  near(BuiltModel(trimmed).summary().at("volume_mm3"),1.5*std::numbers::pi);
  auto changing=straight;auto end=changing["features"][1];end["id"]="end";end["workplane"]["origin"]={0,0,10};end["profile"]["radius"]=1;
  changing["features"].insert(changing["features"].begin()+2,end);changing["features"].back().erase("input");changing["features"].back()["sections"]={"profile","end"};
  BuiltModel varied(changing);near(varied.summary().at("volume_mm3"),10*std::numbers::pi*(.25+.5+1)/3,1e-5);
  require(varied.topology().at("provenance").at("dependencies")==Json({"spine","profile","end"}),"Varying sweep keeps path and section dependencies");
  const auto before=feature_cache_keys(straight);straight["features"][0]["path"]["segments"][0]["end"]={0,0,12};require(before.at("part")!=feature_cache_keys(straight).at("part"),"Curve edit invalidates sweep cache");
  for(const auto& name:{"missing","part","profile"}) {auto bad=helix();bad["features"].back()["path"]=name;fails([&]{validate_model(bad);});}
}
void curvature_junctions(const TopoDS_Shape& shape) {
  NCollection_IndexedDataMap<TopoDS_Shape,NCollection_List<TopoDS_Shape>,TopTools_ShapeMapHasher> adjacency;
  TopExp::MapShapesAndUniqueAncestors(shape,TopAbs_EDGE,TopAbs_FACE,adjacency);int joins=0;
  for(int edge=1;edge<=adjacency.Extent();++edge) {
    const auto& faces=adjacency.FindFromIndex(edge);if(faces.Size()!=2)continue;
    const auto a=TopoDS::Face(faces.First()),b=TopoDS::Face(faces.Last());
    if(BRepAdaptor_Surface(a).GetType()!=GeomAbs_BSplineSurface&&BRepAdaptor_Surface(b).GetType()!=GeomAbs_BSplineSurface)continue;
    BRepAdaptor_Curve curve(TopoDS::Edge(adjacency.FindKey(edge)));bool tangent=false;
    for(int i=0;i<13;++i) {
      const auto point=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*(i+.5)/13);
      const auto sa=BRep_Tool::Surface(a),sb=BRep_Tool::Surface(b);GeomAPI_ProjectPointOnSurf pa(point,sa),pb(point,sb);
      require(pa.IsDone()&&pb.IsDone()&&pa.NbPoints()>0&&pb.NbPoints()>0,"STEP junction projects to both surfaces");
      double au,av,bu,bv;pa.LowerDistanceParameters(au,av);pb.LowerDistanceParameters(bu,bv);
      GeomLProp_SLProps ap(sa,au,av,2,1e-10),bp(sb,bu,bv,2,1e-10);
      require(ap.IsNormalDefined()&&bp.IsNormalDefined(),"STEP junction normals exist");
      const double dot=ap.Normal().Dot(bp.Normal());if(i==0){tangent=std::abs(dot)>1-1e-7;if(!tangent)break;++joins;}
      require(std::acos(std::clamp(std::abs(dot),0.,1.))<2e-5,"G2 seam has matching normals");
      require(ap.IsCurvatureDefined()&&bp.IsCurvatureDefined(),"STEP junction curvature exists");
      // Compare directional normal curvature in a shared world tangent frame;
      // this independently detects rotated principal directions as well as
      // principal magnitude errors.
      auto axis=gp_Vec(1,0,0);if(std::abs(ap.Normal().X())>.8)axis=gp_Vec(0,1,0);
      auto t=gp_Vec(ap.Normal()).Crossed(axis).Normalized(),s=gp_Vec(ap.Normal()).Crossed(t);
      const auto curvature=[](GeomLProp_SLProps& p,const gp_Vec& direction) {
        if(p.IsUmbilic())return (p.MinCurvature()+p.MaxCurvature())/2;
        gp_Dir high,low;p.CurvatureDirections(high,low);
        return p.MaxCurvature()*std::pow(direction.Dot(gp_Vec(high)),2)+p.MinCurvature()*std::pow(direction.Dot(gp_Vec(low)),2);
      };
      for(const auto& direction:std::vector<gp_Vec>{t,s,(t+s).Normalized()})near(curvature(ap,direction),curvature(bp,direction)*(dot<0?-1:1),2e-5);
    }
  }
  require(joins>=2,"At least two independently verified G2 support junctions");
}
void g2_tests(const fs::path& root) {
  for(int fixture=0;fixture<6;++fixture) {
    auto intent=fillet();auto& feature=intent["features"].back();feature["continuity"]="G2";
    if(fixture!=1){feature.erase("radius_end");feature.erase("start");}
    if(fixture==2){intent["features"][0]={{"id","base"},{"type","cylinder"},{"radius",10},{"height",20}};feature["edges"]={{"type","geometric"},{"feature_id","base"},{"curve_kind","circle"},{"expected_count",1},{"center",{{"point",{0,0,20}},{"tolerance",1e-6}}}};}
    if(fixture==3) {
      auto rounding=feature;rounding["radius"]=1;rounding["edges"]={{"type","geometric"},{"feature_id","base"},{"curve_kind","circle"},{"expected_count",1},{"center",{{"point",{0,0,20}},{"tolerance",1e-6}}},{"radius",{{"value",4},{"tolerance",1e-6}}}};
      intent["features"]=Json::array({{{"id","stock"},{"type","cylinder"},{"radius",10},{"height",20}},{{"id","bore"},{"type","cylinder"},{"radius",4},{"height",20}},{{"id","base"},{"type","cut"},{"left","stock"},{"right","bore"}},rounding});
    }
    if(fixture==4||fixture==5) {
      auto rounding=feature;intent["features"][0]["id"]="stock";
      intent["features"].insert(intent["features"].begin()+1,Json{{"id","other"},{"type","box"},{"size",{5,5,5}},{"origin",fixture==4?Json({5,5,5}):Json({50,50,50})}});
      intent["features"].insert(intent["features"].begin()+2,Json{{"id","base"},{"type",fixture==4?"cut":"fuse"},{"left","stock"},{"right","other"}});
      intent["features"].back()=rounding;
    }
    BuiltModel built(intent);const auto exported=read_step(built,root/("g2-"+std::to_string(fixture)+".step"));curvature_junctions(exported);
    const auto fresh=built.summary();require(mass(exported)>0,"G2 result has positive exact material volume");
    near(fresh.at("volume_mm3"),mass(exported),1e-3);
    near(BuiltModel(intent,built.snapshot()).summary().at("volume_mm3"),fresh.at("volume_mm3"),1e-5);
    auto tangent=intent;tangent["features"].back()["continuity"]="G1";
    require(std::abs(BuiltModel(tangent).summary().at("volume_mm3").get<double>()-fresh.at("volume_mm3").get<double>())>.1,"G2 does not substitute a conventional G1 fillet");
    int periodic=0;
    for(TopExp_Explorer it(exported,TopAbs_FACE);it.More();it.Next()) {
      const auto surface=BRep_Tool::Surface(TopoDS::Face(it.Current()));
      if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()!=GeomAbs_BSplineSurface||!surface->IsUPeriodic())continue;
      ++periodic;double u0,u1,v0,v1;surface->Bounds(u0,u1,v0,v1);
      for(double v:{v0,(v0+v1)/2,v1}) {
        gp_Pnt p,q;gp_Vec pu,pv,puu,pvv,puv,qu,qv,quu,qvv,quv;
        surface->D2(u0,v,p,pu,pv,puu,pvv,puv);surface->D2(u1,v,q,qu,qv,quu,qvv,quv);
        near(p.Distance(q),0,1e-7);
        for(const auto& delta:std::vector<gp_Vec>{pu-qu,pv-qv,puu-quu,pvv-qvv,puv-quv})near(delta.Magnitude(),0,1e-6);
      }
    }
    if(fixture==2||fixture==3)require(periodic>0,"Closed G2 rim has a verified C2 periodic seam");
    if(fixture==4||fixture==5) {
      auto original=fillet();original["features"].back().erase("radius_end");original["features"].back().erase("start");original["features"].back()["continuity"]="G2";
      near(fresh.at("volume_mm3"),BuiltModel(original).summary().at("volume_mm3").get<double>()+(fixture==4?-125:125),1e-4);
      require(fresh.at("solid_count")==Json(fixture==4?1:2),"G2 preserves cavity ownership and disconnected solids");
    }
    require(!built.topology().at("provenance").at("history").empty(),"G2 retains feature history");
  }
  // A single selected edge seeds a contour through both collinear source
  // edges. Endpoint radii and G2 apply to the whole propagated contour.
  const auto contour=model(Json::array({
    {{"id","profile"},{"type","sketch"},{"workplane",{{"origin",{0,0,0}},{"normal",{0,0,1}},{"x_direction",{1,0,0}}}},{"profile",{{"type","polygon"},{"points",{{0,0},{10,0},{20,0},{20,30},{0,30}}}}}},
    {{"id","base"},{"type","extrude"},{"input","profile"},{"distance",40}},
    {{"id","part"},{"type","fillet"},{"input","base"},{"radius",2},{"radius_end",4},{"continuity","G2"},{"start",{{"point",{0,0,40}},{"tolerance",1e-6}}},
     {"edges",{{"type","geometric"},{"feature_id","base"},{"curve_kind","line"},{"expected_count",1},{"center",{{"point",{5,0,40}},{"tolerance",1e-6}}}}}}
  }));
  BuiltModel chained(contour);const auto exported=read_step(chained,root/"g2-contour.step");curvature_junctions(exported);
  for(double x:{0.,10.,20.})for(const auto& point:std::vector<gp_Pnt>{{x,2+x/10,40},{x,0,38-x/10}}) {
    double distance=1e9;for(TopExp_Explorer it(exported,TopAbs_FACE);it.More();it.Next()) {
      const auto face=TopoDS::Face(it.Current());if(BRepAdaptor_Surface(face).GetType()!=GeomAbs_BSplineSurface)continue;
      BRepExtrema_DistShapeShape probe(BRepBuilderAPI_MakeVertex(point).Vertex(),face);require(probe.IsDone(),"Propagated contour contact evaluated");distance=std::min(distance,probe.Value());
    }
    near(distance,0,2e-5);
  }
}
void lifecycle_tests(const fs::path& root) {
  Service service(root/"lifecycle");auto source=fillet();source["parameters"]["end_radius"]=4;source["features"].back()["radius_end"]={{"parameter","end_radius"}};
  service.call("cad_create",{{"document_id","rounding"},{"model",source}});
  const auto before=service.call("cad_read",{{"document_id","rounding"}});
  const auto edit=[&](double value,int revision){return service.call("cad_apply",{{"document_id","rounding"},{"expected_revision",revision},{"operations",Json::array({{{"op","set_parameter"},{"name","end_radius"},{"value",value}}})}});};
  edit(5,1);const auto saved=service.call("cad_read",{{"document_id","rounding"}});require(saved.at("revision")==2,"Variable radius edit commits");
  fails([&]{edit(-1,2);});require(service.call("cad_read",{{"document_id","rounding"}})==saved,"Invalid radius preserves full source and HEAD");
  require(service.call("cad_read",{{"document_id","rounding"},{"revision",1}})==before,"Earlier fillet intent is preserved");
  auto g2=source;g2["features"].back()["continuity"]="G2";service.call("cad_create",{{"document_id","smooth"},{"model",g2}});
  const auto smooth=service.call("cad_read",{{"document_id","smooth"}});
  auto unsupported=g2["features"].back();unsupported.erase("radius_end");unsupported.erase("start");unsupported["edges"]="all";
  fails([&]{service.call("cad_apply",{{"document_id","smooth"},{"expected_revision",1},{"operations",Json::array({{{"op","replace_feature"},{"id","part"},{"feature",unsupported}}})}});});
  require(service.call("cad_read",{{"document_id","smooth"}})==smooth,"Unsupported G2 corner topology never commits a G1 substitute");
  auto spring=helix();spring["parameters"]["turns"]=1.25;spring["features"][0]["turns"]={{"parameter","turns"}};
  const auto created=service.call("cad_create",{{"document_id","spring"},{"model",spring}});
  const auto changed=service.call("cad_apply",{{"document_id","spring"},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","turns"},{"value",2.5}}})}});
  near(changed.at("summary").at("volume_mm3"),created.at("summary").at("volume_mm3").get<double>()*2,1e-3);
  fs::remove_all(root/"lifecycle"/".cache");Service reopened(root/"lifecycle");
  near(reopened.call("cad_query",{{"document_id","spring"},{"revision",2}}).at("summary").at("volume_mm3"),changed.at("summary").at("volume_mm3"),1e-3);
  const auto seed=model(Json::array({{{"id","part"},{"type","box"},{"size",{1,1,1}}}}));service.call("cad_create",{{"document_id","consumer"},{"model",seed}});
  const auto imported=service.call("cad_apply",{{"document_id","consumer"},{"expected_revision",1},{"operations",Json::array({{{"op","set_component"},{"id","spring_copy"},{"source_document_id","spring"},{"source_revision",2}},{{"op","set_output"},{"feature_id","spring_copy"}}})}});
  near(imported.at("summary").at("volume_mm3"),changed.at("summary").at("volume_mm3"),1e-3);
  const auto portable=service.call("cad_read",{{"document_id","consumer"}}).at("model");require(portable.at("components")[0].at("feature_map").size()==3,"Pinned sweep component captures profile and saved curve");
  Service elsewhere(root/"portable");near(elsewhere.call("cad_create",{{"document_id","copy"},{"model",portable}}).at("summary").at("volume_mm3"),changed.at("summary").at("volume_mm3"),1e-3);
  const auto dispatch=[&](const Json& request){for(int retry=0;;++retry){try{return service.call("cad_job",request);}catch(const Error& e){if(e.code!="workspace_busy"||retry>=1000)throw;}std::this_thread::sleep_for(std::chrono::milliseconds(5));}};
  dispatch({{"action","submit"},{"request_id","g2-create"},{"tool","cad_create"},{"arguments",{{"document_id","async_smooth"},{"model",g2}}}});
  Json job;for(int n=0;n<1000;++n){job=dispatch({{"action","get"},{"job_id","g2-create"}});if(job.at("state")=="succeeded"||job.at("state")=="failed")break;std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  require(job.at("state")=="succeeded","G2 builds through durable isolated jobs: "+job.dump());
}
}
int main(){try{configure_kernel_logging();set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));Temp temp;variable_fillet(temp.path);saved_sweeps(temp.path);g2_tests(temp.path);lifecycle_tests(temp.path);std::cout<<checks<<" fillet/curve checks passed\n";return 0;}catch(const Error& e){std::cerr<<e.code<<": "<<e.what()<<" "<<e.details.dump()<<"\n";return 1;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
