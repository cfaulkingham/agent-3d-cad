#include "agentcad/kernel.hpp"
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <HLRBRep_Intersector.hxx>
#include <HLRBRep_Surface.hxx>
#include <HLRAlgo_Projector.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pln.hxx>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <sstream>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
void near(double actual,double expected,double tolerance=1e-7) {
  require(std::abs(actual-expected)<=tolerance,"Expected "+std::to_string(expected)+", got "+std::to_string(actual));
}
void equivalent(const Json& a,const Json& b) {
  if (a.is_number() && b.is_number()) {
    near(a.get<double>(),b.get<double>(),1e-7*std::max(1.0,std::abs(a.get<double>())));
    return;
  }
  require(a.type()==b.type(),"Equivalent JSON types");
  if (a.is_object()) {
    require(a.size()==b.size(),"Equivalent object keys");
    for (const auto& [key,value]:a.items()) equivalent(value,b.at(key));
  } else if (a.is_array()) {
    require(a.size()==b.size(),"Equivalent array lengths");
    for (std::size_t i=0;i<a.size();++i) equivalent(a[i],b[i]);
  } else require(a==b,"Equivalent JSON values");
}
Json drawing(std::initializer_list<const char*> orientations) {
  Json views=Json::array();
  for (const auto* orientation:orientations) views.push_back({{"id",orientation},{"orientation",orientation}});
  return {{"views",views},{"hidden_lines",true}};
}
Json thread(double pitch,double length,const std::string& hand="right") {
  return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},
    {"features",Json::array({{{"id","thread"},{"type","external_thread"},
      {"major_diameter",12},{"pitch",pitch},{"length",length},{"handedness",hand}}})},{"output","thread"}};
}
double segment_distance(double x,double y,const Json& a,const Json& b) {
  const double ax=a[0],ay=a[1],dx=b[0].get<double>()-ax,dy=b[1].get<double>()-ay;
  const double squared=dx*dx+dy*dy;
  const double t=squared==0 ? 0 : std::clamp(((x-ax)*dx+(y-ay)*dy)/squared,0.0,1.0);
  return std::hypot(x-ax-t*dx,y-ay-t*dy);
}
double curve_distance(const Json& view,bool hidden,double x,double y) {
  double nearest=std::numeric_limits<double>::infinity();
  for (const auto& entity:view.at("entities")) {
    if (entity.at("hidden")!=hidden) continue;
    if (entity.contains("points")) {
      const auto& points=entity.at("points");
      for (std::size_t i=1;i<points.size();++i)
        nearest=std::min(nearest,segment_distance(x,y,points[i-1],points[i]));
      continue;
    }
    const double cx=entity.at("center")[0],cy=entity.at("center")[1],radius=entity.at("radius");
    double distance=std::abs(std::hypot(x-cx,y-cy)-radius);
    if (entity.at("kind")=="arc") {
      const double start=entity.at("start_deg").get<double>()*std::numbers::pi/180;
      const double end=entity.at("end_deg").get<double>()*std::numbers::pi/180;
      double angle=std::atan2(y-cy,x-cx);
      while (angle<start) angle+=2*std::numbers::pi;
      if (angle>end) distance=std::min(std::hypot(x-cx-radius*std::cos(start),y-cy-radius*std::sin(start)),
                                      std::hypot(x-cx-radius*std::cos(end),y-cy-radius*std::sin(end)));
    }
    nearest=std::min(nearest,distance);
  }
  return nearest;
}
void exact_occlusion_regression(const BuiltModel& built) {
  // Inspect the exact snapshot only within this kernel/SDK regression. Testing
  // raw 3D HLR curves catches a visibility mistake smaller than the renderer's
  // chord tolerance; sampled 2D polylines would conceal this regression.
  std::istringstream input(built.snapshot().at("features").at("thread").at("brep").get<std::string>());
  BRep_Builder builder; TopoDS_Shape shape;
  BRepTools::Read(shape,input,builder);
  require(!shape.IsNull(),"Read exact thread snapshot for visibility regression");
  occ::handle<HLRBRep_Algo> algorithm=new HLRBRep_Algo;
  algorithm->Add(shape,0);
  algorithm->Projector(HLRAlgo_Projector(gp_Ax2(gp_Pnt(),gp_Dir(1,-1,1),gp_Dir(1,1,0))));
  algorithm->Update(); algorithm->Hide();
  HLRBRep_HLRToShape extraction(algorithm);
  const auto point=BRepBuilderAPI_MakeVertex(gp_Pnt(-4.3218075645529366,-4.1619680763436513,0.69937723302485655)).Vertex();
  for (bool visible:{false,true}) {
    TopoDS_Compound curves; builder.MakeCompound(curves);
    for (const auto kind:{HLRBRep_Sharp,HLRBRep_Rg1Line,HLRBRep_OutLine}) {
      const auto group=extraction.CompoundOfEdges(kind,visible,true);
      if (!group.IsNull()) builder.Add(curves,group);
    }
    BRepExtrema_DistShapeShape distance(point,curves);
    require(distance.IsDone(),"Exact HLR curve distance succeeds");
    if (visible) require(distance.Value()>1e-5,"A physically occluded thread-root point is absent from exact visible curves");
    else require(distance.Value()<1e-5,"A physically occluded thread-root point remains in exact hidden curves");
  }
}
void streaming_root_regression(const BuiltModel& built) {
#if defined(AGENTCAD_OCCT_HLR_STREAMING)
  std::istringstream input(built.snapshot().at("features").at("thread").at("brep").get<std::string>());
  BRep_Builder builder;
  TopoDS_Shape shape;
  BRepTools::Read(shape,input,builder);
  require(!shape.IsNull(),"Read exact thread for streaming root checks");
  HLRAlgo_Projector projector(gp_Ax2(gp_Pnt(),gp_Dir(0,0,1),gp_Dir(1,0,0)));
  HLRBRep_Surface surface;
  surface.Projector(&projector);
  HLRBRep_Intersector intersector;
  bool multiple_roots=false,late_acceptance=false;
  const auto point_values=[](const IntCurveSurface_IntersectionPoint& point) {
    gp_Pnt p; double u,v,w; IntCurveSurface_TransitionOnCurve transition;
    point.Values(p,u,v,w,transition);
    return Json::array({p.X(),p.Y(),p.Z(),u,v,w,static_cast<int>(transition)});
  };
  const auto inventory=[&](HLRBRep_Intersector& query,const gp_Lin& ray,bool dense) {
    query.Perform(ray,100,dense);
    require(query.IsDone(),"Complete exact root query succeeds");
    Json points=Json::array();
    for(int i=1;i<=query.NbPoints();++i) points.push_back(point_values(query.CSPoint(i)));
    return points;
  };
  const auto verify=[&](const gp_Lin& ray,bool dense) {
    const auto complete=inventory(intersector,ray,dense);
    HLRBRep_Intersector fresh;
    fresh.Load(&surface);
    // Independently rebuild the requested grid: comparing two queries on the
    // same cached grid alone could miss stale face/density state.
    equivalent(complete,inventory(fresh,ray,dense));
    Json observed=Json::array();
    const bool rejected=intersector.PerformUntil(ray,100,dense,[&](const auto& point) {
      observed.push_back(point_values(point));
      return false;
    });
    require(!rejected,"Rejecting every exact candidate reports no occlusion");
    equivalent(complete,observed);
    int calls=0;
    const bool accepted=intersector.PerformUntil(ray,100,dense,[&](const auto& point) {
      ++calls;
      const auto values=point_values(point);
      equivalent(complete.at(0),values);
      const auto exact=surface.Value(values[3],values[4]);
      require(exact.Distance(gp_Pnt(values[0],values[1],values[2]))<1e-5,"Accepted candidate lies on exact source surface");
      return true;
    });
    require(accepted==!complete.empty(),"Streaming and complete inventories agree on existence");
    require(calls==(complete.empty()?0:1),"Stop after the first qualified root");
    if(complete.size()>1 && complete.back()[5].get<double>()-complete.front()[5].get<double>()>1e-6) {
      multiple_roots=true;
      calls=0;
      const double threshold=(complete.front()[5].get<double>()+complete.back()[5].get<double>())/2;
      const bool late=intersector.PerformUntil(ray,100,dense,[&](const auto& point) {
        ++calls;
        return point_values(point)[5].template get<double>()>threshold;
      });
      require(late && calls>1,"A rejected root must not prevent a later qualified root");
      require(calls<=static_cast<int>(complete.size()),"Qualification preserves duplicate suppression");
      late_acceptance=true;
    }
    equivalent(complete,inventory(intersector,ray,dense));
  };
  for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
    surface.Surface(TopoDS::Face(it.Current()));
    if(surface.Surface().GetType()!=GeomAbs_BSplineSurface) continue;
    intersector.Load(&surface);
    // Repeated grid changes and face changes cannot retain stale brackets.
    for(bool dense:{false,true,false,true}) {
      verify(gp_Lin(gp_Pnt(5.6,0,10),gp_Dir(0,0,-1)),dense);
      verify(gp_Lin(gp_Pnt(100,0,10),gp_Dir(0,0,-1)),dense);
    }
  }
  require(multiple_roots && late_acceptance,"Thread fixture exercises several roots and rejection before acceptance");
  surface.Surface(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,1),gp_Dir(0,0,1)),-2,2,-2,2).Face());
  intersector.Load(&surface);
  verify(gp_Lin(gp_Pnt(0,0,10),gp_Dir(0,0,-1)),false);
