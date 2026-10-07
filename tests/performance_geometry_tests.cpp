#include "agentcad/kernel.hpp"
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <HLRAlgo_Projector.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>
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