#else
  (void)built;
  std::cerr<<"Streaming root checks require the v2 patched SDK; older SDK coverage is limited to projection regressions\n";
#endif
}
void verify_thread(const Json& model,double height) {
  BuiltModel built(model);
  const auto summary=built.summary(),topology=built.topology();
  const auto request=drawing({"top","front","isometric"});
  const auto result=built.drawing(request);
  near(result.at("tolerance_mm"),0.02);
  const auto& top=result.at("views")[0];
  // Complete turns retain the actual major diameter in their axial projection.
  near(top.at("bounds_mm")[0],-6,2e-6); near(top.at("bounds_mm")[1],-6,2e-6);
  near(top.at("bounds_mm")[2],6,2e-6); near(top.at("bounds_mm")[3],6,2e-6);
  const auto& front=result.at("views")[1];
  near(front.at("bounds_mm")[1],0,2e-6); near(front.at("bounds_mm")[3],height,2e-6);
  for (const auto& view:result.at("views")) {
    require(!view.at("entities").empty(),"Thread projection retains exact source curves");
    bool visible=false,hidden=false;
    for (const auto& entity:view.at("entities")) {
      if (entity.at("hidden")==true) hidden=true; else visible=true;
      if (entity.contains("points")) for (const auto& point:entity.at("points"))
        require(std::isfinite(point[0].get<double>()) && std::isfinite(point[1].get<double>()),"Finite projected thread points");
    }
    require(visible && hidden,"Thread views retain both visible and hidden curves");
  }
  equivalent(summary,built.summary()); equivalent(topology,built.topology());
  BuiltModel restored(model,built.snapshot());
  equivalent(summary,restored.summary());
  equivalent(result,restored.drawing(request));
}
}

int main() {
  try {
    configure_kernel_logging();
    // Different hand and pitch exercise the same exact helical B-spline
    // supports with different parameter ranges, without timing assertions.
    verify_thread(thread(2,2,"left"),2);
    verify_thread(thread(1.25,3.75),3.75);
    BuiltModel fine_thread(thread(1.25,3.75));
    exact_occlusion_regression(fine_thread);
    streaming_root_regression(fine_thread);

    std::ifstream input(std::filesystem::path(CAD_SOURCE_DIR)/"examples/m20-knob.create.json");
    require(static_cast<bool>(input),"Read saved parametric knob fixture");
    Json create; input>>create;
    auto model=create.at("model"); model["parameters"]["stud_length"]=1.5;
    BuiltModel knob(model);
    const auto summary=knob.summary(),topology=knob.topology();
    const auto front=knob.drawing(drawing({"front"})).at("views")[0];

    // This exact thread-root point is behind another flank. The original
    // 10x10 HLR bracketing exposed it; the denser B-spline brackets must keep
    // its projected curve hidden. Independent exact face-ray intersection
    // meets the front surface 1.9845194867 mm ahead along (0,-1,0).
    constexpr double x=-9.8577119726752382,y=1.6809284861550422,z=18.026548930686019;
    require(curve_distance(front,true,x,z)<0.011,"Occluded thread-root curve remains in hidden geometry");
    require(curve_distance(front,false,x,z)>0.01,"Occluded thread-root curve is not exposed as visible");
    equivalent(summary,knob.summary()); equivalent(topology,knob.topology());

    // Ask the separate exact balloon visibility path about the same source
    // point. This verifies the physical occlusion independently of HLR output.
    const auto output=model.at("output");
    model["features"].push_back({{"id","assembly"},{"type","assembly"},
      {"parts",Json::array({{{"id","knob"},{"input",output}}})}});
    model["output"]="assembly";
    BuiltModel assembled(model);
    auto request=drawing({"front"});
    request["views"][0]["balloon_anchors"]=Json::array({{{"part_id","knob"},{"point",{x,y,z}}}});
    bool rejected=false;
    try { assembled.drawing(request); }
    catch (const Error& error) {
      rejected=true;
      require(error.code=="invalid_drawing","Exact ray rejects physically occluded thread-root anchor");
      require(error.details.at("part_id")=="knob" && error.details.at("view_id")=="front","Occlusion error retains source context");
    }
    require(rejected,"Occluded thread-root anchor cannot appear visible");
    std::cout<<checks<<" performance geometry checks passed\n";
  } catch (const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
  }
}
