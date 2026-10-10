#include "agentcad/kernel.hpp"
#include "agentcad/measurement.hpp"
#include "agentcad/section.hpp"
#include "agentcad/model.hpp"
#include "agentcad/authoring.hpp"
#include "agentcad/robot.hpp"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <Geom2d_Line.hxx>
#include <gp_GTrsf.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepFilletAPI_MakeFillet2d.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <GeomProjLib.hxx>
#include <Geom_Plane.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffset_MakeOffset.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_BezierSurface.hxx>
#include <Geom_BSplineSurface.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <NCollection_Array2.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <GC_MakeSegment2d.hxx>
#include <BRepLib.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepAlgoAPI_Check.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepTools_Modification.hxx>
#include <BRepTools_Modifier.hxx>
#include <GeomConvert.hxx>
#include <GeomAPI.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomLProp_SLProps.hxx>
#include <BRepOffsetAPI_MakeFilling.hxx>
#include <BRepProj_Projection.hxx>
#include <BSplCLib.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Result.hxx>
#include <ShapeUpgrade_ShapeDivideClosed.hxx>
#include <BRepTools_Modifier.hxx>
#include <BRepTools_Modification.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom_RectangularTrimmedSurface.hxx>
#include <BRepCheck_Shell.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include "agentcad/fabrication.hpp"
#include <limits>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GCPnts_UniformDeflection.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <BndLib_Add3dCurve.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <HLRAlgo_Projector.hxx>
#include <NCollection_Array1.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Writer.hxx>
#include <STEPControl_Reader.hxx>
#include <StlAPI_Writer.hxx>
#include <Standard_Version.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pln.hxx>
#include <gp_Circ.hxx>
#include <gp_Lin.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_PrinterOStream.hxx>
#include <map>
#include <memory>
#include <algorithm>
#include <vector>
#include <cmath>
#include <numbers>
#include <sstream>
#include <fstream>
#include <set>
#include <tuple>
#include <bit>

static_assert(OCC_VERSION_HEX == 0x080001, "agent-3d-cad requires OCCT 8.0.1");

namespace agentcad {
namespace {
// Bound optional snapshot serialization before allocating a giant string.
class SnapshotBuffer final : public std::streambuf {
public:
  std::string bytes;
  explicit SnapshotBuffer(std::size_t limit):limit_(limit) {}
protected:
  std::streamsize xsputn(const char* data,std::streamsize count) override {
    if(count<0 || static_cast<std::size_t>(count)>limit_-bytes.size()) return 0;
    bytes.append(data,static_cast<std::size_t>(count)); return count;
  }
  int_type overflow(int_type value) override {
    if(traits_type::eq_int_type(value,traits_type::eof())) return traits_type::not_eof(value);
    if(bytes.size()>=limit_) return traits_type::eof();
    bytes.push_back(traits_type::to_char_type(value)); return value;
  }
private:
  std::size_t limit_;
};
using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
struct AssemblyPart {
  std::string id, input;
  gp_Trsf transform;
  TopoDS_Shape shape;
};
struct FeatureGeometry {
  TopoDS_Shape shape;
  ShapeMap faces, edges;
  Json provenance;
  std::vector<AssemblyPart> parts;
  Json mates = Json::array();
  Json tree = Json::array();
  Json motion;
  std::map<std::string,gp_Trsf> placements;
  std::vector<std::string> face_parts, edge_parts;
  explicit FeatureGeometry(const TopoDS_Shape& value) : shape(value) {
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
  }
};
}
struct BuiltModel::Impl {
  TopoDS_Shape shape;
  std::string output;
  std::map<std::string, FeatureGeometry> features;
  Json model;
  const FeatureGeometry& feature(const std::string& id) const {
    const auto found = features.find(id.empty() ? output : id);
    if (found == features.end()) throw Error("not_found", "Unknown feature: " + id);
    return found->second;
  }
};
std::string kernel_version() { return OCC_VERSION_COMPLETE; }
std::string kernel_failure_message(const std::exception& failure) {
  const char* message=failure.what();
  if (message && *message) return message;
  // OCCT 8 failures are std::exceptions without RTTI handles; ExceptionType()
  // names the concrete class, which is the only evidence an empty one carries.
  if (const auto* occt=dynamic_cast<const Standard_Failure*>(&failure))
    return std::string(occt->ExceptionType())+" raised by OpenCascade without a message";
  return "Kernel failure without a message";
}
void configure_kernel_logging() {
  auto printer = new Message_PrinterOStream("cerr", true);
  printer->SetToColorize(false);
  Message::DefaultMessenger()->ChangePrinters().Clear();
  Message::DefaultMessenger()->AddPrinter(printer);
}

namespace {
// A hole must remove more than this fraction of its own cylinder. A real hole
// removes essentially all of it where it enters; the threshold sits far below any
// physically meaningful dent and far above volume-integration round-off.
constexpr double hole_removal_tolerance = 1e-6;
// The single conversion of an OCCT failure into a domain error.
Error occt_error(const Standard_Failure& failure, Json details = Json::object(), const std::string& code = "kernel_failure") {
  return Error(code, kernel_failure_message(failure), std::move(details));
}
int count(const TopoDS_Shape& shape, TopAbs_ShapeEnum kind) {
  NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> map;
  TopExp::MapShapes(shape, kind, map);
  return map.Extent();
}
const char* check_status_name(BRepCheck_Status status){switch(status){
case BRepCheck_NoError:return "NoError";
case BRepCheck_InvalidPointOnCurve:return "InvalidPointOnCurve";
case BRepCheck_InvalidPointOnCurveOnSurface:return "InvalidPointOnCurveOnSurface";
case BRepCheck_InvalidPointOnSurface:return "InvalidPointOnSurface";
case BRepCheck_No3DCurve:return "No3DCurve";
case BRepCheck_Multiple3DCurve:return "Multiple3DCurve";
case BRepCheck_Invalid3DCurve:return "Invalid3DCurve";
case BRepCheck_NoCurveOnSurface:return "NoCurveOnSurface";
case BRepCheck_InvalidCurveOnSurface:return "InvalidCurveOnSurface";
case BRepCheck_InvalidCurveOnClosedSurface:return "InvalidCurveOnClosedSurface";
case BRepCheck_InvalidSameRangeFlag:return "InvalidSameRangeFlag";
case BRepCheck_InvalidSameParameterFlag:return "InvalidSameParameterFlag";
case BRepCheck_InvalidDegeneratedFlag:return "InvalidDegeneratedFlag";
case BRepCheck_FreeEdge:return "FreeEdge";
case BRepCheck_InvalidMultiConnexity:return "InvalidMultiConnexity";
case BRepCheck_InvalidRange:return "InvalidRange";
case BRepCheck_EmptyWire:return "EmptyWire";
case BRepCheck_RedundantEdge:return "RedundantEdge";
case BRepCheck_SelfIntersectingWire:return "SelfIntersectingWire";
case BRepCheck_NoSurface:return "NoSurface";
case BRepCheck_InvalidWire:return "InvalidWire";
case BRepCheck_RedundantWire:return "RedundantWire";
case BRepCheck_IntersectingWires:return "IntersectingWires";
case BRepCheck_InvalidImbricationOfWires:return "InvalidImbricationOfWires";
case BRepCheck_EmptyShell:return "EmptyShell";
case BRepCheck_RedundantFace:return "RedundantFace";
case BRepCheck_InvalidImbricationOfShells:return "InvalidImbricationOfShells";
case BRepCheck_UnorientableShape:return "UnorientableShape";
case BRepCheck_NotClosed:return "NotClosed";
case BRepCheck_NotConnected:return "NotConnected";
case BRepCheck_SubshapeNotInShape:return "SubshapeNotInShape";
case BRepCheck_BadOrientation:return "BadOrientation";
case BRepCheck_BadOrientationOfSubshape:return "BadOrientationOfSubshape";
case BRepCheck_InvalidPolygonOnTriangulation:return "InvalidPolygonOnTriangulation";
case BRepCheck_InvalidToleranceValue:return "InvalidToleranceValue";
case BRepCheck_EnclosedRegion:return "EnclosedRegion";
case BRepCheck_CheckFail:return "CheckFail";
default:return "Unknown";}}
void check_shape(const TopoDS_Shape& shape) {
  if (shape.IsNull()) throw Error("invalid_shape", "Feature produced no geometry");
  BRepCheck_Analyzer analyzer(shape);
  if (!analyzer.IsValid()) {
    Json failures=Json::array();bool truncated=false;
    for(const auto kind:{TopAbs_SOLID,TopAbs_SHELL,TopAbs_FACE,TopAbs_WIRE,TopAbs_EDGE,TopAbs_VERTEX}) {
      ShapeMap entities;TopExp::MapShapes(shape,kind,entities);
      for(int i=1;i<=entities.Extent();++i) {
        const auto checked=analyzer.Result(entities(i));if(checked.IsNull())continue;
        Json statuses=Json::array();
        const auto append=[&](const auto& list){for(const auto status:list)if(status!=BRepCheck_NoError&&std::find(statuses.begin(),statuses.end(),check_status_name(status))==statuses.end())statuses.push_back(check_status_name(status));};
        append(checked->Status());
        for(checked->InitContextIterator();checked->MoreShapeInContext();checked->NextShapeInContext())append(checked->StatusOnShape());
        if(statuses.empty())continue;
        if(failures.size()>=64){truncated=true;continue;}
        const char* name=kind==TopAbs_SOLID?"solid":kind==TopAbs_SHELL?"shell":kind==TopAbs_FACE?"face":kind==TopAbs_WIRE?"wire":kind==TopAbs_EDGE?"edge":"vertex";
        failures.push_back({{"kind",name},{"index",i},{"brep_check_statuses",statuses}});
      }
    }
    throw Error("invalid_shape", "Feature did not produce valid solid geometry",
      {{"invalid_entities",failures},{"diagnostics_truncated",truncated},{"index_lifetime","this_shape_only"}});
  }
  // A valid compound may still contain open faces or wires beside its solids.
  // Traverse containment, preserving cumulative orientation and placement,
  // instead of treating a positive aggregate volume as solid-only evidence.
  std::vector<TopoDS_Shape> pending{shape};
  std::size_t solids=0;
  while (!pending.empty()) {
    const auto current=pending.back(); pending.pop_back();
    if (current.ShapeType() == TopAbs_SOLID) {
      for (TopoDS_Iterator shells(current);shells.More();shells.Next()) {
        if (shells.Value().ShapeType()!=TopAbs_SHELL || BRepCheck_Shell(TopoDS::Shell(shells.Value())).Closed()!=BRepCheck_NoError)
          throw Error("invalid_shape", "Every solid must have closed shell boundaries", {{"solid_index",solids+1}});
      }
      GProp_GProps props;
      BRepGProp::VolumeProperties(current, props);
      if (!std::isfinite(props.Mass()) || props.Mass() <= 0)
        throw Error("invalid_shape", "Every solid must have positive finite volume", {{"solid_index",solids+1}});
      ++solids;
    } else if (current.ShapeType() == TopAbs_COMPOUND || current.ShapeType() == TopAbs_COMPSOLID) {
      TopoDS_Iterator children(current);
      if (!children.More()) throw Error("invalid_shape", "Empty geometry containers are not solid geometry");
      for (;children.More();children.Next()) pending.push_back(children.Value());
    } else {
      throw Error("invalid_shape", "Feature contains geometry outside a solid; loose faces, shells, wires, edges and vertices are unsupported");
    }
  }
  if (!solids) throw Error("invalid_shape", "Feature did not produce solid geometry");
  GProp_GProps aggregate;
  BRepGProp::VolumeProperties(shape,aggregate);
  if (!std::isfinite(aggregate.Mass()) || aggregate.Mass()<=0)
    throw Error("invalid_shape", "Feature has nonpositive or nonfinite aggregate volume");
}
// Surface outputs have their own contract. Solid validation above stays strict:
// an open shell can never satisfy a solid operation or manufacturing promise.
void check_surface_shape(const TopoDS_Shape& shape) {
  if(shape.IsNull())throw Error("invalid_shape","Surface result must have a valid exact B-rep");
  if(!BRepCheck_Analyzer(shape).IsValid()){try{check_shape(shape);}catch(const Error& e){throw Error("invalid_shape","Surface result must have a valid exact B-rep",e.details);}}
  std::vector<TopoDS_Shape> pending{shape};std::size_t faces=0;
  while(!pending.empty()) {
    const auto current=pending.back();pending.pop_back();
    if(current.ShapeType()==TopAbs_FACE) {
      GProp_GProps area;BRepGProp::SurfaceProperties(current,area);
      if(!std::isfinite(area.Mass())||area.Mass()<=1e-10)throw Error("invalid_shape","Surface patches require positive finite area");
      Bnd_Box box;BRepBndLib::AddOptimal(current,box,false,false);
      if(box.IsVoid()||box.IsOpen())throw Error("invalid_shape","Surface patches require finite bounds");
      if(++faces>256)throw Error("limit_exceeded","A surface result permits at most 256 faces");
    } else if(current.ShapeType()==TopAbs_SHELL||current.ShapeType()==TopAbs_COMPOUND) {
      TopoDS_Iterator children(current);if(!children.More())throw Error("invalid_shape","Surface containers must not be empty");
      for(;children.More();children.Next())pending.push_back(children.Value());
    } else throw Error("invalid_shape","Surface results must contain faces or shells only");
  }
  if(!faces)throw Error("invalid_shape","Surface feature produced no finite patches");
  BRepAlgoAPI_Check interference(shape,false,true);
  if(!interference.IsValid())throw Error("invalid_shape","Surface result has self-interference");
}
void check_surface_intent(const Json& feature,const TopoDS_Shape& shape) {
  check_surface_shape(shape);
  if(feature.at("type")=="surface_shell") {
    ShapeMap shells;TopExp::MapShapes(shape,TopAbs_SHELL,shells);
    if(shells.Extent()!=1||shape.ShapeType()!=TopAbs_SHELL)throw Error("invalid_shape","Surface shell must contain one connected shell");
    const auto closed=BRepCheck_Shell(TopoDS::Shell(shape)).Closed()==BRepCheck_NoError;
    if(closed!=feature.at("closed").get<bool>())throw Error("invalid_shape","Sewn shell closure differs from the authored closed flag",{{"requested_closed",feature.at("closed")},{"actual_closed",closed}});
  } else if(feature.at("type")=="import_step_surface") { /* Explicit captured surface containers retain their exact topology. */ }
  else if(shape.ShapeType()!=TopAbs_FACE)throw Error("invalid_shape","A parametric surface patch or UV trim must be one exact face");
}
void check_curve_shape(const TopoDS_Shape& shape) {
  if(shape.IsNull()||!BRepCheck_Analyzer(shape).IsValid())throw Error("invalid_shape","Curve result must have a valid exact B-rep");
  std::vector<TopoDS_Shape> pending{shape};std::size_t edges=0;
  while(!pending.empty()) {const auto current=pending.back();pending.pop_back();
    if(current.ShapeType()==TopAbs_EDGE){BRepAdaptor_Curve curve(TopoDS::Edge(current));const auto length=GCPnts_AbscissaPoint::Length(curve);if(!std::isfinite(length)||length<=1e-7)throw Error("invalid_shape","Curve edges require positive finite length");if(++edges>1024)throw Error("limit_exceeded","Curve outputs permit at most 1024 exact edges");}
    else if(current.ShapeType()==TopAbs_WIRE||current.ShapeType()==TopAbs_COMPOUND){TopoDS_Iterator children(current);if(!children.More())throw Error("invalid_shape","Empty curve container");for(;children.More();children.Next())pending.push_back(children.Value());}
    else throw Error("invalid_shape","Curve outputs may contain wires and edges only");
  }
  if(!edges)throw Error("invalid_shape","Curve result has no exact edges");
  BRepAlgoAPI_Check check(shape,false,true);if(!check.IsValid())throw Error("invalid_shape","Curve result self-intersects");
}
// Restrict polynomial support surfaces to the face's existing UV domain before
// offsetting. Segment is exact knot insertion, with unchanged 3D boundaries and
// unchanged UV parameters. OCCT's Bezier offset side-wall construction otherwise
// extrapolates the full support and makes unorientable trimmed crown faces.
class BoundedPolynomialSupports final : public BRepTools_Modification {
public:
  bool NewSurface(const TopoDS_Face& f,occ::handle<Geom_Surface>& surface,TopLoc_Location& location,double& tolerance,bool& reverse_wires,bool& reverse_face) override {
    auto original=BRep_Tool::Surface(f,location);if(!occ::down_cast<Geom_BezierSurface>(original)&&!occ::down_cast<Geom_BSplineSurface>(original))return false;
    auto spline=GeomConvert::SurfaceToBSplineSurface(original);spline=occ::down_cast<Geom_BSplineSurface>(spline->Copy());
    double u0,u1,v0,v1;BRepTools::UVBounds(f,u0,u1,v0,v1);spline->Segment(u0,u1,v0,v1);surface=spline;tolerance=BRep_Tool::Tolerance(f);reverse_wires=false;reverse_face=false;return true;
  }
  bool NewCurve(const TopoDS_Edge& edge,occ::handle<Geom_Curve>& curve,TopLoc_Location& location,double& tolerance) override {
    double first,last;auto original=BRep_Tool::Curve(edge,location,first,last);if(original.IsNull()||(!occ::down_cast<Geom_BezierCurve>(original)&&!occ::down_cast<Geom_BSplineCurve>(original)))return false;
    auto spline=GeomConvert::CurveToBSplineCurve(original);spline=occ::down_cast<Geom_BSplineCurve>(spline->Copy());spline->Segment(first,last);curve=spline;tolerance=BRep_Tool::Tolerance(edge);return true;
  }
  bool NewPoint(const TopoDS_Vertex&,gp_Pnt&,double&) override {return false;}
  bool NewCurve2d(const TopoDS_Edge& e,const TopoDS_Face& f,const TopoDS_Edge&,const TopoDS_Face&,occ::handle<Geom2d_Curve>& curve,double& tolerance) override {
    auto surface=BRep_Tool::Surface(f);if(!occ::down_cast<Geom_BezierSurface>(surface)&&!occ::down_cast<Geom_BSplineSurface>(surface))return false;
    double first,last;curve=BRep_Tool::CurveOnSurface(e,f,first,last);tolerance=BRep_Tool::Tolerance(e);return !curve.IsNull();
  }
  bool NewParameter(const TopoDS_Vertex&,const TopoDS_Edge&,double&,double&) override {return false;}
  GeomAbs_Shape Continuity(const TopoDS_Edge& e,const TopoDS_Face& f1,const TopoDS_Face& f2,const TopoDS_Edge&,const TopoDS_Face&,const TopoDS_Face&) override {return BRep_Tool::Continuity(e,f1,f2);}
};
struct OffsetSupportHistory {
  BRepOffset_MakeOffset& operation;BRepTools_Modifier& normalization;
  bool IsDeleted(const TopoDS_Shape& s){return operation.IsDeleted(normalization.ModifiedShape(s));}
  NCollection_List<TopoDS_Shape> Modified(const TopoDS_Shape& s){const auto& n=normalization.ModifiedShape(s);auto result=operation.Modified(n);result.Append(n);return result;}
  NCollection_List<TopoDS_Shape> Generated(const TopoDS_Shape& s){return operation.Generated(normalization.ModifiedShape(s));}
};
struct SewingHistory {
  BRepBuilderAPI_Sewing& operation;
  bool IsDeleted(const TopoDS_Shape&){return false;}
  NCollection_List<TopoDS_Shape> Modified(const TopoDS_Shape& shape) {
    NCollection_List<TopoDS_Shape> result;
    if(operation.IsModified(shape))result.Append(operation.Modified(shape));
    else if(operation.IsModifiedSubShape(shape))result.Append(operation.ModifiedSubShape(shape));
    return result;
  }
  NCollection_List<TopoDS_Shape> Generated(const TopoDS_Shape&){return {};}
};
occ::handle<Geom_Surface> parametric_surface(const Json& feature,const Json& parameters) {
  const auto& points=feature.at("control_points");const auto rows=static_cast<int>(points.size()),columns=static_cast<int>(points[0].size());
  NCollection_Array2<gp_Pnt> poles(1,rows,1,columns);NCollection_Array2<double> weights(1,rows,1,columns);
  for(int u=1;u<=rows;++u)for(int v=1;v<=columns;++v) {
    const auto p=vector3(points[u-1][v-1],parameters);poles.SetValue(u,v,gp_Pnt(p[0],p[1],p[2]));
    weights.SetValue(u,v,feature.contains("weights")?scalar(feature.at("weights")[u-1][v-1],parameters,"dimensionless"):1);
  }
  if(feature.at("type")=="surface_bezier") {
    if(feature.contains("weights"))return new Geom_BezierSurface(poles,weights);
    return new Geom_BezierSurface(poles);
  }
  const auto fill_knots=[&](const char* name) {
    const auto& values=feature.at(name);NCollection_Array1<double> array(1,static_cast<int>(values.size()));
    for(int i=1;i<=array.Length();++i)array.SetValue(i,scalar(values[i-1],parameters,"dimensionless"));return array;
  };
  const auto fill_mults=[&](const char* name) {
    const auto& values=feature.at(name);NCollection_Array1<int> array(1,static_cast<int>(values.size()));
    for(int i=1;i<=array.Length();++i)array.SetValue(i,values[i-1].get<int>());return array;
  };
  const auto u_knots=fill_knots("knots_u"),v_knots=fill_knots("knots_v");const auto u_mults=fill_mults("multiplicities_u"),v_mults=fill_mults("multiplicities_v");
  if(feature.contains("weights"))return new Geom_BSplineSurface(poles,weights,u_knots,v_knots,u_mults,v_mults,feature.at("degree_u").get<int>(),feature.at("degree_v").get<int>(),false,false);
  return new Geom_BSplineSurface(poles,u_knots,v_knots,u_mults,v_mults,feature.at("degree_u").get<int>(),feature.at("degree_v").get<int>(),false,false);
}
// Exact planar region validation is shared by numeric, derived and imported sketches.
// Containers may only contain faces. BRep validity alone accepts loose geometry.
void check_sketch_shape(const TopoDS_Shape& shape,const gp_Ax2& plane) {
  if(shape.IsNull()||!BRepCheck_Analyzer(shape).IsValid())throw Error("invalid_shape","Sketch result must be valid planar geometry");
  std::vector<TopoDS_Shape> pending{shape};std::vector<TopoDS_Face> faces;
  while(!pending.empty()) {
    const auto current=pending.back();pending.pop_back();
    if(current.ShapeType()==TopAbs_FACE) {
      const auto face=TopoDS::Face(current);const BRepAdaptor_Surface surface(face);
      if(surface.GetType()!=GeomAbs_Plane||std::abs(surface.Plane().Axis().Direction().Dot(plane.Direction()))<1-1e-9
          ||surface.Plane().Distance(plane.Location())>1e-7)throw Error("invalid_shape","Sketch regions must be coplanar with their workplane");
      GProp_GProps area;BRepGProp::SurfaceProperties(face,area);
      if(!std::isfinite(area.Mass())||area.Mass()<=1e-10)throw Error("invalid_shape","Sketch regions need positive finite area");
      if(faces.size()>=128)throw Error("limit_exceeded","A sketch permits at most 128 disjoint regions");
      faces.push_back(face);
    } else if(current.ShapeType()==TopAbs_COMPOUND) {
      TopoDS_Iterator children(current);if(!children.More())throw Error("invalid_shape","Sketch operation produced no regions");
      for(;children.More();children.Next())pending.push_back(children.Value());
    } else throw Error("invalid_shape","Sketch result contains loose or nonplanar geometry");
  }
  if(faces.empty())throw Error("invalid_shape","Sketch operation produced no regions");
  for(std::size_t a=0;a<faces.size();++a)for(std::size_t b=a+1;b<faces.size();++b) {
    BRepAlgoAPI_Common overlap;NCollection_List<TopoDS_Shape> left,right;left.Append(faces[a]);right.Append(faces[b]);
    overlap.SetArguments(left);overlap.SetTools(right);overlap.SetNonDestructive(true);overlap.SetRunParallel(false);overlap.Build();
    if(!overlap.IsDone()||overlap.HasErrors())throw Error("kernel_failure","Sketch overlap validation failed");
    GProp_GProps area;BRepGProp::SurfaceProperties(overlap.Shape(),area);
    if(area.Mass()>1e-9)throw Error("invalid_shape","Sketch regions must not overlap; fuse overlapping profiles explicitly");
  }
}
TopoDS_Shape face_compound(const std::vector<TopoDS_Face>& faces) {
  if(faces.size()==1)return faces.front();
  TopoDS_Compound result;BRep_Builder builder;builder.MakeCompound(result);
  for(const auto& face:faces)builder.Add(result,face);
  return result;
}
// Patterns and assemblies copy exact solids without Boolean cost, so nesting
// multiplies them geometrically (three 64-copy patterns: 262,144 solids). Each
// such feature's output is bounded from its inputs before any copy is made.
constexpr std::size_t replication_solid_limit = 4096;
constexpr std::size_t replication_face_limit = 65536;
void replication_budget(std::size_t solids, std::size_t faces) {
  if (solids > replication_solid_limit || faces > replication_face_limit)
    throw Error("limit_exceeded", "Feature would replicate more solids or faces than its per-feature budget",
      {{"solid_limit",replication_solid_limit},{"face_limit",replication_face_limit},{"solids",solids},{"faces",faces}});
}
Json point(const gp_Pnt& p) { return {p.X(), p.Y(), p.Z()}; }
Json direction(const gp_Dir& d) { return {d.X(), d.Y(), d.Z()}; }
Json bounds(const TopoDS_Shape& shape) {
  Bnd_Box box;
  BRepBndLib::AddOptimal(shape, box, false, false);
  const auto limits = box.Get();
  return {{"min", {limits.Xmin, limits.Ymin, limits.Zmin}}, {"max", {limits.Xmax, limits.Ymax, limits.Zmax}}};
}
std::string curve_kind(GeomAbs_CurveType kind) {
  switch (kind) {
    case GeomAbs_Line: return "line";
    case GeomAbs_Circle: return "circle";
    case GeomAbs_Ellipse: return "ellipse";
    case GeomAbs_Hyperbola: return "hyperbola";
    case GeomAbs_Parabola: return "parabola";
    case GeomAbs_BezierCurve: return "bezier";
    case GeomAbs_BSplineCurve: return "bspline";
    case GeomAbs_OffsetCurve: return "offset";
    default: return "other";
  }
}
std::string surface_kind(GeomAbs_SurfaceType kind) {
  switch (kind) {
    case GeomAbs_Plane: return "plane";
    case GeomAbs_Cylinder: return "cylinder";
    case GeomAbs_Cone: return "cone";
    case GeomAbs_Sphere: return "sphere";
    case GeomAbs_Torus: return "torus";
    case GeomAbs_BezierSurface: return "bezier";
    case GeomAbs_BSplineSurface: return "bspline";
    case GeomAbs_SurfaceOfRevolution: return "revolution";
    case GeomAbs_SurfaceOfExtrusion: return "extrusion";
    case GeomAbs_OffsetSurface: return "offset";
    default: return "other";
  }
}
Json edge_descriptor(const TopoDS_Edge& edge, int index) {
  GProp_GProps props;
  BRepGProp::LinearProperties(edge, props);
  Json result = {{"id", "edge-" + std::to_string(index)}, {"length_mm", props.Mass()},
    {"center_mm", point(props.CentreOfMass())}, {"bounds_mm", bounds(edge)}, {"degenerate", BRep_Tool::Degenerated(edge)}};
  if (BRep_Tool::Degenerated(edge)) { result["curve_kind"] = "other"; return result; }
  BRepAdaptor_Curve curve(edge);
  result["curve_kind"] = curve_kind(curve.GetType());
  if (curve.GetType() == GeomAbs_Line) result["direction"] = direction(curve.Line().Direction());
  if (curve.GetType() == GeomAbs_Circle) {
    result["radius_mm"] = curve.Circle().Radius();
    result["axis"] = direction(curve.Circle().Axis().Direction());
  }
  return result;
}
std::optional<TopoDS_Face> periodic_rectangle(const TopoDS_Face& face) {
  // Only a complete analytic periodic wall with no inner trims is eligible.
  // Recreate its seam on the same surface, solely in private mesh geometry.
  BRepAdaptor_Surface adaptor(face);if(adaptor.GetType()!=GeomAbs_Cone&&adaptor.GetType()!=GeomAbs_Cylinder)return {};
  if(count(face,TopAbs_WIRE)!=1||count(face,TopAbs_EDGE)>16)return {};
  for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()){const auto kind=BRepAdaptor_Curve(TopoDS::Edge(it.Current())).GetType();if(kind!=GeomAbs_Circle&&kind!=GeomAbs_Line)return {};}
  const auto surface=BRep_Tool::Surface(face);if(!surface->IsUPeriodic())return {};
  double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
  if(std::abs(u1-u0-surface->UPeriod())>1e-7)return {};
  BRepBuilderAPI_MakeFace maker(surface,u0,u0+surface->UPeriod(),v0,v1,1e-7);if(!maker.IsDone())return {};
  auto result=maker.Face();result.Orientation(face.Orientation());if(!BRepCheck_Analyzer(result).IsValid())return {};
  GProp_GProps before,after;BRepGProp::SurfaceProperties(face,before);BRepGProp::SurfaceProperties(result,after);
  if(std::abs(before.Mass()-after.Mass())>1e-8*std::max(1.0,std::abs(before.Mass())))return {};
  const auto a=bounds(face),b=bounds(result);for(const auto* end:{"min","max"})for(int k=0;k<3;++k)if(std::abs(a.at(end)[k].get<double>()-b.at(end)[k].get<double>())>1e-6)return {};
  return result;
}
// A periodic trim can defeat OCCT's 2D triangulator despite a valid B-rep.
// Split only a private face for tessellation; retain the original face identity,
// analytic surface and exact solid. Never repair or replace the saved geometry.
std::vector<TopoDS_Face> tessellated_faces(const TopoDS_Face& face, const Json& context) {
  TopLoc_Location location;
  const auto ready=BRep_Tool::Triangulation(face,location);
  if(!ready.IsNull()&&ready->NbTriangles()>0)return {face};
  Json details=context;details["surface_kind"]=surface_kind(BRepAdaptor_Surface(face).GetType());details["bounds_mm"]=bounds(face);
  BRepBuilderAPI_Copy copy(face);ShapeUpgrade_ShapeDivideClosed split(copy.Shape());
  split.SetNbSplitPoints(1);split.Perform();const auto divided=split.Result();
  if(!divided.IsNull()&&count(divided,TopAbs_FACE)>1&&count(divided,TopAbs_FACE)<=16&&BRepCheck_Analyzer(divided).IsValid()) {
    GProp_GProps before,after;BRepGProp::SurfaceProperties(face,before);BRepGProp::SurfaceProperties(divided,after);
    const auto a=bounds(face),b=bounds(divided);bool same=std::abs(before.Mass()-after.Mass())<=1e-7*std::max(1.0,std::abs(before.Mass()));
    for(const auto* end:{"min","max"})for(int k=0;k<3;++k)same=same&&std::abs(a.at(end)[k].get<double>()-b.at(end)[k].get<double>())<=1e-6;
    if(same) {
      BRepMesh_IncrementalMesh retry(divided,.1,false,.5,false);std::vector<TopoDS_Face> fragments;
      for(TopExp_Explorer it(divided,TopAbs_FACE);it.More();it.Next()) {
        const auto fragment=TopoDS::Face(it.Current());const auto mesh=BRep_Tool::Triangulation(fragment,location);
        if(mesh.IsNull()||mesh->NbTriangles()==0){fragments.clear();break;}fragments.push_back(fragment);
      }
      if(retry.IsDone()&&!fragments.empty())return fragments;
    }
  }
  details["attempted_periodic_split"]=true;
  throw Error("kernel_failure","Face tessellation failed, including periodic-face retry",details);
}
TopoDS_Shape read_step(const std::string& content) {
  STEPControl_Reader reader;reader.SetShapeProcessFlags(ShapeProcess::OperationsFlags{});
  std::istringstream stream(content);
  if(reader.ReadStream("embedded.step",stream)!=IFSelect_RetDone)throw Error("kernel_failure","Embedded STEP content could not be imported");
  reader.SetSystemLengthUnit(1.0);
  const int roots=reader.NbRootsForTransfer(),transferred=reader.TransferRoots();
  if(roots<=0||transferred!=roots)throw Error("kernel_failure","STEP did not transfer every root; partial transfers are rejected",
    {{"total_roots",roots},{"transferred_roots",transferred}});
  return reader.OneShape();
}
bool matches(const Json& descriptor, const Json& selector, const Json& parameters) {
  if (descriptor.at("degenerate") == true || descriptor.at("curve_kind") != selector.at("curve_kind")) return false;
  if (selector.contains("direction")) {
    if (!descriptor.contains("direction")) return false;
    const auto wanted = vector3(selector.at("direction").at("vector"), parameters, "dimensionless");
    const auto actual = descriptor.at("direction").get<std::array<double,3>>();
    const double magnitude = std::hypot(wanted[0], wanted[1], wanted[2]);
    const double dot = std::abs((wanted[0]*actual[0]+wanted[1]*actual[1]+wanted[2]*actual[2])/magnitude);
    if (std::acos(std::clamp(dot, 0.0, 1.0)) > scalar(selector.at("direction").at("tolerance"), parameters, "rad")) return false;
  }
  if (selector.contains("center")) {
    const auto wanted = vector3(selector.at("center").at("point"), parameters);
    const auto actual = descriptor.at("center_mm").get<std::array<double,3>>();
    if (std::hypot(wanted[0]-actual[0], wanted[1]-actual[1], wanted[2]-actual[2]) > scalar(selector.at("center").at("tolerance"), parameters)) return false;
  }
  if (selector.contains("length") && std::abs(descriptor.at("length_mm").get<double>() - scalar(selector.at("length").at("value"), parameters)) > scalar(selector.at("length").at("tolerance"), parameters)) return false;
  return true;
}
Json face_descriptor(const TopoDS_Face& face,int index) {
  GProp_GProps props;BRepGProp::SurfaceProperties(face,props);
  BRepAdaptor_Surface surface(face);
  Json result={{"id","face-"+std::to_string(index)},{"surface_kind",surface_kind(surface.GetType())},
    {"area_mm2",props.Mass()},{"center_mm",point(props.CentreOfMass())},{"bounds_mm",bounds(face)}};
  if(surface.GetType()==GeomAbs_Plane) {
    auto normal=surface.Plane().Axis().Direction();
    if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
    result["normal"]=direction(normal);
  }
  return result;
}
bool matches_face(const Json& descriptor,const Json& selector,const Json& parameters) {
  if(descriptor.at("surface_kind")!=selector.at("surface_kind"))return false;
  if(selector.contains("normal")) {
    if(!descriptor.contains("normal"))return false;
    const auto wanted=vector3(selector.at("normal").at("vector"),parameters,"dimensionless");
    const auto actual=descriptor.at("normal").get<std::array<double,3>>();
    const auto dot=(wanted[0]*actual[0]+wanted[1]*actual[1]+wanted[2]*actual[2])/std::hypot(wanted[0],wanted[1],wanted[2]);
    if(std::acos(std::clamp(dot,-1.0,1.0))>scalar(selector.at("normal").at("tolerance"),parameters,"rad"))return false;
  }
  if(selector.contains("center")) {
    const auto wanted=vector3(selector.at("center").at("point"),parameters);
    const auto actual=descriptor.at("center_mm").get<std::array<double,3>>();
    if(std::hypot(wanted[0]-actual[0],wanted[1]-actual[1],wanted[2]-actual[2])>scalar(selector.at("center").at("tolerance"),parameters))return false;
  }
  if(selector.contains("area")&&std::abs(descriptor.at("area_mm2").get<double>()-scalar(selector.at("area").at("value"),parameters,"mm2"))>
    scalar(selector.at("area").at("tolerance"),parameters,"mm2"))return false;
  return true;
}
std::vector<int> select_faces(const FeatureGeometry& source,const Json& selection,const Json& parameters,const std::string& input) {
  const auto selectors=selection.is_array()?selection:Json::array({selection});
  Json candidates=Json::array();std::vector<Json> descriptors;
  for(int i=1;i<=source.faces.Extent();++i) {
    descriptors.push_back(face_descriptor(TopoDS::Face(source.faces(i)),i));
    if(candidates.size()<64)candidates.push_back(descriptors.back());
  }
  std::vector<int> result;std::set<int> selected;
  for(std::size_t rule=0;rule<selectors.size();++rule) {
    const auto& selector=selectors[rule];std::vector<int> matches;
    for(int i=1;i<=source.faces.Extent();++i)if(matches_face(descriptors[i-1],selector,parameters))matches.push_back(i);
    const auto expected=selector.at("expected_count").get<std::size_t>();
    if(matches.size()!=expected) {
      Json matched=Json::array();for(const auto i:matches)if(matched.size()<64)matched.push_back(descriptors[i-1]);
      const auto code=matches.empty()?"selection_missing":matches.size()>expected?"selection_ambiguous":"selection_count_mismatch";
      throw Error(code,"Geometric selector did not match its expected face count",{{"source_feature_id",input},{"selector_index",rule},
        {"expected_count",expected},{"actual_count",matches.size()},{"matches",matched},{"candidates",candidates},{"candidates_truncated",source.faces.Extent()>64}});
    }
    for(const auto i:matches) {
      if(!selected.insert(i).second)throw Error("invalid_model","Face selection rules overlap; each source face must be selected once",{{"source_feature_id",input},{"selector_index",rule},{"source_id","face-"+std::to_string(i)}});
      result.push_back(i);
    }
  }
  return result;
}
TopoDS_Shape open_connected_patch(const std::vector<TopoDS_Face>& faces,const std::string& input) {
  TopoDS_Shell shell;BRep_Builder builder;builder.MakeShell(shell);
  ShapeMap edges;std::vector<int> uses,first_owner;std::vector<std::set<int>> neighbors(faces.size());
  for(std::size_t f=0;f<faces.size();++f) {
    builder.Add(shell,faces[f]);
    for(TopExp_Explorer it(faces[f],TopAbs_EDGE);it.More();it.Next()) {
      const auto edge=TopoDS::Edge(it.Current());if(BRep_Tool::Degenerated(edge))continue;
      const auto n=edges.Add(edge);
      if(static_cast<std::size_t>(n)>uses.size()){uses.push_back(0);first_owner.push_back(static_cast<int>(f));}
      ++uses[n-1];const auto other=first_owner[n-1];
      if(other!=static_cast<int>(f)){neighbors[f].insert(other);neighbors[other].insert(static_cast<int>(f));}
    }
  }
  std::set<int> visited;std::vector<int> pending{0};
  while(!pending.empty()) {const auto f=pending.back();pending.pop_back();if(!visited.insert(f).second)continue;for(auto other:neighbors[f])pending.push_back(other);}
  if(visited.size()!=faces.size())throw Error("invalid_model","Thicken requires one connected surface patch; selected faces are disconnected",{{"source_feature_id",input}});
  if(std::any_of(uses.begin(),uses.end(),[](int n){return n>2;}))throw Error("invalid_model","Thicken requires a manifold surface patch",{{"source_feature_id",input}});
  if(std::none_of(uses.begin(),uses.end(),[](int n){return n==1;}))throw Error("invalid_model","Thicken requires an open surface patch; use shell for a closed body",{{"source_feature_id",input}});
  if(!BRepCheck_Analyzer(shell).IsValid())throw Error("invalid_model","Selected thickening patch is not a valid shell",{{"source_feature_id",input}});
  return faces.size()==1?TopoDS_Shape(faces.front()):TopoDS_Shape(shell);
}
void check_parallel_material(const TopoDS_Shape& source,const TopoDS_Shape& result,double distance,
                             bool shell_or_thicken,const std::string& input) {
  // Offset algorithms can invert a collapsed cavity into a valid body outside
  // its source. Check exact material containment as well as B-rep validity.
  const auto source_shape=distance>0?source:result;
  const auto result_shape=distance>0?result:source;
  const auto forbidden=[&](auto& operation,const TopoDS_Shape& left,const TopoDS_Shape& right) {
    NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(left);tools.Append(right);
    operation.SetArguments(arguments);operation.SetTools(tools);operation.SetNonDestructive(true);operation.SetRunParallel(false);operation.Build();
    if(!operation.IsDone()||operation.HasErrors())throw Error("kernel_failure","Could not verify parallel-boundary material intent",{{"source_feature_id",input}});
    ShapeMap material;TopExp::MapShapes(operation.Shape(),TopAbs_SOLID,material);
    for(int i=1;i<=material.Extent();++i) {
      GProp_GProps properties;BRepGProp::VolumeProperties(material(i),properties);
      if(properties.Mass()>0)throw Error("invalid_shape","Parallel boundary crossed or inverted beyond its requested side of the source",{{"source_feature_id",input},{"unexpected_volume_mm3",properties.Mass()}});
    }
  };
  if(shell_or_thicken&&distance>0) {
    BRepAlgoAPI_Common overlap;forbidden(overlap,source,result);
  } else {
    BRepAlgoAPI_Cut escaped;forbidden(escaped,source_shape,result_shape);
  }
}
void topology_limit(const FeatureGeometry& feature, const QueryLimits& limits) {
  const auto requested = static_cast<std::size_t>(feature.faces.Extent()) + feature.edges.Extent();
  const auto maximum = std::min<std::size_t>(limits.topology_entities, 10000);
  if (requested > maximum) throw Error("limit_exceeded", "Topology enumeration exceeds its entity limit", {{"limit", maximum}, {"requested", requested}});
}
template<class Operation>
void record_history(Operation& operation, const FeatureGeometry& source, const std::string& source_id,
                    const FeatureGeometry& target, Json& history, bool& truncated,
                    BRepBuilderAPI_Copy* copy = nullptr, int instance_index = -1, const TopoDS_Shape* source_scope = nullptr) {
  if (truncated) return;
  const auto append = [&](Json item) {
    if (history.size() >= 10000) { truncated=true; return false; }
    history.push_back(std::move(item));
    return true;
  };
  for (const auto& [kind, entities] : std::initializer_list<std::pair<std::string,const ShapeMap*>>{{"face",&source.faces},{"edge",&source.edges}}) {
    ShapeMap scope;
    if(source_scope)TopExp::MapShapes(*source_scope,kind=="face"?TopAbs_FACE:TopAbs_EDGE,scope);
    for (int i=1; i<=entities->Extent(); ++i) {
      const auto original=(*entities)(i);
      const auto entity=copy ? copy->ModifiedShape(original) : original;
      if(source_scope&&!scope.Contains(entity))continue;
      Json entry={{"source_feature_id",source_id},{"source_kind",kind},{"source_id",kind+"-"+std::to_string(i)}};
      if (instance_index >= 0) entry["instance_index"]=instance_index;
      if (operation.IsDeleted(entity)) { auto deleted=entry; deleted["relation"]="deleted"; if (!append(deleted)) return; }
      for (const auto& relation : {std::string("unchanged"),std::string("modified"),std::string("generated")}) {
        NCollection_List<TopoDS_Shape> descendants;
        if (relation == "unchanged") descendants.Append(entity);
        else descendants = relation == "modified" ? operation.Modified(entity) : operation.Generated(entity);
        for (const auto& descendant : descendants) {
          const int face=target.faces.FindIndex(descendant), edge=target.edges.FindIndex(descendant);
          if (!face && !edge) continue;
          auto item=entry;
          item["relation"]=relation; item["result_kind"]=face ? "face" : "edge";
          item["result_id"]=(face ? "face-" : "edge-")+std::to_string(face ? face : edge);
          if (!append(item)) return;
        }
      }
    }
  }
}
template<class Operation>
void record_history(Operation& operation, const FeatureGeometry& source, const std::string& source_id,
                    const TopoDS_Shape& result, Json& history, bool& truncated, BRepBuilderAPI_Copy* copy = nullptr) {
  if (!truncated) record_history(operation,source,source_id,FeatureGeometry(result),history,truncated,copy);
}
gp_Pnt parameter_point(const Json& value, const Json& parameters) {
  const auto p = vector3(value, parameters);
  return gp_Pnt(p[0],p[1],p[2]);
}
gp_Dir parameter_direction(const Json& value, const Json& parameters) {
  const auto p = vector3(value, parameters, "dimensionless");
  return gp_Dir(p[0],p[1],p[2]);
}
gp_Ax2 parameter_plane(const Json& value, const Json& parameters) {
  return gp_Ax2(parameter_point(value.at("origin"),parameters), parameter_direction(value.at("normal"),parameters), parameter_direction(value.at("x_direction"),parameters));
}
// Express a Bezier extrusion as an identical rational B-spline
// surface. Every boundary curve and its UV parameterization remains unchanged.
// This avoids OCCT's aborting swept-Bezier intersection path without fitting,
// healing, increasing tolerances, or converting the analytic bend cylinders.
class SheetBezierExtrusions final : public BRepTools_Modification {
 public:
  bool NewSurface(const TopoDS_Face& face,occ::handle<Geom_Surface>& result,TopLoc_Location& location,double& tolerance,bool& reverse_wires,bool& reverse_face) override {
    auto surface=BRep_Tool::Surface(face,location);
    while(const auto trimmed=occ::down_cast<Geom_RectangularTrimmedSurface>(surface))surface=trimmed->BasisSurface();
    const auto extrusion=occ::down_cast<Geom_SurfaceOfLinearExtrusion>(surface);if(extrusion.IsNull())return false;
    const auto source=occ::down_cast<Geom_BezierCurve>(extrusion->BasisCurve());if(source.IsNull())return false;
    double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
    if(!std::isfinite(u0)||!std::isfinite(u1)||!std::isfinite(v0)||!std::isfinite(v1)||u1<=u0||v1<=v0)throw Error("invalid_shape","Swept-Bezier face requires finite nonempty parameter ranges");
    auto curve=occ::down_cast<Geom_BezierCurve>(source->Copy());curve->Segment(u0,u1);
    const auto n=curve->NbPoles();NCollection_Array2<gp_Pnt> poles(1,n,1,2);NCollection_Array2<double> weights(1,n,1,2);
    for(int u=1;u<=n;++u)for(int v=1;v<=2;++v){poles.SetValue(u,v,curve->Pole(u).Translated(gp_Vec(extrusion->Direction())*(v==1?v0:v1)));weights.SetValue(u,v,curve->Weight(u));}
    NCollection_Array1<double> uk(1,2),vk(1,2);uk.SetValue(1,u0);uk.SetValue(2,u1);vk.SetValue(1,v0);vk.SetValue(2,v1);
    NCollection_Array1<int> um(1,2),vm(1,2);um.Init(n);vm.Init(2);
    occ::handle<Geom_BSplineSurface> spline=new Geom_BSplineSurface(poles,weights,uk,vk,um,vm,n-1,1,false,false);
    // Exact knot insertion supplies stable integration spans; the original
    // swept-Bezier and one-span quadratures otherwise underresolve curved walls.
    if(n>=3)for(int i=1;i<8;++i)spline->InsertUKnot(u0+(u1-u0)*i/8,1,1e-12);
    result=spline;
    // The coefficient construction proves identity; these evaluations catch
    // an incorrect domain/location assumption before the native shape checker.
    for(int i=0;i<=4;++i)for(int j=0;j<=2;++j){const auto u=u0+(u1-u0)*i/4,v=v0+(v1-v0)*j/2;if(surface->Value(u,v).Distance(result->Value(u,v))>1e-8)throw Error("invalid_shape","Swept-Bezier basis conversion changed its parameterized surface");}
    tolerance=BRep_Tool::Tolerance(face);reverse_wires=false;reverse_face=false;return true;
  }
  bool NewCurve(const TopoDS_Edge&,occ::handle<Geom_Curve>&,TopLoc_Location&,double&) override {return false;}
  bool NewPoint(const TopoDS_Vertex&,gp_Pnt&,double&) override {return false;}
  bool NewCurve2d(const TopoDS_Edge& edge,const TopoDS_Face& face,const TopoDS_Edge&,const TopoDS_Face&,occ::handle<Geom2d_Curve>& curve,double& tolerance) override {
    BRepAdaptor_Surface surface(face);if(surface.GetType()!=GeomAbs_SurfaceOfExtrusion||surface.BasisCurve()->GetType()!=GeomAbs_BezierCurve)return false;
    double first,last;curve=BRep_Tool::CurveOnSurface(edge,face,first,last);if(curve.IsNull())throw Error("invalid_shape","Swept-Bezier boundary has no source pcurve");
    curve=occ::down_cast<Geom2d_Curve>(curve->Copy());tolerance=BRep_Tool::Tolerance(edge);return true;
  }
  bool NewParameter(const TopoDS_Vertex&,const TopoDS_Edge&,double&,double&) override {return false;}
  GeomAbs_Shape Continuity(const TopoDS_Edge& edge,const TopoDS_Face& f1,const TopoDS_Face& f2,const TopoDS_Edge&,const TopoDS_Face&,const TopoDS_Face&) override {return BRep_Tool::Continuity(edge,f1,f2);}
};
struct SheetFrame {gp_Pnt start;gp_Dir axis,outward,normal;};
struct SheetCut {double offset,width,from,to;};
struct SheetFlange {
  std::string id,parent,attachment;
  SheetFrame formed,flat;
  double width,radius,angle,length,allowance,miter_start=0,miter_end=0;
  bool fold=false;
  std::vector<SheetCut> cuts;
};
struct SheetPlan {
  TopoDS_Shape face;
  gp_Dir normal;
  double thickness,k_factor,base_area;
  std::vector<SheetFlange> flanges;
};
gp_Pnt sheet_point(const SheetFrame& frame,double x,double y,double z=0) {
  return frame.start.Translated(gp_Vec(frame.outward)*x+gp_Vec(frame.normal)*y+gp_Vec(frame.axis)*z);
}
TopoDS_Shape sheet_prism(const TopoDS_Shape& profile,const gp_Vec& direction) {
  BRepPrimAPI_MakePrism prism(profile,direction,true);
  if(!prism.IsDone())throw Error("kernel_failure","Sheet-metal region extrusion failed");
  const auto shape=prism.Shape();check_shape(shape);return shape;
}
TopoDS_Face sheet_polygon(const std::vector<gp_Pnt>& points) {
  BRepBuilderAPI_MakePolygon polygon;for(const auto& p:points)polygon.Add(p);polygon.Close();
  if(!polygon.IsDone())throw Error("kernel_failure","Sheet-metal boundary construction failed");
  BRepBuilderAPI_MakeFace face(polygon.Wire(),true);
  if(!face.IsDone())throw Error("kernel_failure","Sheet-metal planar region construction failed");
  return face.Face();
}
double sheet_area(const TopoDS_Shape& shape) {GProp_GProps p;BRepGProp::SurfaceProperties(shape,p,1e-9);return p.Mass();}
TopoDS_Face sheet_rectangle(const SheetFrame& frame,double from,double to,double offset,double width) {
  return sheet_polygon({sheet_point(frame,from,0,offset),sheet_point(frame,to,0,offset),sheet_point(frame,to,0,offset+width),sheet_point(frame,from,0,offset+width)});
}
TopoDS_Shape sheet_subtract(const TopoDS_Shape& source,const TopoDS_Shape& tool) {
  BRepAlgoAPI_Cut operation;NCollection_List<TopoDS_Shape> a,b;a.Append(source);b.Append(tool);
  operation.SetArguments(a);operation.SetTools(b);operation.SetNonDestructive(true);operation.SetRunParallel(false);operation.Build();
  if(!operation.IsDone()||operation.HasErrors())throw Error("kernel_failure","Sheet-metal intent cut failed");
  return operation.Shape();
}
SheetFrame sheet_leg_frame(const SheetFlange& flange,double thickness,bool flat) {
  if(flat){auto frame=flange.flat;frame.start=sheet_point(frame,flange.allowance,0);return frame;}
  const auto angle=std::abs(flange.angle),sign=flange.angle>0?1.0:-1.0;
  const auto center=sign>0?flange.radius:-flange.radius-thickness;
  const auto top_radius=sign>0?flange.radius:flange.radius+thickness;
  return {sheet_point(flange.formed,top_radius*std::sin(angle),center-sign*top_radius*std::cos(angle)),flange.formed.axis,
    gp_Dir(gp_Vec(flange.formed.outward)*std::cos(angle)+gp_Vec(flange.formed.normal)*sign*std::sin(angle)),
    gp_Dir(gp_Vec(flange.formed.normal)*std::cos(angle)-gp_Vec(flange.formed.outward)*sign*std::sin(angle))};
}
SheetFrame sheet_attachment(const SheetFlange& parent,double thickness,bool flat,const std::string& attachment,double& width) {
  const auto leg=sheet_leg_frame(parent,thickness,flat);
  gp_Pnt start,end;
  if(attachment=="tip"){start=sheet_point(leg,parent.length,0,parent.length*parent.miter_start);end=sheet_point(leg,parent.length,0,parent.width-parent.length*parent.miter_end);}
  else if(attachment=="start"){start=sheet_point(leg,0,0);end=sheet_point(leg,parent.length,0,parent.length*parent.miter_start);}
  else {start=sheet_point(leg,parent.length,0,parent.width-parent.length*parent.miter_end);end=sheet_point(leg,0,0,parent.width);}
  const auto axis=gp_Dir(gp_Vec(start,end));width=start.Distance(end);
  return {start,axis,gp_Dir(gp_Vec(axis).Crossed(gp_Vec(leg.normal))),leg.normal};
}
SheetPlan sheet_plan(const Json& feature,const Json& parameters,const FeatureGeometry& source) {
  const auto input=text_field(feature,"input");
  if(source.faces.Extent()!=1)throw Error("invalid_model","Sheet metal requires one connected planar profile region",{{"source_feature_id",input},{"face_count",source.faces.Extent()}});
  const auto face=TopoDS::Face(source.faces(1));BRepAdaptor_Surface surface(face);
  if(surface.GetType()!=GeomAbs_Plane)throw Error("invalid_model","Sheet-metal base profile must be planar",{{"source_feature_id",input}});
  auto normal=surface.Plane().Axis().Direction();if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
  SheetPlan result{face,normal,scalar(feature.at("thickness"),parameters),scalar(feature.at("k_factor"),parameters,"dimensionless"),sheet_area(face),{}};
  ShapeMap outer;TopExp::MapShapes(BRepTools::OuterWire(face),TopAbs_EDGE,outer);std::set<int> selected;std::set<std::string> attachments;
  for(const auto& flange:feature.at("flanges")) {
    const auto id=text_field(flange,"id");
    try {
      SheetFrame frame;SheetFrame flat;double edge_length=0;
      const auto parent_id=flange.value("parent",std::string()),attachment=flange.value("attachment",std::string("tip"));
      if(!parent_id.empty()) {
        const auto found=std::find_if(result.flanges.begin(),result.flanges.end(),[&](const auto& f){return f.id==parent_id;});
        if(found==result.flanges.end())throw Error("invalid_model","Missing parent bend");
        if(!attachments.insert(parent_id+":"+attachment).second)throw Error("invalid_model","A parent attachment edge can carry only one bend");
        for(const auto& cut:found->cuts)if((attachment=="tip"&&cut.to>=found->allowance+found->length-1e-7)||
          (attachment=="start"&&cut.offset<=1e-7&&cut.to>found->allowance)||(attachment=="end"&&cut.offset+cut.width>=found->width-1e-7&&cut.to>found->allowance))throw Error("invalid_model","A parent attachment crosses a removed cut");
        frame=sheet_attachment(*found,result.thickness,false,attachment,edge_length);flat=sheet_attachment(*found,result.thickness,true,attachment,edge_length);
      }else if(flange.contains("fold_line")) {
        const auto start=parameter_point(flange.at("fold_line")[0],parameters),end=parameter_point(flange.at("fold_line")[1],parameters);
        edge_length=start.Distance(end);if(edge_length<1e-5)throw Error("invalid_model","Fold-line endpoints must differ");
        if(surface.Plane().Distance(start)>1e-7||surface.Plane().Distance(end)>1e-7)throw Error("invalid_model","Fold line must lie in the source sketch plane");
        const auto axis=gp_Dir(gp_Vec(start,end));frame={start,axis,gp_Dir(gp_Vec(axis).Crossed(gp_Vec(normal))),normal};flat=frame;
      }else {
        const auto& selector=flange.at("edge");std::vector<int> matches;Json candidates=Json::array();
        for(int i=1;i<=source.edges.Extent();++i) {const auto descriptor=edge_descriptor(TopoDS::Edge(source.edges(i)),i);if(candidates.size()<64)candidates.push_back(descriptor);if(agentcad::matches(descriptor,selector,parameters))matches.push_back(i);}
        if(matches.size()!=1)throw Error(matches.empty()?"selection_missing":"selection_ambiguous","Sheet flange must resolve one source profile edge",{{"source_feature_id",input},{"actual_count",matches.size()},{"candidates",candidates},{"candidates_truncated",source.edges.Extent()>64}});
        const auto index=matches.front(),outer_index=outer.FindIndex(source.edges(index));
        if(!outer_index)throw Error("invalid_model","Sheet flanges require an outer boundary edge; interior holes cannot carry a flange",{{"source_feature_id",input}});
        if(!selected.insert(index).second)throw Error("invalid_model","Two flanges cannot reuse the same source edge",{{"source_feature_id",input}});
        const auto edge=TopoDS::Edge(outer(outer_index));TopoDS_Vertex first,last;TopExp::Vertices(edge,first,last,true);
        auto start=BRep_Tool::Pnt(first);const auto end=BRep_Tool::Pnt(last);auto axis=gp_Dir(gp_Vec(start,end));auto outward=gp_Dir(gp_Vec(axis).Crossed(gp_Vec(normal)));edge_length=start.Distance(end);
        const auto center=start.Translated(gp_Vec(start,end)*.5);bool oriented=false;double step=std::min({.001,edge_length*.01,result.thickness*.1});
        for(int attempt=0;attempt<8&&step>1e-7;++attempt,step*=.25) {
          BRepClass_FaceClassifier outside(face,center.Translated(gp_Vec(outward)*step),1e-7),inside(face,center.Translated(gp_Vec(outward)*-step),1e-7);
          if(outside.State()==TopAbs_OUT&&inside.State()==TopAbs_IN){oriented=true;break;}
          if(outside.State()==TopAbs_IN&&inside.State()==TopAbs_OUT){outward.Reverse();axis.Reverse();start=end;oriented=true;break;}
        }
        if(!oriented)throw Error("invalid_model","Could not establish the material side of the selected sheet edge",{{"source_feature_id",input}});
        frame={start,axis,outward,normal};flat=frame;
      }
      auto start_gap=flange.contains("start_gap")?scalar(flange.at("start_gap"),parameters):0.0;
      auto end_gap=flange.contains("end_gap")?scalar(flange.at("end_gap"),parameters):0.0;
      double relief_width=0,relief_depth=0;
      if(flange.contains("relief")){relief_width=scalar(flange.at("relief").at("width"),parameters);relief_depth=scalar(flange.at("relief").at("depth"),parameters);start_gap+=relief_width;end_gap+=relief_width;}
      const auto width=edge_length-start_gap-end_gap;if(width<1e-5)throw Error("invalid_model","Flange gaps and relief consume its entire attachment",{{"edge_length_mm",edge_length}});
      frame.start.Translate(gp_Vec(frame.axis)*start_gap);flat.start.Translate(gp_Vec(flat.axis)*start_gap);
      const auto radius=scalar(flange.at("inside_radius"),parameters),angle=scalar(flange.at("angle_deg"),parameters,"deg")*std::numbers::pi/180;
      const auto length=scalar(flange.at("length"),parameters),allowance=std::abs(angle)*(radius+result.k_factor*result.thickness);
      SheetFlange planned{id,parent_id,attachment,frame,flat,width,radius,angle,length,allowance,0,0,false,{}};planned.fold=flange.contains("fold_line");
      if(flange.contains("miter")){planned.miter_start=std::tan(scalar(flange.at("miter").at("start_deg"),parameters,"deg")*std::numbers::pi/180);planned.miter_end=std::tan(scalar(flange.at("miter").at("end_deg"),parameters,"deg")*std::numbers::pi/180);if(length*(planned.miter_start+planned.miter_end)>=width-1e-5)throw Error("invalid_model","Miter consumes the complete flange tip");}
      if(planned.fold) {
        // The authored line and length identify a complete rectangular developed tab.
        // Its area must exist in the original blank; holes/cutouts are separate mapped cut intent.
        const auto rectangle=sheet_rectangle(flat,0,allowance+length,0,width);
        const auto trimmed=sheet_subtract(result.face,rectangle);const auto removed=sheet_area(result.face)-sheet_area(trimmed);
        if(std::abs(removed-width*(allowance+length))>1e-7*std::max(1.0,width*(allowance+length)))throw Error("invalid_model","Fold region must be fully contained and unperforated in the existing blank",{{"required_area_mm2",width*(allowance+length)},{"available_area_mm2",removed}});
        result.face=trimmed;
      }
      if(relief_width>0)for(const auto offset:{-relief_width,width})result.face=sheet_subtract(result.face,sheet_rectangle(flat,-relief_depth,1e-7,offset,relief_width));
      if(flange.contains("cuts"))for(const auto& cut:flange.at("cuts")) {
        SheetCut c{scalar(cut.at("offset"),parameters),scalar(cut.at("width"),parameters),scalar(cut.at("from"),parameters),scalar(cut.at("to"),parameters)};
        if(c.offset+c.width>width+1e-7||c.to>allowance+length+1e-7)throw Error("invalid_model","Developed cut exceeds its named flange region");
        const auto leg_to=std::max(0.0,c.to-allowance);
        if(c.offset<leg_to*planned.miter_start-1e-7||c.offset+c.width>width-leg_to*planned.miter_end+1e-7)throw Error("invalid_model","A rectangular cut crosses its flange miter");
        for(const auto& previous:planned.cuts)if(std::min(c.to,previous.to)-std::max(c.from,previous.from)>1e-7&&std::min(c.offset+c.width,previous.offset+previous.width)-std::max(c.offset,previous.offset)>1e-7)throw Error("invalid_model","Developed cuts must not overlap");
        planned.cuts.push_back(c);
      }
      result.flanges.push_back(planned);
    }catch(const Error& e){auto details=e.details;details["flange_id"]=id;throw Error(e.code,e.what(),details);}
  }
  result.base_area=sheet_area(result.face);if(result.base_area<1e-5)throw Error("invalid_model","Fold and relief intent must leave a stationary base region");
  return result;
}
TopoDS_Shape join_sheet_region(const TopoDS_Shape& source,const TopoDS_Shape& addition,const std::string& flange_id) {
  BRepAlgoAPI_Common overlap;NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(source);tools.Append(addition);
  overlap.SetArguments(arguments);overlap.SetTools(tools);overlap.SetNonDestructive(true);overlap.SetRunParallel(false);overlap.Build();
  if(!overlap.IsDone()||overlap.HasErrors())throw Error("kernel_failure","Sheet-metal collision check failed",{{"flange_id",flange_id}});
  GProp_GProps interference;BRepGProp::VolumeProperties(overlap.Shape(),interference,1e-9);
  if(interference.Mass()>1e-8)throw Error("invalid_model","Sheet-metal bend or flange collides with existing material",{{"flange_id",flange_id},{"intersection_volume_mm3",interference.Mass()}});
  BRepAlgoAPI_Fuse operation;operation.SetArguments(arguments);operation.SetTools(tools);operation.SetNonDestructive(true);operation.SetRunParallel(false);operation.Build();
  if(!operation.IsDone()||operation.HasErrors())throw Error("kernel_failure","Sheet-metal region fuse failed",{{"flange_id",flange_id}});
  const auto shape=operation.Shape();check_shape(shape);
  if(count(shape,TopAbs_SOLID)!=1)throw Error("invalid_shape","Sheet-metal regions must form one connected solid",{{"flange_id",flange_id}});
  return shape;
}
TopoDS_Shape sheet_bend_region(const SheetFlange& flange,double thickness,double from,double to,double offset,double width) {
  const auto sign=flange.angle>0?1.0:-1.0,center=sign>0?flange.radius:-flange.radius-thickness;
  const auto arcpoint=[&](double radius,double theta){return sheet_point(flange.formed,radius*std::sin(theta),center-sign*radius*std::cos(theta),offset);};
  const auto inner_start=arcpoint(flange.radius,from),inner_end=arcpoint(flange.radius,to),outer_start=arcpoint(flange.radius+thickness,from),outer_end=arcpoint(flange.radius+thickness,to);
  GC_MakeArcOfCircle inner(inner_start,arcpoint(flange.radius,(from+to)*.5),inner_end),outer(outer_end,arcpoint(flange.radius+thickness,(from+to)*.5),outer_start);
  if(!inner.IsDone()||!outer.IsDone())throw Error("kernel_failure","Sheet-metal bend arcs failed");
  BRepBuilderAPI_MakeWire wire;wire.Add(BRepBuilderAPI_MakeEdge(inner.Value()).Edge());wire.Add(BRepBuilderAPI_MakeEdge(inner_end,outer_end).Edge());wire.Add(BRepBuilderAPI_MakeEdge(outer.Value()).Edge());wire.Add(BRepBuilderAPI_MakeEdge(outer_start,inner_start).Edge());
  if(!wire.IsDone())throw Error("kernel_failure","Sheet-metal bend wire failed");BRepBuilderAPI_MakeFace cross_section(wire.Wire(),true);if(!cross_section.IsDone())throw Error("kernel_failure","Sheet-metal annular sector failed");
  return sheet_prism(cross_section.Face(),gp_Vec(flange.formed.axis)*width);
}
std::vector<TopoDS_Shape> sheet_regions(const SheetFlange& flange,double thickness,bool flat) {
  std::vector<TopoDS_Shape> regions;
  if(flat)regions.push_back(sheet_prism(sheet_rectangle(flange.flat,0,flange.allowance,0,flange.width),gp_Vec(flange.flat.normal)*-thickness));
  else regions.push_back(sheet_bend_region(flange,thickness,0,std::abs(flange.angle),0,flange.width));
  const auto leg=sheet_leg_frame(flange,thickness,flat);
  regions.push_back(sheet_prism(sheet_polygon({sheet_point(leg,0,0),sheet_point(leg,flange.length,0,flange.length*flange.miter_start),
    sheet_point(leg,flange.length,0,flange.width-flange.length*flange.miter_end),sheet_point(leg,0,0,flange.width)}),gp_Vec(leg.normal)*-thickness));
  // Map cuts by neutral-axis arclength. A crossing cut becomes an exact annular
  // sector and a planar prism, sharing the physical tangent boundary.
  for(const auto& cut:flange.cuts) {
    if(cut.from<flange.allowance-1e-8) {
      const auto end=std::min(cut.to,flange.allowance);
      const auto tool=flat?sheet_prism(sheet_rectangle(flange.flat,cut.from,end,cut.offset,cut.width),gp_Vec(flange.flat.normal)*-thickness):
        sheet_bend_region(flange,thickness,cut.from/flange.allowance*std::abs(flange.angle),end/flange.allowance*std::abs(flange.angle),cut.offset,cut.width);
      regions[0]=sheet_subtract(regions[0],tool);
    }
    if(cut.to>flange.allowance+1e-8)regions[1]=sheet_subtract(regions[1],sheet_prism(sheet_rectangle(leg,std::max(0.0,cut.from-flange.allowance),cut.to-flange.allowance,cut.offset,cut.width),gp_Vec(leg.normal)*-thickness));
  }
  return regions;
}
Json sheet_report(const SheetPlan& plan,const std::string& source,bool flat) {
  Json bends=Json::array();auto flat_area=plan.base_area,formed_volume=plan.base_area*plan.thickness;
  for(const auto& flange:plan.flanges) {
    const auto allowance=flange.allowance,geometric_allowance=std::abs(flange.angle)*(flange.radius+.5*plan.thickness);
    const auto leg_area=flange.width*flange.length-.5*flange.length*flange.length*(flange.miter_start+flange.miter_end);
    flat_area+=flange.width*allowance+leg_area;formed_volume+=(flange.width*geometric_allowance+leg_area)*plan.thickness;
    for(const auto& cut:flange.cuts){flat_area-=cut.width*(cut.to-cut.from);const auto bend_length=std::max(0.0,std::min(cut.to,allowance)-cut.from),leg_length=std::max(0.0,cut.to-std::max(cut.from,allowance));formed_volume-=cut.width*plan.thickness*(bend_length*geometric_allowance/allowance+leg_length);}
    const auto line=[&](double x){return Json::array({point(sheet_point(flange.flat,x,0)),point(sheet_point(flange.flat,x,0,flange.width))});};
    bends.push_back({{"id",flange.id},{"width_mm",flange.width},{"inside_radius_mm",flange.radius},{"angle_deg",flange.angle*180/std::numbers::pi},
      {"straight_length_mm",flange.length},{"bend_allowance_mm",allowance},{"bend_deduction_mm",std::abs(std::abs(flange.angle)-std::numbers::pi)<1e-9?Json(nullptr):Json(2*(flange.radius+plan.thickness)*std::tan(std::abs(flange.angle)*.5)-allowance)},
      {"bend_start_line_mm",line(0)},{"bend_center_line_mm",line(allowance*.5)},{"bend_end_line_mm",line(allowance)}});
  }
  return {{"mode",flat?"flat":"formed"},{"source_feature_id",source},{"thickness_mm",plan.thickness},{"k_factor",plan.k_factor},
    {"neutral_axis_basis","caller_supplied_k_factor"},{"flat_area_mm2",flat_area},{"flat_volume_mm3",flat_area*plan.thickness},{"formed_volume_mm3",formed_volume},
    {"volume_preservation_assumed",false},{"bends",bends}};
}
gp_Trsf placement_transform(const Json& value, const Json& parameters) {
  gp_Trsf result;
  if (value.contains("rotation")) {
    const auto& rotation=value.at("rotation");
    result.SetRotation(gp_Ax1(parameter_point(rotation.at("origin"),parameters),parameter_direction(rotation.at("axis"),parameters)),
      scalar(rotation.at("angle_deg"),parameters,"deg")*std::numbers::pi/180);
  }
  if (value.contains("translation")) {
    const auto delta=vector3(value.at("translation"),parameters);
    gp_Trsf translation; translation.SetTranslation(gp_Vec(delta[0],delta[1],delta[2]));
    result=translation*result;
  }
  return result;
}
gp_Trsf local_frame(const Json& value,const Json& parameters) {
  // SetTransformation maps world coordinates into the frame; inversion gives
  // the local-to-world placement used in the mate composition below.
  gp_Trsf result; result.SetTransformation(gp_Ax3(parameter_plane(value,parameters)));
  result.Invert(); return result;
}
Json matrix(const gp_Trsf& transform) {
  Json result=Json::array();
  for (int row=1;row<=3;++row) for (int column=1;column<=4;++column) result.push_back(transform.Value(row,column));
  for (const auto value:{0,0,0,1}) result.push_back(value);
  return result;
}
struct AssemblyPlan {
  std::vector<AssemblyPart> parts;
  std::map<std::string,gp_Trsf> placements;
  Json tree=Json::array(),mates=Json::array(),motion;
};
AssemblyPlan assembly_plan(const Json& feature,const Json& parameters,
                           const std::map<std::string,FeatureGeometry>& sources) {
  const auto& parts=feature.at("parts");
  const auto motion=assembly_motion(feature,parameters);
  std::map<std::string,Json> coordinates;
  for (const auto& dof:motion.at("dofs")) coordinates[text_field(dof,"mate_id")][text_field(dof,"coordinate")]=dof.at("value");
  std::size_t replicated_solids=0,replicated_faces=0;
  for (const auto& part:parts) {
    const auto& source=sources.at(text_field(part,"input"));
    replicated_solids+=count(source.shape,TopAbs_SOLID); replicated_faces+=source.faces.Extent();
  }
  replication_budget(replicated_solids,replicated_faces);
  std::map<std::string,const Json*> incoming;
  if (feature.contains("mates")) for (const auto& mate:feature.at("mates")) incoming.emplace(text_field(mate,"child"),&mate);
  std::map<std::string,gp_Trsf> transforms;
  // The document validates a forest. Resolve parents first independently of
  // the serialization order of either the parts or the mates.
  while (transforms.size()<parts.size()) {
    bool progress=false;
    for (const auto& part:parts) {
      const auto id=text_field(part,"id");
      if (transforms.contains(id)) continue;
      const auto relation=incoming.find(id);
      if (relation==incoming.end()) {
        transforms.emplace(id,placement_transform(part.value("placement",Json::object()),parameters));
      } else {
        const auto& mate=*relation->second;
        const auto parent=transforms.find(text_field(mate,"parent"));
        if (parent==transforms.end()) continue;
        try {
          gp_Trsf offset,rotation,travel;
          if (mate.contains("offset")) {
            const auto delta=vector3(mate.at("offset"),parameters);
            offset.SetTranslation(gp_Vec(delta[0],delta[1],delta[2]));
          }
          const auto mate_id=text_field(mate,"id");
          const auto found=coordinates.find(mate_id);
          const auto value=[&](const char* key,const char* unit) {
            if (found!=coordinates.end() && found->second.contains(key)) return found->second.at(key).get<double>();
            return scalar(mate.value(key,Json(0)),parameters,unit);
          };
          rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)),value("angle_deg","deg")*std::numbers::pi/180);
          travel.SetTranslation(gp_Vec(0,0,value("travel_mm","mm")));
          transforms.emplace(id,parent->second*local_frame(mate.at("parent_frame"),parameters)*offset*rotation*travel*
            local_frame(mate.at("child_frame"),parameters).Inverted());
        } catch (const Standard_Failure& e) {
          throw occt_error(e,{{"part_id",id},{"mate_id",mate.at("id")}});
        }
      }
      progress=true;
    }
    if (!progress) throw Error("invalid_model","Assembly mate graph cannot be resolved");
  }
  AssemblyPlan plan;plan.placements=transforms;
  if(!motion.at("dofs").empty())plan.motion=motion;
  for(const auto& part:parts) {
    const auto id=text_field(part,"id"),input=text_field(part,"input");
    const auto& source=sources.at(input);
    plan.tree.push_back({{"id",id},{"input",input},{"assembly_id",feature.at("id")},{"parent_id",""},
      {"kind",source.parts.empty()?"part":"assembly"}});
    for(auto node:source.tree) {
      node["id"]=id+"/"+text_field(node,"id");
      node["parent_id"]=text_field(node,"parent_id").empty()?id:id+"/"+text_field(node,"parent_id");
      plan.tree.push_back(std::move(node));
    }
    if(source.parts.empty())plan.parts.push_back({id,input,transforms.at(id),{}});
    else for(const auto& leaf:source.parts)
      plan.parts.push_back({id+"/"+leaf.id,leaf.input,transforms.at(id)*leaf.transform,{}});
  }
  if(feature.contains("mates"))for(const auto& mate:feature.at("mates"))
    plan.mates.push_back({{"id",mate.at("id")},{"type",mate.at("type")},{"parent",mate.at("parent")},{"child",mate.at("child")}});
  return plan;
}
Json assembly_parts(const std::vector<AssemblyPart>& parts) {
  Json result=Json::array();
  for(const auto& part:parts)result.push_back({{"id",part.id},{"input",part.input},{"transform",matrix(part.transform)}});
  return result;
}
void assembly_ownership(FeatureGeometry& result,const std::string& code) {
  result.face_parts.resize(result.faces.Extent()+1);result.edge_parts.resize(result.edges.Extent()+1);
  for(const auto& part:result.parts) {
    const FeatureGeometry geometry(part.shape);
    for(const auto& [source,target,owners]:std::initializer_list<std::tuple<const ShapeMap*,const ShapeMap*,std::vector<std::string>*>>{
        {&geometry.faces,&result.faces,&result.face_parts},{&geometry.edges,&result.edges,&result.edge_parts}}) {
      for(int i=1;i<=source->Extent();++i) {
        const int index=target->FindIndex((*source)(i));
        if(!index||!(*owners)[index].empty())throw Error(code,"Assembly topology ownership is ambiguous",{{"part_id",part.id}});
        (*owners)[index]=part.id;
      }
    }
  }
  for(const auto* owners:{&result.face_parts,&result.edge_parts})
    for(std::size_t i=1;i<owners->size();++i)if((*owners)[i].empty())throw Error(code,"Assembly topology has no owning part");
}
FeatureGeometry assemble_geometry(const TopoDS_Shape& shape,AssemblyPlan plan,const std::string& code="kernel_failure") {
  FeatureGeometry result(shape);result.parts=std::move(plan.parts);
  result.placements=std::move(plan.placements);result.tree=std::move(plan.tree);
  result.motion=std::move(plan.motion);result.mates=std::move(plan.mates);
  assembly_ownership(result,code);return result;
}
FeatureGeometry build_assembly(const Json& feature,const Json& parameters,
                               const std::map<std::string,FeatureGeometry>& sources,
                               Json& history,bool& history_truncated) {
  auto plan=assembly_plan(feature,parameters,sources);
  BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
  std::vector<std::unique_ptr<BRepBuilderAPI_Transform>> operations;
  for(auto& part:plan.parts) {
    try {
      auto operation=std::make_unique<BRepBuilderAPI_Transform>(sources.at(part.input).shape,part.transform,true);
      if(!operation->IsDone())throw Error("kernel_failure","Assembly part placement failed");
      check_shape(operation->Shape());part.shape=operation->Shape();builder.Add(compound,part.shape);
      operations.push_back(std::move(operation));
    }catch(const Error& error){auto details=error.details;details["part_id"]=part.id;throw Error(error.code,error.what(),details);}
    catch(const Standard_Failure& error){throw occt_error(error,{{"part_id",part.id}});}
  }
  check_shape(compound);auto result=assemble_geometry(compound,std::move(plan));
  for(std::size_t p=0;p<result.parts.size();++p) {
    const auto& part=result.parts[p];const auto start=history.size();
    record_history(*operations[p],sources.at(part.input),part.input,result,history,history_truncated);
    for(std::size_t i=start;i<history.size();++i)history[i]["part_id"]=part.id;
  }
  return result;
}
// OCCT's 2D corner algorithm implements Modified for edges only; its
// override casts a face to an edge instead of returning an empty history.
struct CornerHistory {
  BRepFilletAPI_MakeFillet2d& operation;
  bool IsDeleted(const TopoDS_Shape& shape){return operation.IsDeleted(shape);}
  NCollection_List<TopoDS_Shape> Modified(const TopoDS_Shape& shape){return shape.ShapeType()==TopAbs_EDGE&&operation.IsModified(TopoDS::Edge(shape))?operation.Modified(shape):NCollection_List<TopoDS_Shape>{};}
  NCollection_List<TopoDS_Shape> Generated(const TopoDS_Shape& shape){return operation.Generated(shape);}
};
gp_Ax2 sketch_plane(const Json& feature,const Json& parameters,const std::map<std::string,gp_Ax2>& planes,const TopoDS_Shape& shape) {
  const auto type=text_field(feature,"type");
  if(type=="sketch"||type=="sketch_projection")return parameter_plane(feature.at("workplane"),parameters);
  if(type=="sketch_face") {
    ShapeMap faces;TopExp::MapShapes(shape,TopAbs_FACE,faces);
    if(faces.IsEmpty())throw Error("invalid_shape","Derived sketch has no planar faces");
    const auto face=TopoDS::Face(faces(1));const BRepAdaptor_Surface surface(face);
    if(surface.GetType()!=GeomAbs_Plane)throw Error("invalid_model","Derived sketches require selected planar faces");
    auto normal=surface.Plane().Axis().Direction();if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
    return gp_Ax2(surface.Plane().Location(),normal,surface.Plane().XAxis().Direction());
  }
  const auto input=text_field(feature,feature.contains("left")?"left":"input");auto plane=planes.at(input);
  if(type=="sketch_transform"||type=="sketch_instance")plane.Transform(placement_transform(feature,parameters));
  else if(type=="sketch_mirror") {
    gp_Trsf mirror;mirror.SetMirror(parameter_plane(feature.at("plane"),parameters));
    // Transform the normal as a vector. Ax2::Transform reverses it under a
    // reflection to restore handedness, which would reverse extrusion intent.
    plane=gp_Ax2(plane.Location().Transformed(mirror),plane.Direction().Transformed(mirror),plane.XDirection().Transformed(mirror));
  }
  return plane;
}
Json feature_provenance(const Json& feature,const Json& history,bool history_truncated) {
  Json dependencies=Json::array();
  for (const auto* key:{"input","left","right","target"}) if (feature.contains(key)) dependencies.push_back(feature.at(key));
  if(feature.contains("boundaries"))for(const auto& b:feature.at("boundaries"))if(b.contains("input")&&std::find(dependencies.begin(),dependencies.end(),b.at("input"))==dependencies.end())dependencies.push_back(b.at("input"));
  if (feature.contains("sections")) dependencies=feature.at("sections");
  if (feature.contains("inputs")) dependencies=feature.at("inputs");
  if (feature.at("type")=="assembly") {
    std::set<std::string> added;
    for (const auto& part:feature.at("parts")) if (added.insert(text_field(part,"input")).second) dependencies.push_back(part.at("input"));
  }
  Json result={{"feature_id",feature.at("id")},{"feature_type",feature.at("type")},{"dependencies",dependencies},
    {"reference_policy","geometric_replay"},{"history_lifetime","evaluation"},{"history",history},{"history_truncated",history_truncated}};
  if (feature.at("type")=="import_step"||feature.at("type")=="import_step_surface") result["content_sha256"]=feature.at("sha256");
  if(feature.at("type")=="sketch"){const auto& p=feature.at("profile");if(p.contains("sha256"))result["content_sha256"]=p.at("sha256");if(p.contains("font"))result["font_sha256"]=p.at("font").at("sha256");}
  return result;
}
Json snapshot_feature(const FeatureGeometry& geometry,std::size_t limit=32*1024*1024) {
  SnapshotBuffer buffer(limit);std::ostream stream(&buffer);
  BRepTools::Write(geometry.shape,stream,false,false,TopTools_FormatVersion_CURRENT);
  if(!stream)throw Error("limit_exceeded","Feature snapshot exceeds cache budget");
  Json entry={{"brep",std::move(buffer.bytes)},{"faces",geometry.faces.Extent()},
    {"edges",geometry.edges.Extent()},{"provenance",geometry.provenance}};
  if(!geometry.parts.empty()) {entry["assembly"]=true;entry["parts"]=assembly_parts(geometry.parts);}
  return entry;
}
FeatureGeometry restore_feature(const Json& feature,const Json& parameters,
                               const std::map<std::string,FeatureGeometry>& sources,const Json& entry) {
  const auto type=text_field(feature,"type");
  std::istringstream stream(entry.at("brep").get<std::string>());
  TopoDS_Shape shape;BRepTools::Read(shape,stream,BRep_Builder{});
  if(stream.fail()||shape.IsNull())throw Error("cache_miss","Cannot read cached feature B-rep");
  if(is_curve_feature_type(type))check_curve_shape(shape);
  else if(is_surface_feature_type(type))check_surface_intent(feature,shape);
  else if(!is_sketch_feature_type(type))check_shape(shape);
  else {ShapeMap faces;TopExp::MapShapes(shape,TopAbs_FACE,faces);if(faces.IsEmpty())throw Error("cache_miss","Invalid cached sketch");const auto surface=BRepAdaptor_Surface(TopoDS::Face(faces(1)));if(surface.GetType()!=GeomAbs_Plane)throw Error("cache_miss","Cached sketch is nonplanar");check_sketch_shape(shape,surface.Plane().Position().Ax2());}
  FeatureGeometry geometry(shape);
  if(type=="assembly") {
    if(!entry.value("assembly",false)||shape.ShapeType()!=TopAbs_COMPOUND)throw Error("cache_miss","Missing cached assembly compound");
    auto plan=assembly_plan(feature,parameters,sources);
    if(entry.at("parts")!=assembly_parts(plan.parts))throw Error("cache_miss","Cached assembly occurrence transforms differ from saved intent");
    TopoDS_Iterator children(shape);
    for(auto& part:plan.parts) {
      if(!children.More())throw Error("cache_miss","Cached assembly is missing an occurrence");
      part.shape=children.Value();children.Next();const auto& source=sources.at(part.input);
      if(count(part.shape,TopAbs_FACE)!=source.faces.Extent()||count(part.shape,TopAbs_EDGE)!=source.edges.Extent()
          ||count(part.shape,TopAbs_SOLID)!=count(source.shape,TopAbs_SOLID))
        throw Error("cache_miss","Cached occurrence topology differs from its source",{{"part_id",part.id}});
    }
    if(children.More())throw Error("cache_miss","Cached assembly has extra occurrences");
    // Ownership comes from the actual deserialized children, never old indices.
    geometry=assemble_geometry(shape,std::move(plan),"cache_miss");
  }else if(entry.contains("assembly"))throw Error("cache_miss","Unexpected cached assembly marker");
  if(geometry.faces.Extent()!=entry.at("faces")||geometry.edges.Extent()!=entry.at("edges"))
    throw Error("cache_miss","Cached topology count mismatch");
  const auto& provenance=entry.at("provenance");
  if(!provenance.at("history").is_array()||provenance.at("history").size()>10000||!provenance.at("history_truncated").is_boolean()
      ||provenance!=feature_provenance(feature,provenance.at("history"),provenance.at("history_truncated").get<bool>()))
    throw Error("cache_miss","Cached feature provenance differs from saved intent");
  geometry.provenance=provenance;return geometry;
}
struct CurveWire {
  TopoDS_Wire wire;
  gp_Pnt start;
  gp_Vec tangent;
};
CurveWire curve_wire(const Json& segments,const Json& parameters,const gp_Ax2* plane=nullptr,bool closed=false) {
  const auto point=[&](const Json& value) {
    if (!plane) return parameter_point(value,parameters);
    return plane->Location().Translated(gp_Vec(plane->XDirection())*scalar(value[0],parameters)+
      gp_Vec(plane->YDirection())*scalar(value[1],parameters));
  };
  const auto tangent=[&](const Json& value) {
    if (plane) return gp_Vec(plane->XDirection())*scalar(value[0],parameters,"dimensionless")+
      gp_Vec(plane->YDirection())*scalar(value[1],parameters,"dimensionless");
    const auto v=vector3(value,parameters,"dimensionless");return gp_Vec(v[0],v[1],v[2]);
  };
  BRepBuilderAPI_MakeWire builder;
  CurveWire result;gp_Pnt previous;
  for (std::size_t i=0;i<segments.size();++i) {
    try {
      const auto& segment=segments[i];const auto type=text_field(segment,"type");
      TopoDS_Edge edge;gp_Pnt first,last;gp_Vec derivative;
      if (type=="line") {
        first=point(segment.at("start"));last=point(segment.at("end"));derivative=gp_Vec(first,last);
        if (derivative.Magnitude()<1e-7) throw Error("invalid_shape","Curve contains a zero-length line");
        edge=BRepBuilderAPI_MakeEdge(first,last);
      } else {
        occ::handle<Geom_Curve> curve;
        if (type=="arc") {
          GC_MakeArcOfCircle arc(point(segment.at("start")),point(segment.at("mid")),point(segment.at("end")));
          if (!arc.IsDone()) throw Error("invalid_shape","Arc needs three distinct non-collinear points");
          curve=arc.Value();
        } else {
          const auto& points=segment.at("points");
          if (type=="bezier") {
            NCollection_Array1<gp_Pnt> poles(1,static_cast<int>(points.size()));
            for (std::size_t j=0;j<points.size();++j) poles.SetValue(static_cast<int>(j+1),point(points[j]));
            if(segment.contains("weights")){NCollection_Array1<double> weights(1,static_cast<int>(points.size()));for(std::size_t j=0;j<points.size();++j)weights.SetValue(static_cast<int>(j+1),segment.at("weights")[j].get<double>());curve=new Geom_BezierCurve(poles,weights);}else curve=new Geom_BezierCurve(poles);
          } else if(type=="bspline") {
            NCollection_Array1<gp_Pnt> poles(1,static_cast<int>(points.size()));for(std::size_t j=0;j<points.size();++j)poles.SetValue(static_cast<int>(j+1),point(points[j]));
            const auto& values=segment.at("knots");NCollection_Array1<double> knots(1,static_cast<int>(values.size()));NCollection_Array1<int> multiplicities(1,static_cast<int>(values.size()));for(std::size_t j=0;j<values.size();++j){knots.SetValue(static_cast<int>(j+1),values[j].get<double>());multiplicities.SetValue(static_cast<int>(j+1),segment.at("multiplicities")[j].get<int>());}
            if(segment.contains("weights")){NCollection_Array1<double> weights(1,static_cast<int>(points.size()));for(std::size_t j=0;j<points.size();++j)weights.SetValue(static_cast<int>(j+1),segment.at("weights")[j].get<double>());curve=new Geom_BSplineCurve(poles,weights,knots,multiplicities,segment.at("degree").get<int>());}else curve=new Geom_BSplineCurve(poles,knots,multiplicities,segment.at("degree").get<int>());
          } else {
            occ::handle<NCollection_HArray1<gp_Pnt>> data=new NCollection_HArray1<gp_Pnt>(1,static_cast<int>(points.size()));
            for (std::size_t j=0;j<points.size();++j) {
              const auto p=point(points[j]);
              for (std::size_t k=0;k<j;++k) if (p.Distance(data->Value(static_cast<int>(k+1)))<1e-7)
                throw Error("invalid_shape","Spline interpolation points must be distinct; periodic closure is implicit");
              data->SetValue(static_cast<int>(j+1),p);
            }
            GeomAPI_Interpolate interpolation(data,segment.value("periodic",false),1e-7);
            if (segment.contains("start_tangent")) interpolation.Load(tangent(segment.at("start_tangent")),tangent(segment.at("end_tangent")));
            interpolation.Perform();
            if (!interpolation.IsDone()) throw Error("kernel_failure","Spline interpolation failed");
            curve=interpolation.Curve();
          }
        }
        curve->D1(curve->FirstParameter(),first,derivative);last=curve->Value(curve->LastParameter());
        if (derivative.Magnitude()<1e-12) throw Error("invalid_shape","Curve has an undefined start tangent");
        gp_Pnt endpoint;gp_Vec end_tangent;curve->D1(curve->LastParameter(),endpoint,end_tangent);
        if (end_tangent.Magnitude()<1e-12) throw Error("invalid_shape","Curve has an undefined end tangent");
        BRepBuilderAPI_MakeEdge make(curve);
        if (!make.IsDone()) throw Error("kernel_failure","Curve edge construction failed");
        edge=make.Edge();
      }
      if (i && first.Distance(previous)>1e-7) throw Error("invalid_shape","Curve segments must connect in their authored order within 1e-7 mm");
      if (!i) {result.start=first;result.tangent=derivative;}
      previous=last;builder.Add(edge);
      if (!builder.IsDone()) throw Error("invalid_shape","Curve segments do not form a connected wire");
    } catch (const Error& e) {auto details=e.details;details["segment_index"]=i;throw Error(e.code,e.what(),details);}
    catch (const Standard_Failure& e) {throw occt_error(e,{{"segment_index",i}});}
  }
  if (closed && previous.Distance(result.start)>1e-7) throw Error("invalid_shape","Profile wire must close explicitly within 1e-7 mm");
  result.wire=builder.Wire();
  BRepAlgoAPI_Check check(result.wire,false,true);
  if (check.HasErrors() || !check.IsValid()) throw Error("invalid_shape","Curve wire self-intersects or contains invalid geometry");
  return result;
}
TopoDS_Face sketch_face(const Json& profile, const Json& parameters, const gp_Ax2& plane) {
  const auto kind = text_field(profile,"type");
  TopoDS_Wire wire;
  if (kind == "circle") {
    wire = BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(plane,scalar(profile.at("radius"),parameters))).Edge());
  } else if (kind=="wire") {
    wire=curve_wire(profile.at("segments"),parameters,&plane,true).wire;
  } else {
    std::vector<std::array<double,2>> points;
    if (kind == "rectangle") {
      const auto width = scalar(profile.at("width"),parameters), height = scalar(profile.at("height"),parameters);
      points = {{0,0},{width,0},{width,height},{0,height}};
    } else for (const auto& p : profile.at("points")) points.push_back({scalar(p[0],parameters),scalar(p[1],parameters)});
    // Reject self-intersection and touching non-adjacent edges explicitly. A
    // valid wire container alone is not evidence of a simple profile boundary.
    const auto orient = [](auto a,auto b,auto c) { return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]); };
    const auto on = [&](auto a,auto b,auto p) { return std::abs(orient(a,b,p)) <= 1e-10 && p[0] >= std::min(a[0],b[0])-1e-10 && p[0] <= std::max(a[0],b[0])+1e-10 && p[1] >= std::min(a[1],b[1])-1e-10 && p[1] <= std::max(a[1],b[1])+1e-10; };
    for (std::size_t i = 0; i < points.size(); ++i) {
      const auto a=points[i], b=points[(i+1)%points.size()];
      if (std::hypot(a[0]-b[0],a[1]-b[1]) < 1e-7) throw Error("invalid_shape","Sketch contains a zero-length boundary segment");
      for (std::size_t j=i+1; j<points.size(); ++j) {
        if (j == i+1 || (i == 0 && j == points.size()-1)) continue;
        const auto c=points[j],d=points[(j+1)%points.size()];
        if ((orient(a,b,c)*orient(a,b,d)<0 && orient(c,d,a)*orient(c,d,b)<0) || on(a,b,c) || on(a,b,d) || on(c,d,a) || on(c,d,b))
          throw Error("invalid_shape","Sketch boundary self-intersects or touches itself");
      }
    }
    BRepBuilderAPI_MakePolygon polygon;
    for (const auto& p : points) polygon.Add(plane.Location().Translated(gp_Vec(plane.XDirection())*p[0]+gp_Vec(plane.YDirection())*p[1]));
    polygon.Close();
    if (!polygon.IsDone()) throw Error("kernel_failure","Sketch wire construction failed");
    wire = polygon.Wire();
  }
  BRepBuilderAPI_MakeFace face(gp_Pln(plane),wire);
  if (!face.IsDone()) throw Error("kernel_failure","Sketch face construction failed");
  if (profile.contains("holes")) {
    const auto area_of=[](const TopoDS_Shape& shape) {GProp_GProps p;BRepGProp::SurfaceProperties(shape,p);return p.Mass();};
    const auto overlap_area=[&](const TopoDS_Shape& a,const TopoDS_Shape& b) {
      BRepAlgoAPI_Common common;NCollection_List<TopoDS_Shape> arguments,tools;
      arguments.Append(a);tools.Append(b);common.SetArguments(arguments);common.SetTools(tools);
      common.SetNonDestructive(true);common.SetRunParallel(false);common.Build();
      if (!common.IsDone() || common.HasErrors()) throw Error("kernel_failure","Profile interior containment check failed");
      return area_of(common.Shape());
    };
    const auto outer=face.Face();std::vector<TopoDS_Face> interiors;
    for (std::size_t i=0;i<profile.at("holes").size();++i) {
      try {
        const auto interior=curve_wire(profile.at("holes")[i],parameters,&plane,true).wire;
        BRepBuilderAPI_MakeFace make(gp_Pln(plane),interior);
        if (!make.IsDone() || !BRepCheck_Analyzer(make.Face()).IsValid()) throw Error("invalid_shape","Invalid profile interior boundary");
        const auto hole=make.Face();const double hole_area=area_of(hole);
        if (!(hole_area>1e-10) || std::abs(overlap_area(outer,hole)-hole_area)>std::max(1e-9,hole_area*1e-9))
          throw Error("invalid_shape","Profile interior boundary must be inside the outer boundary");
        const auto separated=[&](const TopoDS_Face& other) {
          BRepExtrema_DistShapeShape distance(BRepTools::OuterWire(other),BRepTools::OuterWire(hole));
          if (!distance.IsDone() || distance.Value()<=1e-7) throw Error("invalid_shape","Profile boundaries must not touch or intersect");
        };
        separated(outer);
        for (const auto& prior:interiors) {
          separated(prior);
          if (overlap_area(prior,hole)>1e-9) throw Error("invalid_shape","Profile interiors must not overlap or contain one another");
        }
        // MakeFace normalizes the outer wire's orientation, so author winding
        // does not change which side of an explicit interior is removed.
        face.Add(TopoDS::Wire(BRepTools::OuterWire(hole).Reversed()));interiors.push_back(hole);
      } catch (const Error& e) {auto details=e.details;details["hole_index"]=i;throw Error(e.code,e.what(),details);}
    }
  }
  GProp_GProps area;
  BRepGProp::SurfaceProperties(face.Face(),area);
  if (!BRepCheck_Analyzer(face.Face()).IsValid() || !std::isfinite(area.Mass()) || area.Mass() <= 0)
    throw Error("invalid_shape","Sketch must be a valid simple profile with positive area");
  if (kind=="wire") {
    BRepAlgoAPI_Check check(face.Face(),false,true);
    if (check.HasErrors() || !check.IsValid()) throw Error("invalid_shape","Curved profile has self-interference");
  }
  return face.Face();
}
template<class Operation>
TopoDS_Shape extrusion_boolean(const TopoDS_Shape& a,const TopoDS_Shape& b) {
  Operation operation;NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(a);tools.Append(b);
  operation.SetArguments(arguments);operation.SetTools(tools);operation.SetNonDestructive(true);operation.SetRunParallel(false);operation.Build();
  if(!operation.IsDone()||operation.HasErrors())throw Error("kernel_failure","Extrusion boundary operation failed");
  return operation.Shape();
}
double shape_area(const TopoDS_Shape& shape) {GProp_GProps area;BRepGProp::SurfaceProperties(shape,area);return area.Mass();}
double face_contact_area(const TopoDS_Shape& shape,const TopoDS_Shape& face) {
  return shape_area(extrusion_boolean<BRepAlgoAPI_Common>(shape,face));
}
TopoDS_Face uv_trim_face(const TopoDS_Face& source,const Json& feature,const Json& parameters) {
  const gp_Ax2 xy(gp_Pnt(0,0,0),gp_Dir(0,0,1));
  Json profile={{"type","wire"},{"segments",feature.at("boundary")}};if(feature.contains("holes"))profile["holes"]=feature.at("holes");
  const auto evaluate_uv=[&](auto&& self,Json& value)->void{if(value.is_object()){if(value.contains("parameter")||value.contains("expression")){value=scalar(value,parameters,"dimensionless");return;}for(auto& item:value.items())self(self,item.value());}else if(value.is_array())for(auto& item:value)self(self,item);};evaluate_uv(evaluate_uv,profile);
  const auto uv=sketch_face(profile,Json::object(),xy);const auto support=BRep_Tool::Surface(source);
  BRepBuilderAPI_MakeFace result;result.Init(support,false,1e-7);
  for(TopoDS_Iterator wires(uv);wires.More();wires.Next()) {
    BRepBuilderAPI_MakeWire wire;
    for(BRepTools_WireExplorer edges(TopoDS::Wire(wires.Value()),uv);edges.More();edges.Next()) {
      double first,last;const auto curve=BRep_Tool::CurveOnSurface(edges.Current(),uv,first,last);
      BRepBuilderAPI_MakeEdge edge(curve,support,first,last);if(!edge.IsDone())throw Error("invalid_shape","Cannot map trim contour to source UV surface");
      auto mapped=edge.Edge();if(edges.Current().Orientation()==TopAbs_REVERSED)mapped.Reverse();wire.Add(mapped);
    }
    if(!wire.IsDone())throw Error("invalid_shape","Mapped trim contour is disconnected");result.Add(wire.Wire());
  }
  if(!result.IsDone())throw Error("kernel_failure","Surface contour trimming failed");auto face=result.Face();
  BRepLib::BuildCurves3d(face,1e-7);if(source.Orientation()==TopAbs_REVERSED)face.Reverse();check_surface_shape(face);return face;
}
void require_surface_containment(const TopoDS_Face& source,const TopoDS_Face& trimmed) {
  const auto outside=extrusion_boolean<BRepAlgoAPI_Cut>(trimmed,source);
  const auto area=shape_area(trimmed);if(shape_area(outside)>std::max(1e-7,area*1e-8))throw Error("invalid_model","Trim contour must remain wholly inside the existing source face, including its holes");
}
TopoDS_Edge unique_surface_edge(const FeatureGeometry& source,const Json& selector,const Json& parameters,const std::string& input) {
  std::vector<int> matched;for(int i=1;i<=source.edges.Extent();++i)if(matches(edge_descriptor(TopoDS::Edge(source.edges(i)),i),selector,parameters))matched.push_back(i);
  if(matched.size()!=1)throw Error(matched.empty()?"selection_missing":"selection_ambiguous","Surface boundary requires one uniquely selected exact edge",{{"source_feature_id",input},{"actual_count",matched.size()}});
  return TopoDS::Edge(source.edges(matched.front()));
}
TopoDS_Face filling_surface(const Json& feature,const Json& parameters,const std::map<std::string,FeatureGeometry>& sources) {
  const auto tolerance=feature.at("tolerance").get<double>(),angular=feature.value("angular_tolerance",1e-4),curvature=feature.value("curvature_tolerance",1e-4);
  // Reserve an approximation margin for the plate-to-B-spline conversion;
  // independently checked public tolerances are never enlarged.
  BRepOffsetAPI_MakeFilling filling(4,50,4,false,1e-8,tolerance*.1,angular*.1,curvature*.1,14,16);
  struct Constraint {TopoDS_Edge edge;TopoDS_Face face;GeomAbs_Shape order;int index;};std::vector<Constraint> constraints;
  gp_Pnt start,previous;std::map<std::string,std::unique_ptr<BRepBuilderAPI_Copy>> copies;
  for(const auto& boundary:feature.at("boundaries")) {
    TopoDS_Edge edge;TopoDS_Face support;
    if(boundary.contains("curve")){const auto wire=curve_wire(Json::array({boundary.at("curve")}),parameters).wire;edge=TopoDS::Edge(TopExp_Explorer(wire,TopAbs_EDGE).Current());}
    else {const auto input=text_field(boundary,"input");const auto& source=sources.at(input);edge=unique_surface_edge(source,boundary.at("edge"),parameters,input);
      if(boundary.contains("face")){support=TopoDS::Face(source.faces(select_faces(source,boundary.at("face"),parameters,input).front()));ShapeMap edges;TopExp::MapShapes(support,TopAbs_EDGE,edges);if(!edges.Contains(edge))throw Error("invalid_model","Filling edge must belong to its explicitly selected support face");}
      if(!copies.contains(input))copies[input]=std::make_unique<BRepBuilderAPI_Copy>(source.shape);
      auto& copy=*copies.at(input);const auto edge_orientation=edge.Orientation();edge=TopoDS::Edge(copy.ModifiedShape(edge));edge.Orientation(edge_orientation);if(!support.IsNull()){const auto face_orientation=support.Orientation();support=TopoDS::Face(copy.ModifiedShape(support));support.Orientation(face_orientation);}
    }
    if(boundary.value("reverse",false))edge.Reverse();
    const auto first=BRep_Tool::Pnt(TopExp::FirstVertex(edge,true)),last=BRep_Tool::Pnt(TopExp::LastVertex(edge,true));
    if(constraints.empty())start=first;else if(previous.Distance(first)>tolerance)throw Error("invalid_model","Filling boundaries must connect in their authored order; set reverse explicitly");previous=last;
    const auto continuity=text_field(boundary,"continuity");const auto order=continuity=="C0"?GeomAbs_C0:continuity=="G1"?GeomAbs_G1:GeomAbs_G2;
    // Pinned OCCT 8.0.1 call path: BRepOffsetAPI_MakeFilling::Add ->
    // BRepFill_Filling::Build / AddConstraints -> BRepFill_CurveConstraint ->
    // GeomPlate_CurveConstraint / GeomPlate_BuildPlateSurface. The GeomAbs
    // enum becomes a derivative-order integer: G2 is 3, but the plate expects
    // order 2. Map only this solver argument; retain the authored G2 contract
    // and independently check nonplanar curvature (parity_surfaces regression).
    const auto solver_order=order==GeomAbs_G2?static_cast<GeomAbs_Shape>(2):order;
    const auto index=support.IsNull()?filling.Add(edge,solver_order):filling.Add(edge,support,solver_order);constraints.push_back({edge,support,order,index});
  }
  if(previous.Distance(start)>tolerance)throw Error("invalid_model","Filling boundary must close explicitly; no missing edges are synthesized");
  if(feature.contains("points"))for(const auto& p:feature.at("points"))filling.Add(parameter_point(p,parameters));
  filling.Build();if(!filling.IsDone())throw Error("kernel_failure","Constrained surface filling did not converge");
  // OCCT 8.0.1's indexed G0Error accessor throws on valid C0 constraints;
  // use the algorithm's global maximum, followed by independent boundary checks.
  if(filling.G0Error()>tolerance||filling.G1Error()>angular||filling.G2Error()>curvature)throw Error("invalid_shape","Filling does not satisfy its explicit continuity tolerances",{{"distance_error_mm",filling.G0Error()},{"angular_error_rad",filling.G1Error()},{"curvature_error_per_mm",filling.G2Error()}});
  const auto face=TopoDS::Face(filling.Shape());check_surface_shape(face);const auto result=BRep_Tool::Surface(face);
  if(feature.contains("points"))for(const auto& p:feature.at("points")){BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(parameter_point(p,parameters)).Vertex(),face);if(!distance.IsDone()||distance.Value()>tolerance)throw Error("invalid_shape","Filling interior point constraint is not satisfied on the resulting face",{{"distance_mm",distance.IsDone()?distance.Value():-1}});}
  // Independently interrogate constructed geometry. Kernel completion alone can
  // silently ignore incompatible tangent/curvature constraints.
  for(std::size_t i=0;i<constraints.size();++i){const auto& c=constraints[i];BRepAdaptor_Curve edge(c.edge);
    for(int n=0;n<=32;++n){const auto point=edge.Value(edge.FirstParameter()+(edge.LastParameter()-edge.FirstParameter())*n/32.0);GeomAPI_ProjectPointOnSurf projected(point,result);
      if(!projected.IsDone()||projected.NbPoints()==0||projected.LowerDistance()>tolerance)throw Error("invalid_shape","Filling boundary positional verification failed",{{"boundary_index",i}});
      if(c.order==GeomAbs_C0)continue;double u,v;projected.LowerDistanceParameters(u,v);GeomLProp_SLProps a(result,u,v,2,1e-9);const auto support=BRep_Tool::Surface(c.face);GeomAPI_ProjectPointOnSurf on_support(point,support);if(!on_support.IsDone()||on_support.NbPoints()==0)throw Error("invalid_shape","Cannot verify filling support continuity");double su,sv;on_support.LowerDistanceParameters(su,sv);GeomLProp_SLProps b(support,su,sv,2,1e-9);
      if(!a.IsNormalDefined()||!b.IsNormalDefined())throw Error("invalid_shape","Filling continuity has an undefined surface normal");const double dot=a.Normal().Dot(b.Normal());
      if(std::acos(std::clamp(std::abs(dot),0.0,1.0))>angular)throw Error("invalid_shape","Filling tangent verification failed",{{"boundary_index",i}});
      if(c.order==GeomAbs_G2){if(!a.IsCurvatureDefined()||!b.IsCurvatureDefined())throw Error("invalid_shape","Filling curvature is undefined");std::array<double,2> ka={a.MinCurvature(),a.MaxCurvature()},kb={b.MinCurvature()*(dot<0?-1:1),b.MaxCurvature()*(dot<0?-1:1)};std::sort(kb.begin(),kb.end());
        // Principal values alone miss a rotation of the curvature directions.
        // Compare world-space shape operators after aligning normal signs.
        const auto tensor=[](GeomLProp_SLProps& props){std::array<std::array<double,3>,3> matrix{};const auto normal=props.Normal();
          if(props.IsUmbilic()){const auto k=(props.MinCurvature()+props.MaxCurvature())*.5;for(int r=0;r<3;++r)for(int c=0;c<3;++c)matrix[r][c]=k*((r==c?1.0:0.0)-normal.Coord(r+1)*normal.Coord(c+1));}
          else {gp_Dir maximum,minimum;props.CurvatureDirections(maximum,minimum);for(int r=0;r<3;++r)for(int c=0;c<3;++c)matrix[r][c]=props.MaxCurvature()*maximum.Coord(r+1)*maximum.Coord(c+1)+props.MinCurvature()*minimum.Coord(r+1)*minimum.Coord(c+1);}return matrix;};
        const auto ta=tensor(a),tb=tensor(b);double tensor_error=0;for(int r=0;r<3;++r)for(int k=0;k<3;++k)tensor_error=std::max(tensor_error,std::abs(ta[r][k]-tb[r][k]*(dot<0?-1:1)));
        if(std::abs(ka[0]-kb[0])>curvature||std::abs(ka[1]-kb[1])>curvature||tensor_error>curvature)throw Error("invalid_shape","Filling curvature verification failed",{{"boundary_index",i},{"sample_index",n},{"result_curvatures",ka},{"support_curvatures",kb},{"normal_dot",dot},{"curvature_tensor_error_per_mm",tensor_error}});}
    }
  }
  return face;
}
using Spline=occ::handle<Geom_BSplineCurve>;
Spline network_curve(const Json& value,const Json& parameters) {
  const auto wire=curve_wire(Json::array({value}),parameters).wire;const auto edge=TopoDS::Edge(TopExp_Explorer(wire,TopAbs_EDGE).Current());double first,last;auto curve=BRep_Tool::Curve(edge,first,last);
  auto spline=GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(curve,first,last));if(spline->IsRational()||spline->IsPeriodic())throw Error("invalid_model","Gordon curves must be nonperiodic polynomial curves");
  auto knots=spline->Knots();BSplCLib::Reparametrize(0.0,1.0,knots);spline->SetKnots(knots);return spline;
}
double choose(int n,int k){double result=1;for(int j=1;j<=k;++j)result=result*(n-j+1)/j;return result;}
std::vector<Spline> cardinal_splines(const Json& stations,const Json& parameters) {
  std::vector<Spline> result;const auto degree=static_cast<int>(stations.size())-1;
  for(int i=0;i<=degree;++i){std::vector<double> power{1};double denominator=1;const auto xi=scalar(stations[i],parameters,"dimensionless");
    for(int j=0;j<=degree;++j)if(j!=i){const auto xj=scalar(stations[j],parameters,"dimensionless");std::vector<double> next(power.size()+1,0);for(std::size_t k=0;k<power.size();++k){next[k]-=xj*power[k];next[k+1]+=power[k];}power=std::move(next);denominator*=xi-xj;}
    NCollection_Array1<gp_Pnt> poles(1,degree+1);for(int k=0;k<=degree;++k){double value=0;for(int j=0;j<=k;++j)value+=power[j]/denominator*choose(k,j)/choose(degree,j);poles.SetValue(k+1,gp_Pnt(value,0,0));}
    result.push_back(GeomConvert::CurveToBSplineCurve(new Geom_BezierCurve(poles)));
  }return result;
}
void compatible_network_basis(std::vector<Spline>& family,std::vector<Spline>& cardinal) {
  std::vector<Spline> all=family;all.insert(all.end(),cardinal.begin(),cardinal.end());int degree=1;for(const auto& c:all)degree=std::max(degree,c->Degree());if(degree>25)throw Error("limit_exceeded","Gordon surface degree exceeds 25");
  std::map<double,int> knots;for(auto& c:all){c->IncreaseDegree(degree);for(int i=1;i<=c->NbKnots();++i)knots[c->Knot(i)]=std::max(knots[c->Knot(i)],c->Multiplicity(i));}
  if(knots.size()>512)throw Error("limit_exceeded","Gordon network exceeds 512 distinct knots per direction");
  NCollection_Array1<double> values(1,static_cast<int>(knots.size()));NCollection_Array1<int> mults(1,static_cast<int>(knots.size()));int k=1;for(const auto& [value,mult]:knots){values.SetValue(k,value);mults.SetValue(k++,mult);}
  for(auto& c:all)c->InsertKnots(values,mults,1e-12,false);
  for(const auto& c:all){if(c->NbPoles()!=all.front()->NbPoles()||c->NbKnots()!=all.front()->NbKnots())throw Error("invalid_shape","Gordon basis could not be reconciled exactly");
    for(int i=1;i<=c->NbKnots();++i)if(std::abs(c->Knot(i)-all.front()->Knot(i))>1e-12||c->Multiplicity(i)!=all.front()->Multiplicity(i))throw Error("invalid_shape","Gordon basis knot values or multiplicities differ");}
}
occ::handle<Geom_BSplineSurface> gordon_surface(const Json& feature,const Json& parameters) {
  std::vector<Spline> us,vs;for(const auto& c:feature.at("u_curves"))us.push_back(network_curve(c,parameters));for(const auto& c:feature.at("v_curves"))vs.push_back(network_curve(c,parameters));
  auto uc=cardinal_splines(feature.at("u_parameters"),parameters),vc=cardinal_splines(feature.at("v_parameters"),parameters);const auto tolerance=feature.at("tolerance").get<double>();
  std::vector<std::vector<gp_Pnt>> crossings(us.size(),std::vector<gp_Pnt>(vs.size()));
  for(std::size_t i=0;i<us.size();++i)for(std::size_t j=0;j<vs.size();++j){const auto a=us[i]->Value(scalar(feature.at("u_parameters")[j],parameters,"dimensionless")),b=vs[j]->Value(scalar(feature.at("v_parameters")[i],parameters,"dimensionless"));if(a.Distance(b)>tolerance)throw Error("invalid_model","Gordon network curves do not intersect at their explicit compatible parameter stations",{{"u_curve_index",i},{"v_curve_index",j},{"gap_mm",a.Distance(b)}});crossings[i][j]=a;}
  compatible_network_basis(us,uc);compatible_network_basis(vs,vc);const auto nu=us.front()->NbPoles(),nv=vs.front()->NbPoles();if(static_cast<std::size_t>(nu)*nv>65536)throw Error("limit_exceeded","Gordon surface exceeds 65536 control points");NCollection_Array2<gp_Pnt> poles(1,nu,1,nv);
  for(int k=1;k<=nu;++k)for(int l=1;l<=nv;++l){gp_XYZ point(0,0,0);for(std::size_t i=0;i<us.size();++i)point+=us[i]->Pole(k).XYZ()*vc[i]->Pole(l).X();for(std::size_t j=0;j<vs.size();++j)point+=vs[j]->Pole(l).XYZ()*uc[j]->Pole(k).X();for(std::size_t i=0;i<us.size();++i)for(std::size_t j=0;j<vs.size();++j)point-=crossings[i][j].XYZ()*(uc[j]->Pole(k).X()*vc[i]->Pole(l).X());poles.SetValue(k,l,gp_Pnt(point));}
  auto surface=occ::handle<Geom_BSplineSurface>(new Geom_BSplineSurface(poles,us.front()->Knots(),vs.front()->Knots(),us.front()->Multiplicities(),vs.front()->Multiplicities(),us.front()->Degree(),vs.front()->Degree()));
  // Algebraically equal bases interpolate complete polynomial curves. Retain an
  // independent sampling bound to catch ill-conditioned user station networks.
  for(std::size_t i=0;i<us.size();++i)for(int n=0;n<=128;++n){const double u=n/128.0,v=scalar(feature.at("v_parameters")[i],parameters,"dimensionless");if(surface->Value(u,v).Distance(us[i]->Value(u))>tolerance)throw Error("invalid_shape","Gordon surface fails U-network interpolation tolerance");}
  for(std::size_t j=0;j<vs.size();++j)for(int n=0;n<=128;++n){const double v=n/128.0,u=scalar(feature.at("u_parameters")[j],parameters,"dimensionless");if(surface->Value(u,v).Distance(vs[j]->Value(v))>tolerance)throw Error("invalid_shape","Gordon surface fails V-network interpolation tolerance");}
  return surface;
}
TopoDS_Wire projected_wire(const TopoDS_Wire& source,const TopoDS_Face& target,const gp_Dir& direction) {
  // Require a unique forward hit over each source edge before asking OCCT to
  // build the exact section. A target behind the curve is never selected.
  for(TopExp_Explorer it(source,TopAbs_EDGE);it.More();it.Next()){BRepAdaptor_Curve edge(TopoDS::Edge(it.Current()));for(int n=0;n<=64;++n){const auto p=edge.Value(edge.FirstParameter()+(edge.LastParameter()-edge.FirstParameter())*n/64.0);IntCurvesFace_ShapeIntersector ray;ray.Load(target,1e-7);ray.Perform(gp_Lin(p,direction),-1e-7,1e9);if(!ray.IsDone()||ray.NbPnt()!=1)throw Error(ray.NbPnt()>1?"selection_ambiguous":"selection_missing","Projection needs exactly one forward target intersection along every source edge",{{"sample_index",n},{"intersection_count",ray.NbPnt()}});}}
  BRepProj_Projection projection(source,target,direction);if(!projection.IsDone())throw Error("kernel_failure","Exact curved-surface projection failed");projection.Init();if(!projection.More())throw Error("selection_missing","Projection misses its target");const auto wire=projection.Current();projection.Next();if(projection.More())throw Error("selection_ambiguous","Projection produces multiple wires on the selected face");
  // The boundary endpoints must be represented; projection is not an implicit
  // trimming/clipping operation. Closedness must also survive the projection.
  if(source.Closed()!=wire.Closed())throw Error("invalid_shape","Projection changed wire closure");check_curve_shape(wire);return wire;
}
TopoDS_Shape project_geometry(const Json& feature,const Json& parameters,const FeatureGeometry& source,const FeatureGeometry& target) {
  const auto original_face=TopoDS::Face(target.faces(select_faces(target,feature.at("faces"),parameters,text_field(feature,"target")).front()));
  BRepBuilderAPI_Copy target_copy(original_face),source_copy(source.shape);
  auto face=TopoDS::Face(target_copy.Shape());face.Orientation(original_face.Orientation());const auto direction=parameter_direction(feature.at("direction"),parameters);const bool region=feature.at("type")=="surface_project";
  if(region&&source.faces.Extent()>1)throw Error("invalid_model","Surface projection accepts one sketch region at a time");
  TopoDS_Compound curves;BRep_Builder builder;builder.MakeCompound(curves);BRepBuilderAPI_MakeFace projected;
  ShapeMap wires;TopExp::MapShapes(source_copy.Shape(),TopAbs_WIRE,wires);if(wires.IsEmpty())throw Error("invalid_model","Projection input must contain exact wires");
  for(int i=1;i<=wires.Extent();++i){const auto wire=projected_wire(TopoDS::Wire(wires(i)),face,direction);if(region){
      if(!wire.Closed())throw Error("invalid_model","Surface projection needs a closed boundary; use curve_project for open curves");
      BRepBuilderAPI_MakeFace contour(BRep_Tool::Surface(face),wire,true);if(!contour.IsDone())throw Error("invalid_shape","Projected wire has no valid bounded surface region");
      if(i==1)projected=contour;else projected.Add(TopoDS::Wire(BRepTools::OuterWire(contour.Face()).Reversed()));
    }else builder.Add(curves,wire);}

  if(!region)return curves;
  if(!projected.IsDone())throw Error("kernel_failure","Projected surface boundary construction failed");auto result=projected.Face();if(face.Orientation()==TopAbs_REVERSED)result.Reverse();check_surface_shape(result);require_surface_containment(face,result);return result;
}

TopoDS_Shape join_extrusion_regions(const std::vector<TopoDS_Shape>& regions) {
  if(regions.empty())throw Error("invalid_shape","Extrusion has no material");
  auto result=regions.front();for(std::size_t i=1;i<regions.size();++i)result=extrusion_boolean<BRepAlgoAPI_Fuse>(result,regions[i]);
  return result;
}
TopoDS_Wire taper_wire(const TopoDS_Wire& wire,const gp_Ax2& plane,double amount,const gp_Vec& travel) {
  const auto face=BRepBuilderAPI_MakeFace(gp_Pln(plane),wire).Face();
  // OCCT's medial-axis offset crashes on a circular contour when the requested
  // inset passes its centre. Construct this exact analytic case directly.
  std::optional<gp_Circ> circle;bool circular=true;
  for(TopExp_Explorer edge(wire,TopAbs_EDGE);edge.More();edge.Next()) {
    BRepAdaptor_Curve curve(TopoDS::Edge(edge.Current()));
    if(curve.GetType()!=GeomAbs_Circle){circular=false;break;}
    const auto current=curve.Circle();
    if(circle&&(circle->Location().Distance(current.Location())>1e-7||std::abs(circle->Radius()-current.Radius())>1e-7)){circular=false;break;}
    circle=current;
  }
  GProp_GProps perimeter;BRepGProp::LinearProperties(wire,perimeter);
  if(circular&&circle&&std::abs(perimeter.Mass()-2*std::numbers::pi*circle->Radius())<1e-7) {
    const double radius=circle->Radius()+amount;
    if(radius<=1e-7)throw Error("invalid_shape","Taper collapses a circular profile boundary");
    const auto end=gp_Ax2(circle->Location().Translated(travel),plane.Direction(),plane.XDirection());
    return BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(end,radius)).Edge()).Wire();
  }
  BRepOffsetAPI_MakeOffset offset(face,GeomAbs_Intersection,false);offset.Perform(amount);
  if(!offset.IsDone())throw Error("kernel_failure","Taper profile offset failed");
  ShapeMap wires;TopExp::MapShapes(offset.Shape(),TopAbs_WIRE,wires);
  if(wires.Extent()!=1)throw Error("invalid_shape","Taper collapses or divides a profile boundary");
  const auto result=TopoDS::Wire(wires(1));
  if(!BRepCheck_Analyzer(result).IsValid())throw Error("invalid_shape","Taper creates an invalid profile boundary");
  const auto offset_face=BRepBuilderAPI_MakeFace(gp_Pln(plane),result).Face();
  const auto before=shape_area(face),after=shape_area(offset_face);
  if(!(after>1e-10)||(amount<0&&after>=before)||(amount>0&&after<=before))throw Error("invalid_shape","Taper does not preserve its intended profile offset");
  gp_Trsf move;move.SetTranslation(travel);return TopoDS::Wire(BRepBuilderAPI_Transform(result,move,true).Shape());
}
TopoDS_Shape taper_region(const TopoDS_Face& face,const gp_Ax2& plane,const gp_Vec& travel,double taper) {
  const double inset=travel.Magnitude()*std::tan(taper*std::numbers::pi/180);
  const auto swept=[&](const TopoDS_Wire& wire,const TopoDS_Wire& end) {
    BRepOffsetAPI_ThruSections loft(true,true);loft.SetMutableInput(false);loft.AddWire(wire);
    loft.AddWire(end);loft.CheckCompatibility(true);loft.Build();
    if(!loft.IsDone())throw Error("kernel_failure","Tapered extrusion loft failed");
    return loft.Shape();
  };
  const auto outer=BRepTools::OuterWire(face),end_outer=taper_wire(outer,plane,-inset,travel);
  const auto endplane=gp_Pln(plane.Location().Translated(travel),plane.Direction());
  BRepBuilderAPI_MakeFace end_cap(endplane,end_outer);
  auto result=swept(outer,end_outer);
  for(TopExp_Explorer it(face,TopAbs_WIRE);it.More();it.Next())if(!it.Current().IsSame(outer)) {
    const auto wire=TopoDS::Wire(it.Current()),end_wire=taper_wire(wire,plane,inset,travel);
    const auto hole=BRepBuilderAPI_MakeFace(endplane,end_wire).Face();
    end_cap.Add(TopoDS::Wire(BRepTools::OuterWire(hole).Reversed()));
    result=extrusion_boolean<BRepAlgoAPI_Cut>(result,swept(wire,end_wire));
  }
  // A valid side-wall fragment is insufficient: the complete requested end
  // profile must survive rather than silently terminating where a wall closes.
  const auto cap=end_cap.Face();const double area=shape_area(cap);
  if(!BRepCheck_Analyzer(cap).IsValid()||!(area>1e-10)||std::abs(face_contact_area(result,cap)-area)>std::max(1e-8,area*1e-8))
    throw Error("invalid_shape","Taper closes the extrusion before its requested distance");
  check_shape(result);return result;
}
TopoDS_Shape target_extrusion(const TopoDS_Shape& profile,const gp_Dir& direction,const TopoDS_Shape& target,const std::string& until) {
  const auto projected=[&](const TopoDS_Shape& shape) {
    const auto box=bounds(shape);double low=std::numeric_limits<double>::infinity(),high=-low;
    for(int x=0;x<2;++x)for(int y=0;y<2;++y)for(int z=0;z<2;++z) {
      const auto coord=[&](int axis,int upper){return box.at(upper?"max":"min").at(axis).get<double>();};
      const double value=gp_Vec(coord(0,x),coord(1,y),coord(2,z)).Dot(gp_Vec(direction));low=std::min(low,value);high=std::max(high,value);
    }
    return std::pair<double,double>{low,high};
  };
  const auto source_range=projected(profile),target_range=projected(target);
  if(target_range.second<=source_range.first+1e-7)throw Error("invalid_model","Extrusion target is behind the requested direction");
  const double length=target_range.second-source_range.first+std::max(1.0,target_range.second-target_range.first);
  const gp_Vec travel=gp_Vec(direction)*length;BRepPrimAPI_MakePrism prism(profile,travel,true);
  if(!prism.IsDone())throw Error("kernel_failure","Target extrusion prism failed");
  gp_Trsf move;move.SetTranslation(travel);const auto end=BRepBuilderAPI_Transform(profile,move,true).Shape();
  const auto inside=extrusion_boolean<BRepAlgoAPI_Common>(prism.Shape(),target),outside=extrusion_boolean<BRepAlgoAPI_Cut>(prism.Shape(),target);
  std::vector<TopoDS_Shape> selected;double start_area=0;bool encountered=false;
  for(const auto& partition:{inside,outside})for(TopExp_Explorer it(partition,TopAbs_SOLID);it.More();it.Next()) {
    const auto cell=it.Current();const double start=face_contact_area(cell,profile),finish=face_contact_area(cell,end);
    if(partition.IsSame(inside))encountered=true;
    if(until=="first"?start>1e-9:finish<=1e-9) {
      if(finish>1e-9)throw Error("invalid_model","Extrusion target does not terminate the complete profile");
      selected.push_back(cell);start_area+=start;
    }
  }
  const double area=shape_area(profile);
  if(!encountered||std::abs(start_area-area)>std::max(1e-8,area*1e-8))throw Error("invalid_model","Extrusion target must terminate every source region in the requested direction");
  auto result=join_extrusion_regions(selected);check_shape(result);return result;
}
std::vector<TopoDS_Wire> section_wires(const TopoDS_Face& face,const gp_Ax2& plane) {
  const auto outer=BRepTools::OuterWire(face);std::vector<TopoDS_Wire> result{outer};
  std::vector<std::pair<std::array<double,2>,TopoDS_Wire>> holes;
  for(TopExp_Explorer it(face,TopAbs_WIRE);it.More();it.Next())if(!it.Current().IsSame(outer)) {
    GProp_GProps area;BRepGProp::SurfaceProperties(BRepBuilderAPI_MakeFace(gp_Pln(plane),TopoDS::Wire(it.Current())).Face(),area);
    const gp_Vec center(plane.Location(),area.CentreOfMass());holes.push_back({{center.Dot(gp_Vec(plane.XDirection())),center.Dot(gp_Vec(plane.YDirection()))},TopoDS::Wire(it.Current())});
  }
  std::sort(holes.begin(),holes.end(),[](const auto& a,const auto& b){return a.first<b.first;});
  for(const auto& hole:holes)result.push_back(hole.second);return result;
}
void sweep_stations(const CurveWire& spine,const std::vector<gp_Ax2>& planes) {
  std::vector<std::pair<double,gp_Dir>> stations;double total=0;
  for(const auto& plane:planes) {
    std::vector<std::pair<double,gp_Dir>> matches;double accumulated=0;
    const auto vertex=BRepBuilderAPI_MakeVertex(plane.Location()).Vertex();
    for(BRepTools_WireExplorer edge(spine.wire);edge.More();edge.Next()) {
      BRepAdaptor_Curve curve(edge.Current());const auto first=curve.FirstParameter(),last=curve.LastParameter();
      const double length=GCPnts_AbscissaPoint::Length(curve,first,last,1e-8);
      BRepExtrema_DistShapeShape distance(vertex,edge.Current());
      if(!distance.IsDone())throw Error("kernel_failure","Sweep station projection failed");
      if(distance.Value()<=1e-7)for(int i=1;i<=distance.NbSolution();++i) {
        double parameter=first;
        if(distance.SupportTypeShape2(i)==BRepExtrema_IsOnEdge)distance.ParOnEdgeS2(i,parameter);
        else if(distance.PointOnShape2(i).Distance(curve.Value(last))<=1e-7)parameter=last;
        gp_Pnt point;gp_Vec tangent;curve.D1(parameter,point,tangent);
        if(tangent.Magnitude()<1e-12)throw Error("invalid_model","Sweep station has an undefined tangent");
        double local=GCPnts_AbscissaPoint::Length(curve,first,parameter,1e-8);
        if(edge.Current().Orientation()==TopAbs_REVERSED){local=length-local;tangent.Reverse();}
        const double station=accumulated+local;
        if(std::abs(gp_Dir(tangent).Dot(plane.Direction()))<1-1e-8)
          throw Error("invalid_model","Sweep section plane must be perpendicular to its path tangent");
        if(std::none_of(matches.begin(),matches.end(),[&](const auto& prior){return std::abs(prior.first-station)<=1e-7;}))matches.push_back({station,gp_Dir(tangent)});
      }
      accumulated+=length;
    }
    total=accumulated;
    if(matches.size()!=1)throw Error("invalid_model","Sweep section origin must have one unambiguous station on its path");
    stations.push_back(matches.front());
  }
  if(std::abs(stations.front().first)>1e-7||std::abs(stations.back().first-total)>1e-7)
    throw Error("invalid_model","Varying sweep sections must include both path endpoints");
  for(std::size_t i=1;i<stations.size();++i)if(stations[i].first<=stations[i-1].first+1e-7)
    throw Error("invalid_model","Varying sweep sections must follow strictly increasing path stations");
}
TopoDS_Shape controlled_sweep(const Json& feature,const Json& parameters,const CurveWire& spine,
                             const std::vector<TopoDS_Face>& faces,const std::vector<gp_Ax2>& planes,
                             Json& history,bool& history_truncated,const std::vector<const FeatureGeometry*>& sources,
                             const std::vector<std::string>& ids) {
  std::vector<std::vector<TopoDS_Wire>> profiles;for(std::size_t i=0;i<faces.size();++i)profiles.push_back(section_wires(faces[i],planes[i]));
  for(const auto& wires:profiles)if(wires.size()!=profiles.front().size())throw Error("invalid_model","Sweep sections must retain the same number of interior boundaries");
  std::optional<CurveWire> guide;if(feature.contains("guide"))guide=curve_wire(feature.at("guide").at("segments"),parameters);
  std::optional<gp_Dir> linear_guide;
  if(guide&&count(spine.wire,TopAbs_EDGE)==1&&count(guide->wire,TopAbs_EDGE)==1) {
    TopExp_Explorer a(spine.wire,TopAbs_EDGE),b(guide->wire,TopAbs_EDGE);
    BRepAdaptor_Curve main(TopoDS::Edge(a.Current())),auxiliary(TopoDS::Edge(b.Current()));
    if(main.GetType()==GeomAbs_Line&&auxiliary.GetType()==GeomAbs_Line&&std::abs(gp_Dir(spine.tangent).Dot(gp_Dir(guide->tangent)))>1-1e-10) {
      const gp_Vec offset(spine.start,guide->start);const gp_Vec tangent(gp_Dir(spine.tangent));
      const auto normal=offset-tangent*offset.Dot(tangent);
      if(normal.Magnitude()<1e-7)throw Error("invalid_model","Auxiliary sweep guide must define a distinct normal direction");
      linear_guide=gp_Dir(normal);
    }
  }
  std::vector<std::unique_ptr<BRepOffsetAPI_MakePipeShell>> operations;
  for(std::size_t boundary=0;boundary<profiles.front().size();++boundary) {
    auto operation=std::make_unique<BRepOffsetAPI_MakePipeShell>(spine.wire);
    if(feature.contains("binormal"))operation->SetMode(parameter_direction(feature.at("binormal"),parameters));
    // Parallel straight guide lines define a constant exact frame. Avoid the
    // auxiliary-spine law fitter, which approximates even this analytic case.
    else if(linear_guide)operation->SetMode(*linear_guide);
    else if(guide)operation->SetMode(guide->wire,true,BRepFill_NoContact);
    else if(feature.value("orientation",std::string("corrected_frenet"))=="fixed")operation->SetMode(planes.front());
    else operation->SetMode(feature.value("orientation",std::string("corrected_frenet"))=="frenet");
    const auto transition=feature.value("transition",std::string("transformed"));
    operation->SetTransitionMode(transition=="right_corner"?BRepBuilderAPI_RightCorner:transition=="round_corner"?BRepBuilderAPI_RoundCorner:BRepBuilderAPI_Transformed);
    operation->SetTolerance(1e-7,1e-7,1e-6);operation->SetMaxSegments(512);
    for(const auto& wires:profiles)operation->Add(wires[boundary],false,false);
    operation->Build();if(!operation->IsDone()||!operation->MakeSolid())throw Error("kernel_failure","Controlled sweep failed to create a closed solid",{{"boundary",boundary},{"status",static_cast<int>(operation->GetStatus())}});
    check_shape(operation->Shape());operations.push_back(std::move(operation));
  }
  auto result=operations.front()->Shape();for(std::size_t i=1;i<operations.size();++i)result=extrusion_boolean<BRepAlgoAPI_Cut>(result,operations[i]->Shape());
  check_shape(result);for(const auto& operation:operations)for(std::size_t i=0;i<sources.size();++i)record_history(*operation,*sources[i],ids[i],result,history,history_truncated);
  return result;
}
// Build material regions by nesting closed exact loops. No wire repair or
// polygon approximation occurs; touching/crossing boundaries fail explicitly.
TopoDS_Shape planar_regions(const std::vector<TopoDS_Wire>& wires,const gp_Ax2& plane) {
  if(wires.empty()||wires.size()>256)throw Error("invalid_shape","A planar result needs 1–256 closed boundaries");
  std::vector<TopoDS_Face> discs;std::vector<double> areas;
  for(const auto& wire:wires) {
    BRepBuilderAPI_MakeFace face(gp_Pln(plane),wire,true);
    if(!face.IsDone()||!wire.Closed()||!BRepCheck_Analyzer(face.Face()).IsValid())throw Error("invalid_shape","Offset or projected boundary is not a valid closed planar loop");
    BRepAlgoAPI_Check check(face.Face(),false,true);if(check.HasErrors()||!check.IsValid())throw Error("invalid_shape","Planar boundary self-intersects");
    GProp_GProps area;BRepGProp::SurfaceProperties(face.Face(),area);
    if(!(area.Mass()>1e-10))throw Error("invalid_shape","Planar boundary encloses no material");
    discs.push_back(face.Face());areas.push_back(area.Mass());
  }
  std::vector<int> parent(discs.size(),-1),depth(discs.size(),0);
  for(std::size_t a=0;a<discs.size();++a)for(std::size_t b=a+1;b<discs.size();++b) {
    const auto outer=areas[a]>areas[b]?a:b,inner=outer==a?b:a;
    BRepAlgoAPI_Common overlap(discs[outer],discs[inner]);overlap.SetRunParallel(false);overlap.Build();
    if(!overlap.IsDone()||overlap.HasErrors())throw Error("kernel_failure","Planar boundary nesting failed");
    GProp_GProps area;BRepGProp::SurfaceProperties(overlap.Shape(),area);
    const auto tolerance=std::max(1e-8,areas[inner]*1e-9);
    if(area.Mass()<=tolerance) {
      BRepExtrema_DistShapeShape contact(wires[a],wires[b]);contact.Perform();
      if(!contact.IsDone()||contact.Value()<=1e-7)throw Error("invalid_shape","Planar boundaries touch or cross");
      continue;
    }
    if(std::abs(area.Mass()-areas[inner])>tolerance||std::abs(areas[outer]-areas[inner])<=tolerance)throw Error("invalid_shape","Planar boundaries overlap or coincide");
    BRepExtrema_DistShapeShape contact(wires[a],wires[b]);contact.Perform();
    if(!contact.IsDone()||contact.Value()<=1e-7)throw Error("invalid_shape","Nested planar boundaries touch");
    if(parent[inner]<0||areas[outer]<areas[parent[inner]])parent[inner]=static_cast<int>(outer);
  }
  for(std::size_t a=0;a<discs.size();++a)for(auto p=parent[a];p>=0;p=parent[p])if(++depth[a]>256)throw Error("invalid_shape","Planar boundaries have cyclic nesting");
  std::vector<TopoDS_Face> regions;
  for(std::size_t a=0;a<discs.size();++a)if(depth[a]%2==0) {
    BRepBuilderAPI_MakeFace face(discs[a]);
    for(std::size_t b=0;b<discs.size();++b)if(parent[b]==static_cast<int>(a))face.Add(TopoDS::Wire(BRepTools::OuterWire(discs[b]).Reversed()));
    regions.push_back(face.Face());
  }
  const auto result=face_compound(regions);check_sketch_shape(result,plane);return result;
}
TopoDS_Shape authoring_face(const Json& profile,const Json& parameters,const gp_Ax2& plane) {
  const auto groups=authoring_contours(profile,parameters);
  const auto area=[](const TopoDS_Shape& shape){GProp_GProps props;BRepGProp::SurfaceProperties(shape,props);return props.Mass();};
  const auto common_area=[&](const TopoDS_Shape& a,const TopoDS_Shape& b){BRepAlgoAPI_Common common;NCollection_List<TopoDS_Shape> inputs,tools;inputs.Append(a);tools.Append(b);common.SetArguments(inputs);common.SetTools(tools);common.SetNonDestructive(true);common.SetRunParallel(false);common.Build();if(!common.IsDone()||common.HasErrors())throw Error("kernel_failure","Authoring contour containment failed");return common.Shape().IsNull()?0.0:area(common.Shape());};
  std::vector<TopoDS_Face> regions;
  for(const auto& group:groups) {
    struct Boundary{TopoDS_Face face;TopoDS_Wire wire;double area=0;int sign=0,parent=-1,winding=0;bool inside=false,outside=false;};
    std::vector<Boundary> boundaries;
    for(const auto& loop:group.at("loops")) {
      const auto authored=curve_wire(loop,Json::object(),&plane,true).wire;
      BRepBuilderAPI_MakeFace make(gp_Pln(plane),authored,true);
      if(!make.IsDone()||!BRepCheck_Analyzer(make.Face()).IsValid())throw Error("invalid_shape","Authoring boundary must enclose a valid planar region");
      BRepAlgoAPI_Check check(make.Face(),false,true);if(check.HasErrors()||!check.IsValid())throw Error("invalid_shape","Authoring boundary self-intersects");
      const auto normalized=BRepTools::OuterWire(make.Face());const auto value=area(make.Face());if(!std::isfinite(value)||value<1e-10)throw Error("invalid_shape","Authoring boundary has zero area");
      boundaries.push_back({make.Face(),normalized,value,authored.Orientation()==normalized.Orientation()?1:-1});
    }
    for(std::size_t i=0;i<boundaries.size();++i)for(std::size_t j=i+1;j<boundaries.size();++j) {
      auto& a=boundaries[i];auto& b=boundaries[j];BRepExtrema_DistShapeShape distance(a.wire,b.wire);if(!distance.IsDone()||distance.Value()<=1e-7)throw Error("invalid_shape","Authoring contours within one filled element must not touch or cross",{{"contour_a",i},{"contour_b",j}});
      const auto overlap=common_area(a.face,b.face),tolerance=std::max(1e-9,std::min(a.area,b.area)*1e-9);
      const bool a_contains=std::abs(overlap-b.area)<=tolerance,b_contains=std::abs(overlap-a.area)<=tolerance;
      if(overlap>tolerance&&!a_contains&&!b_contains)throw Error("invalid_shape","Authoring contours intersect without containment");
      if(a_contains&&(b.parent<0||a.area<boundaries[b.parent].area))b.parent=static_cast<int>(i);
      if(b_contains&&(a.parent<0||b.area<boundaries[a.parent].area))a.parent=static_cast<int>(j);
    }
    const bool evenodd=group.at("fill_rule")=="evenodd";
    std::function<void(int,std::set<int>&)> classify=[&](int i,std::set<int>& stack){auto& b=boundaries[i];if(!stack.insert(i).second)throw Error("invalid_shape","Authoring contour containment is cyclic");int before=0;if(b.parent>=0){classify(b.parent,stack);before=boundaries[b.parent].winding;}b.winding=evenodd?before+1:before+b.sign;b.outside=evenodd?before%2!=0:before!=0;b.inside=evenodd?b.winding%2!=0:b.winding!=0;stack.erase(i);};
    for(std::size_t i=0;i<boundaries.size();++i){std::set<int> stack;classify(static_cast<int>(i),stack);}
    for(std::size_t i=0;i<boundaries.size();++i) {
      const auto& outer=boundaries[i];if(outer.outside||!outer.inside)continue;BRepBuilderAPI_MakeFace make(gp_Pln(plane),outer.wire,true);
      for(std::size_t j=0;j<boundaries.size();++j){const auto& hole=boundaries[j];if(!hole.outside||hole.inside)continue;int parent=hole.parent;while(parent>=0&&(boundaries[parent].outside||!boundaries[parent].inside))parent=boundaries[parent].parent;if(parent==static_cast<int>(i))make.Add(TopoDS::Wire(hole.wire.Reversed()));}
      if(!make.IsDone()||!BRepCheck_Analyzer(make.Face()).IsValid()||area(make.Face())<=1e-10)throw Error("invalid_shape","Authoring filled region is invalid");regions.push_back(make.Face());
    }
  }
  if(regions.empty())throw Error("invalid_shape","Authoring source has no filled regions");
  // Separate SVG shapes and glyphs follow union semantics, including overlaps.
  TopoDS_Shape result=regions.front();for(std::size_t i=1;i<regions.size();++i){BRepAlgoAPI_Fuse fuse;NCollection_List<TopoDS_Shape> inputs,tools;inputs.Append(result);tools.Append(regions[i]);fuse.SetArguments(inputs);fuse.SetTools(tools);fuse.SetNonDestructive(true);fuse.SetRunParallel(false);fuse.Build();if(!fuse.IsDone()||fuse.HasErrors())throw Error("kernel_failure","Authoring filled-region union failed");result=fuse.Shape();}
  if(result.IsNull()||!BRepCheck_Analyzer(result).IsValid()||count(result,TopAbs_FACE)>128||area(result)<=1e-10)throw Error("invalid_shape","Authoring must yield 1..128 valid planar faces");return result;
}
TopoDS_Shape external_thread(double diameter,double pitch,double length,const gp_Pnt& origin,bool left_handed) {
  const double radius=diameter/2,depth=17*std::sqrt(3.0)*pitch/48,root=radius-depth;
  // The visible flanks are 60 degrees, the crest is P/8 wide, and the
  // nominal root diameter is d - 1.226869322 P. The root is deliberately
  // flat, not a claim of a certified rolled-root or 6g tolerance profile.
  const double overlap=.02*pitch,half_base=pitch/16+(depth+overlap)/std::sqrt(3.0);
  const auto point_at=[&](double r,double z){return origin.Translated(gp_Vec(r,0,z));};
  occ::handle<Geom_CylindricalSurface> support=new Geom_CylindricalSurface(gp_Ax3(origin,gp_Dir(0,0,1)),root);
  const auto curve=GC_MakeSegment2d(gp_Pnt2d(0,-pitch),gp_Pnt2d((left_handed?-1:1)*2*std::numbers::pi*(length/pitch+2),length+pitch)).Value();
  BRepBuilderAPI_MakeEdge edge(curve,support,curve->FirstParameter(),curve->LastParameter());
  if (!edge.IsDone()) throw Error("kernel_failure","Thread helix construction failed");
  const auto spine=BRepBuilderAPI_MakeWire(edge.Edge()).Wire();
  if (!BRepLib::BuildCurves3d(spine,1e-7)) throw Error("kernel_failure","Thread helix curve construction failed");
  BRepBuilderAPI_MakePolygon section;
  for (const auto& p : {point_at(root-overlap,-pitch-half_base),point_at(radius,-pitch-pitch/16),point_at(radius,-pitch+pitch/16),point_at(root-overlap,-pitch+half_base)}) section.Add(p);
  section.Close();
  BRepOffsetAPI_MakePipeShell sweep(spine);
  // A constant axial binormal rotates the radial/axial section with the
  // helix; unlike normal-to-tangent correction it preserves its axial pitch.
  sweep.SetMode(gp_Dir(0,0,1));
  sweep.SetTolerance(1e-6,1e-6,1e-4); sweep.SetMaxSegments(512);
  sweep.Add(section.Wire(),false,false); sweep.Build();
  if (!sweep.IsDone() || !sweep.MakeSolid()) throw Error("kernel_failure","Thread ridge sweep failed");
  const auto boolean=[&](auto& operation,const TopoDS_Shape& a,const TopoDS_Shape& b,const char* message) {
    NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(a);tools.Append(b);
    operation.SetArguments(arguments);operation.SetTools(tools);operation.SetNonDestructive(true);operation.SetRunParallel(false);operation.Build();
    if (!operation.IsDone() || operation.HasErrors()) throw Error("kernel_failure",message);
    return operation.Shape();
  };
  // Oversweep and intersect only with the two end planes, so fractional
  // turn counts produce the same profile phase as integer turn counts.
  const auto clip=BRepPrimAPI_MakeBox(origin.Translated(gp_Vec(-radius-pitch,-radius-pitch,0)),2*(radius+pitch),2*(radius+pitch),length).Shape();
  BRepAlgoAPI_Common trim;
  const auto ridge=boolean(trim,sweep.Shape(),clip,"Thread end trimming failed");
  const auto core=BRepPrimAPI_MakeCylinder(gp_Ax2(origin,gp_Dir(0,0,1)),root,length).Shape();
  BRepAlgoAPI_Fuse join;
  const auto threaded=boolean(join,core,ridge,"Thread core and ridge fusion failed");
  // A 45-degree lead at +Z eases entry without changing the nominal
  // diameters of the full-height threaded portion.
  const double lead=std::min(depth,length/4),clearance=pitch/4;
  // Keep the envelope off the helical crest except where the chamfer crosses
  // it; coincident cylindrical/swept crest faces make an unnecessary Boolean.
  const auto barrel=BRepPrimAPI_MakeCylinder(gp_Ax2(origin,gp_Dir(0,0,1)),radius+clearance,length-lead-clearance).Shape();
  const auto tip=BRepPrimAPI_MakeCone(gp_Ax2(origin.Translated(gp_Vec(0,0,length-lead-clearance)),gp_Dir(0,0,1)),radius+clearance,radius-lead,lead+clearance).Shape();
  BRepAlgoAPI_Fuse envelope;
  const auto limit=boolean(envelope,barrel,tip,"Thread lead envelope failed");
  BRepAlgoAPI_Common lead_trim;
  const auto result=boolean(lead_trim,threaded,limit,"Thread lead trimming failed");
  if (count(result,TopAbs_SOLID)!=1) throw Error("invalid_shape","Thread must produce exactly one connected solid");
  return result;
}
}

Json inspect_step(const std::string& content) {
  try {
    const auto shape=read_step(content);ShapeMap solids;TopExp::MapShapes(shape,TopAbs_SOLID,solids);
    if(solids.Extent()>4096)throw Error("limit_exceeded","STEP inspection permits at most 4096 solids");
    Json result={{"valid",true},{"meshable",true},{"solid_count",solids.Extent()},{"solids",Json::array()},{"errors",Json::array()}};
    try{check_shape(shape);}catch(const Error& e){result["valid"]=false;result["meshable"]=false;result["errors"].push_back(e.json());}
    for(int i=1;i<=solids.Extent();++i) {
      const auto& solid=solids(i);GProp_GProps volume;BRepGProp::VolumeProperties(solid,volume);
      Json entry={{"index",i},{"valid",true},{"meshable",false},{"bounds_mm",bounds(solid)},
        {"volume_mm3",std::isfinite(volume.Mass())?Json(volume.Mass()):Json(nullptr)},{"errors",Json::array()}};
      try{check_shape(solid);}catch(const Error& e){entry["valid"]=false;entry["errors"].push_back(e.json());}
      if(entry.at("valid")==true)try{
        BRepMesh_IncrementalMesh mesh(solid,.1,false,.5,false);int j=0;
        if(!mesh.IsDone())throw Error("kernel_failure","Solid tessellation failed");
        for(TopExp_Explorer f(solid,TopAbs_FACE);f.More();f.Next())tessellated_faces(TopoDS::Face(f.Current()),{{"solid_index",i},{"face_id","face-"+std::to_string(++j)}});
        entry["meshable"]=true;
      }catch(const Error& e){entry["errors"].push_back(e.json());}
      if(entry.at("meshable")==false)result["meshable"]=false;
      result["solids"].push_back(std::move(entry));
    }
    return result;
  }catch(const Standard_Failure& e){throw occt_error(e);}
}

BuiltModel::BuiltModel(const Json& model) : BuiltModel(model,FeatureCache{}) {}
BuiltModel::BuiltModel(const Json& model,const FeatureCache& cache) : impl_(std::make_unique<Impl>()) {
  validate_model(model);
  impl_->model=model;
  std::map<std::string, TopoDS_Shape> shapes;
  std::map<std::string, gp_Ax2> planes;
  const auto& parameters = model.at("parameters");
  for (const auto& feature : model.at("features")) {
    const auto id = text_field(feature, "id");
    const auto type = text_field(feature, "type");
    try {
      if(cache.diagnostics)(*cache.diagnostics)["feature_hits"][id]=false;
      if(cache.load&&cache.keys.contains(id)) {
        std::unique_ptr<FeatureGeometry> restored;
        std::optional<gp_Ax2> plane;
        try {
          if(auto saved=cache.load(cache.keys.at(id).get<std::string>())) {
            if(saved->at("feature_id")!=id)throw Error("cache_miss","Cached feature identity differs");
            restored=std::make_unique<FeatureGeometry>(restore_feature(feature,parameters,impl_->features,saved->at("snapshot")));
            if(is_sketch_feature_type(type)){plane=sketch_plane(feature,parameters,planes,restored->shape);check_sketch_shape(restored->shape,*plane);}
          }
        }catch(const std::exception&) {restored.reset();}
        // Publish to this process's feature map only after restoration succeeds.
        if(restored) {
          shapes.emplace(id,restored->shape);impl_->features.emplace(id,std::move(*restored));
          if(plane)planes.emplace(id,*plane);
          if(cache.diagnostics)(*cache.diagnostics)["feature_hits"][id]=true;
          continue;
        }
      }
      TopoDS_Shape shape;
      std::unique_ptr<FeatureGeometry> assembly;
      Json history=Json::array();
      bool history_truncated=false;
      const auto position = feature.contains("origin") ? vector3(feature.at("origin"), parameters) : std::array<double,3>{0,0,0};
      const gp_Pnt origin(position[0], position[1], position[2]);
      if(type=="curve") {
        shape=curve_wire(feature.at("path").at("segments"),parameters).wire;
      } else if(type=="curve_project"||type=="surface_project") {
        shape=project_geometry(feature,parameters,impl_->features.at(text_field(feature,"input")),impl_->features.at(text_field(feature,"target")));
      } else if(type=="surface_fill") {
        shape=filling_surface(feature,parameters,impl_->features);
      } else if(type=="surface_gordon") {
        const auto surface=gordon_surface(feature,parameters);BRepBuilderAPI_MakeFace face(surface,0,1,0,1,1e-7);if(!face.IsDone())throw Error("kernel_failure","Gordon face construction failed");shape=face.Face();
      } else if(type=="surface_bezier"||type=="surface_bspline") {
        const auto surface=parametric_surface(feature,parameters);double first_u,last_u,first_v,last_v;surface->Bounds(first_u,last_u,first_v,last_v);
        if(!std::isfinite(first_u)||!std::isfinite(last_u)||!std::isfinite(first_v)||!std::isfinite(last_v))throw Error("invalid_shape","Surface parameter domain must be finite");
        BRepBuilderAPI_MakeFace face(surface,first_u,last_u,first_v,last_v,1e-7);
        if(!face.IsDone())throw Error("kernel_failure","Parametric surface face creation failed");shape=face.Face();
      } else if(type=="surface_trim") {
        const auto input=text_field(feature,"input");const auto& source=impl_->features.at(input);
        if(source.faces.Extent()!=1)throw Error("invalid_model","Surface trim requires one exact face");
        const auto face=TopoDS::Face(source.faces(1));double first_u,last_u,first_v,last_v;BRepTools::UVBounds(face,first_u,last_u,first_v,last_v);
        if(feature.contains("boundary"))shape=uv_trim_face(face,feature,parameters);
        else {
        const auto u0=scalar(feature.at("u_range")[0],parameters,"dimensionless"),u1=scalar(feature.at("u_range")[1],parameters,"dimensionless"),
          v0=scalar(feature.at("v_range")[0],parameters,"dimensionless"),v1=scalar(feature.at("v_range")[1],parameters,"dimensionless");
        if(u0<first_u-1e-12||u1>last_u+1e-12||v0<first_v-1e-12||v1>last_v+1e-12)throw Error("invalid_model","Surface trim ranges must stay inside the source face UV domain",{{"source_feature_id",input},{"source_u_range",{first_u,last_u}},{"source_v_range",{first_v,last_v}}});
        BRepBuilderAPI_MakeFace operation(BRep_Tool::Surface(face),u0,u1,v0,v1,1e-7);
        if(!operation.IsDone())throw Error("kernel_failure","Exact UV surface trimming failed");shape=operation.Face();
        if(face.Orientation()==TopAbs_REVERSED)shape.Reverse();
        }
        require_surface_containment(face,TopoDS::Face(shape));
        history.push_back({{"source_feature_id",input},{"source_kind","face"},{"source_id","face-1"},{"relation","modified"},{"result_kind","face"},{"result_id","face-1"}});
      } else if(type=="surface_shell") {
        const auto tolerance=feature.at("tolerance").get<double>();BRepBuilderAPI_Sewing operation(tolerance,true,true,false,false);
        ShapeMap original_faces;double source_area=0;
        for(const auto& value:feature.at("inputs")) {
          const auto input=value.get<std::string>();const auto& source=impl_->features.at(input);
          for(int f=1;f<=source.faces.Extent();++f) {
            if(original_faces.Contains(source.faces(f)))throw Error("invalid_model","Surface shell inputs repeat an exact source face");
            original_faces.Add(source.faces(f));GProp_GProps area;BRepGProp::SurfaceProperties(source.faces(f),area);source_area+=area.Mass();operation.Add(source.faces(f));
          }
        }
        if(original_faces.Extent()>256)throw Error("limit_exceeded","Surface shell permits at most 256 source patches");
        operation.Perform();shape=operation.SewedShape();
        if(shape.IsNull()||operation.NbMultipleEdges()!=0||operation.NbDeletedFaces()!=0||count(shape,TopAbs_FACE)!=original_faces.Extent())
          throw Error("invalid_shape","Surface sewing must preserve every patch and produce a manifold shell",{{"nonmanifold_edges",operation.NbMultipleEdges()},{"deleted_faces",operation.NbDeletedFaces()}});
        if(shape.ShapeType()==TopAbs_FACE) {TopoDS_Shell shell;BRep_Builder builder;builder.MakeShell(shell);builder.Add(shell,shape);shape=shell;}
        check_surface_intent(feature,shape);
        GProp_GProps area;BRepGProp::SurfaceProperties(shape,area);
        if(std::abs(area.Mass()-source_area)>std::max(1e-8,source_area*1e-8))throw Error("invalid_shape","Surface sewing changed source patch area beyond tolerance");
        SewingHistory evidence{operation};for(const auto& value:feature.at("inputs")){const auto input=value.get<std::string>();record_history(evidence,impl_->features.at(input),input,shape,history,history_truncated);}
      } else if(type=="surface_solid") {
        const auto input=text_field(feature,"input");const auto& source=impl_->features.at(input);
        if(source.shape.ShapeType()!=TopAbs_SHELL||BRepCheck_Shell(TopoDS::Shell(source.shape)).Closed()!=BRepCheck_NoError)throw Error("invalid_model","Solid materialization requires one closed manifold shell");
        auto shell=TopoDS::Shell(source.shape);if(feature.value("reverse",false))shell.Reverse();
        BRepBuilderAPI_MakeSolid operation(shell);if(!operation.IsDone())throw Error("kernel_failure","Closed shell solid materialization failed");shape=operation.Solid();
        // No implicit normal repair: the caller can explicitly reverse a globally
        // inward shell; inconsistent local face orientations fail validation.
        check_shape(shape);record_history(operation,source,input,shape,history,history_truncated);
      } else if (type == "box") {
        const auto size = vector3(feature.at("size"), parameters);
        shape = BRepPrimAPI_MakeBox(origin, size[0], size[1], size[2]).Shape();
      } else if (type == "cylinder") {
        shape = BRepPrimAPI_MakeCylinder(gp_Ax2(origin, gp_Dir(0,0,1)), scalar(feature.at("radius"), parameters), scalar(feature.at("height"), parameters)).Shape();
      } else if (type == "external_thread") {
        shape=external_thread(scalar(feature.at("major_diameter"),parameters),scalar(feature.at("pitch"),parameters),scalar(feature.at("length"),parameters),origin,feature.value("handedness",std::string("right"))=="left");
      } else if (type == "sketch") {
        const auto plane = parameter_plane(feature.at("workplane"),parameters);
        planes.emplace(id,plane);
        const auto kind=text_field(feature.at("profile"),"type");shape=(kind=="text"||kind=="svg"||kind=="dxf")?authoring_face(feature.at("profile"),parameters,plane):sketch_face(feature.at("profile"),parameters,plane);
      } else if (type=="sketch_cut"||type=="sketch_fuse"||type=="sketch_intersection") {
        const auto left=text_field(feature,"left"),right=text_field(feature,"right");const auto plane=planes.at(left);
        if(std::abs(plane.Direction().Dot(planes.at(right).Direction()))<1-1e-9||gp_Pln(plane).Distance(planes.at(right).Location())>1e-7)
          throw Error("invalid_model","Sketch booleans require coplanar inputs");
        const auto perform=[&](auto& operation) {
          NCollection_List<TopoDS_Shape> a,b;a.Append(shapes.at(left));b.Append(shapes.at(right));operation.SetArguments(a);operation.SetTools(b);
          operation.SetNonDestructive(true);operation.SetRunParallel(false);operation.Build();
          if(!operation.IsDone()||operation.HasErrors())throw Error("kernel_failure","Sketch boolean failed");
          operation.SimplifyResult(true,true);shape=operation.Shape();
          record_history(operation,impl_->features.at(left),left,shape,history,history_truncated);
          record_history(operation,impl_->features.at(right),right,shape,history,history_truncated);
        };
        if(type=="sketch_cut"){BRepAlgoAPI_Cut operation;perform(operation);}
        else if(type=="sketch_fuse"){BRepAlgoAPI_Fuse operation;perform(operation);}
        else{BRepAlgoAPI_Common operation;perform(operation);}
        planes.emplace(id,plane);
      } else if (type=="sketch_offset") {
        const auto input=text_field(feature,"input");const auto plane=planes.at(input);std::vector<TopoDS_Wire> wires;std::vector<std::unique_ptr<BRepOffsetAPI_MakeOffset>> operations;std::vector<std::pair<TopoDS_Edge,TopoDS_Edge>> circles;
        for(TopExp_Explorer face_it(shapes.at(input),TopAbs_FACE);face_it.More();face_it.Next()) {
          const auto face=TopoDS::Face(face_it.Current());const auto outer=BRepTools::OuterWire(face);
          // Offset each exact boundary as a normalized disc. OCCT 8's combined
          // annular-face offset fails valid inward circles; separate contours
          // preserve the same authored material intent and explicit holes.
          for(TopExp_Explorer boundary(face,TopAbs_WIRE);boundary.More();boundary.Next()) {
            const auto wire=TopoDS::Wire(boundary.Current());BRepBuilderAPI_MakeFace disc(gp_Pln(plane),wire,true);
            if(!disc.IsDone()||!BRepCheck_Analyzer(disc.Face()).IsValid())throw Error("invalid_shape","Sketch offset boundary is invalid");
            const auto distance=scalar(feature.at("distance"),parameters)*(wire.IsSame(outer)?1:-1);
            ShapeMap edges;TopExp::MapShapes(wire,TopAbs_EDGE,edges);
            if(edges.Extent()==1) {
              const auto edge=TopoDS::Edge(edges(1));const BRepAdaptor_Curve curve(edge);
              if(curve.GetType()==GeomAbs_Circle&&std::abs(curve.LastParameter()-curve.FirstParameter()-2*std::numbers::pi)<1e-7) {
                auto circle=curve.Circle();const auto radius=circle.Radius()+distance;
                if(radius<=1e-7)throw Error("invalid_shape","Sketch offset collapsed a circular boundary",{{"source_radius_mm",circle.Radius()},{"offset_mm",distance}});
                // MAT2d inside OCCT can fault on offsets larger than a closed
                // circle. An analytic radius change is exact and bounds collapse
                // before entering that algorithm.
                circle.SetRadius(radius);const auto result=BRepBuilderAPI_MakeEdge(circle).Edge();
                wires.push_back(BRepBuilderAPI_MakeWire(result).Wire());circles.push_back({edge,result});continue;
              }
            }
            auto operation=std::make_unique<BRepOffsetAPI_MakeOffset>(disc.Face(),feature.value("join",std::string("arc"))=="arc"?GeomAbs_Arc:GeomAbs_Intersection,false);
            operation->Perform(distance);
            if(!operation->IsDone())throw Error("kernel_failure","Sketch offset failed; change its distance or input geometry");
            const auto before=wires.size();
            for(TopExp_Explorer result(operation->Shape(),TopAbs_WIRE);result.More();result.Next())wires.push_back(TopoDS::Wire(result.Current()));
            if(wires.size()==before)throw Error("invalid_shape","Sketch offset collapsed a boundary");
            operations.push_back(std::move(operation));
          }
        }
        shape=planar_regions(wires,plane);planes.emplace(id,plane);
        for(auto& operation:operations)record_history(*operation,impl_->features.at(input),input,shape,history,history_truncated);
        const FeatureGeometry target(shape);const auto& source=impl_->features.at(input);
        for(const auto& pair:circles) {
          const auto source_index=source.edges.FindIndex(pair.first),result_index=target.edges.FindIndex(pair.second);
          if(source_index&&result_index)history.push_back({{"source_feature_id",input},{"source_kind","edge"},{"source_id","edge-"+std::to_string(source_index)},
            {"relation","generated"},{"result_kind","edge"},{"result_id","edge-"+std::to_string(result_index)}});
        }
      } else if (type=="sketch_transform"||type=="sketch_instance"||type=="sketch_mirror") {
        const auto input=text_field(feature,"input");gp_Trsf transform;
        if(type=="sketch_mirror")transform.SetMirror(parameter_plane(feature.at("plane"),parameters));else transform=placement_transform(feature,parameters);
        BRepBuilderAPI_Transform operation(shapes.at(input),transform,true);
        if(!operation.IsDone())throw Error("kernel_failure","Sketch transform failed");shape=operation.Shape();
        planes.emplace(id,sketch_plane(feature,parameters,planes,shape));record_history(operation,impl_->features.at(input),input,shape,history,history_truncated);
      } else if(type=="sketch_face"||type=="sketch_projection") {
        const auto input=text_field(feature,"input");const auto& source=impl_->features.at(input);
        const auto selected=select_faces(source,feature.at("faces"),parameters,input);std::vector<TopoDS_Face> faces;
        std::vector<std::pair<TopoDS_Edge,TopoDS_Edge>> projected_edges;std::vector<std::pair<int,TopoDS_Face>> projected_faces;
        const auto target=type=="sketch_projection"?parameter_plane(feature.at("workplane"),parameters):gp_Ax2{};
        for(const auto index:selected) {
          const auto face=TopoDS::Face(source.faces(index));const BRepAdaptor_Surface surface(face);
          if(surface.GetType()!=GeomAbs_Plane)throw Error("invalid_model","Derived sketches support planar faces only");
          if(type=="sketch_face")faces.push_back(face);
          else {
            if(std::abs(surface.Plane().Axis().Direction().Dot(target.Direction()))<1e-9)throw Error("invalid_model","Edge-on face projection has no sketch area");
            std::vector<TopoDS_Wire> projected;
            for(TopExp_Explorer it(face,TopAbs_WIRE);it.More();it.Next()) {
              BRepBuilderAPI_MakeWire wire;
              for(BRepTools_WireExplorer edge(TopoDS::Wire(it.Current()),face);edge.More();edge.Next()) {
                const auto original=edge.Current();double first,last;TopLoc_Location location;auto curve=BRep_Tool::Curve(original,location,first,last);
                if(curve.IsNull()||BRep_Tool::Degenerated(original))throw Error("invalid_shape","Face projection requires nondegenerate exact boundary curves");
                curve=occ::down_cast<Geom_Curve>(curve->Transformed(location.Transformation()));
                // Trim first: projection changes line parameter scale and conic
                // parameter origin. Native projection returns the correct trim
                // range; reusing source edge parameters disconnects boundaries.
                auto projection=GeomProjLib::ProjectOnPlane(new Geom_TrimmedCurve(curve,first,last),new Geom_Plane(gp_Pln(target)),target.Direction(),true);
                if(projection.IsNull())throw Error("kernel_failure","Exact curve projection failed");
                BRepBuilderAPI_MakeEdge make(projection);if(!make.IsDone())throw Error("invalid_shape","Projected curve became degenerate");
                auto result=make.Edge();if(original.Orientation()==TopAbs_REVERSED)result.Reverse();wire.Add(result);projected_edges.push_back({original,result});
              }
              if(!wire.IsDone())throw Error("invalid_shape","Projected boundary is disconnected");projected.push_back(wire.Wire());
            }
            const auto regions=planar_regions(projected,target);
            for(TopExp_Explorer it(regions,TopAbs_FACE);it.More();it.Next()){const auto result=TopoDS::Face(it.Current());faces.push_back(result);projected_faces.push_back({index,result});}
          }
        }
        shape=face_compound(faces);planes.emplace(id,sketch_plane(feature,parameters,planes,shape));
        if(type=="sketch_face") {
          for(const auto index:selected)history.push_back({{"source_feature_id",input},{"source_kind","face"},{"source_id","face-"+std::to_string(index)},
            {"relation","unchanged"},{"result_kind","face"},{"result_id","face-"+std::to_string(FeatureGeometry(shape).faces.FindIndex(source.faces(index)))}});
        } else {
          const FeatureGeometry target_geometry(shape);
          const auto append=[&](const std::string& kind,int source_index,int result_index) {
            if(!source_index||!result_index)return;
            if(history.size()>=10000){history_truncated=true;return;}
            history.push_back({{"source_feature_id",input},{"source_kind",kind},{"source_id",kind+"-"+std::to_string(source_index)},
              {"relation","generated"},{"result_kind",kind},{"result_id",kind+"-"+std::to_string(result_index)}});
          };
          for(const auto& pair:projected_faces)append("face",pair.first,target_geometry.faces.FindIndex(pair.second));
          for(const auto& pair:projected_edges)append("edge",source.edges.FindIndex(pair.first),target_geometry.edges.FindIndex(pair.second));
        }
      } else if(type=="sketch_fillet"||type=="sketch_chamfer") {
        const auto input=text_field(feature,"input");const auto& source=impl_->features.at(input);const auto& selector=feature.at("vertices");
        ShapeMap vertices;TopExp::MapShapes(source.shape,TopAbs_VERTEX,vertices);std::vector<TopoDS_Vertex> selected;
        for(int v=1;v<=vertices.Extent();++v) {
          const auto vertex=TopoDS::Vertex(vertices(v));ShapeMap adjacent;
          for(int e=1;e<=source.edges.Extent();++e) {ShapeMap ends;TopExp::MapShapes(source.edges(e),TopAbs_VERTEX,ends);if(ends.Contains(vertex))adjacent.Add(source.edges(e));}
          if(adjacent.Extent()!=2)continue;
          if(selector.is_string()||BRep_Tool::Pnt(vertex).Distance(parameter_point(selector.at("point"),parameters))<=scalar(selector.at("tolerance"),parameters))selected.push_back(vertex);
        }
        if(selector.is_object()&&selected.size()!=selector.at("expected_count").get<std::size_t>()) {
          const auto expected=selector.at("expected_count").get<std::size_t>();
          throw Error(selected.empty()?"selection_missing":selected.size()>expected?"selection_ambiguous":"selection_count_mismatch","Sketch vertex selector did not match its expected count",
            {{"source_feature_id",input},{"expected_count",expected},{"actual_count",selected.size()}});
        }
        if(selected.empty())throw Error("invalid_model","Sketch has no corners eligible for fillet or chamfer");
        std::vector<TopoDS_Face> faces;std::vector<std::unique_ptr<BRepFilletAPI_MakeFillet2d>> operations;
        for(int f=1;f<=source.faces.Extent();++f) {
          const auto face=TopoDS::Face(source.faces(f));auto operation=std::make_unique<BRepFilletAPI_MakeFillet2d>(face);bool changed=false;
          ShapeMap local;TopExp::MapShapes(face,TopAbs_VERTEX,local);
          for(const auto& vertex:selected)if(local.Contains(vertex)) {
            TopoDS_Edge result;
            if(type=="sketch_fillet")result=operation->AddFillet(vertex,scalar(feature.at("radius"),parameters));
            else {
              std::vector<TopoDS_Edge> adjacent;ShapeMap edges;TopExp::MapShapes(face,TopAbs_EDGE,edges);
              for(int e=1;e<=edges.Extent();++e){ShapeMap ends;TopExp::MapShapes(edges(e),TopAbs_VERTEX,ends);if(ends.Contains(vertex))adjacent.push_back(TopoDS::Edge(edges(e)));}
              result=operation->AddChamfer(adjacent.at(0),adjacent.at(1),scalar(feature.at("distance"),parameters),scalar(feature.at("distance"),parameters));
            }
            if(result.IsNull())throw Error("kernel_failure","Sketch corner operation failed; change its dimension or selected corner");changed=true;
          }
          if(changed){operation->Build();if(!operation->IsDone())throw Error("kernel_failure","Sketch corner operation failed");faces.push_back(TopoDS::Face(operation->Shape()));operations.push_back(std::move(operation));}
          else faces.push_back(face);
        }
        shape=face_compound(faces);planes.emplace(id,planes.at(input));
        for(auto& operation:operations){CornerHistory evidence{*operation};record_history(evidence,source,input,shape,history,history_truncated);}
      } else if(type=="scale") {
        const auto input=text_field(feature,"input");const auto& source=impl_->features.at(input);
        const auto origin=parameter_point(feature.at("origin"),parameters);const auto& factors=feature.at("factors");
        std::array<double,3> values;
        for(int i=0;i<3;++i)values[i]=scalar(factors.is_array()?factors[i]:factors,parameters,"dimensionless");
        if(values[0]==values[1]&&values[1]==values[2]) {
          gp_Trsf transform;transform.SetScale(origin,values[0]);BRepBuilderAPI_Transform operation(source.shape,transform,true);
          if(!operation.IsDone())throw Error("kernel_failure","Uniform scaling failed");shape=operation.Shape();record_history(operation,source,input,shape,history,history_truncated);
        } else {
          gp_GTrsf transform;transform.SetVectorialPart(gp_Mat(values[0],0,0,0,values[1],0,0,0,values[2]));
          transform.SetTranslationPart(gp_XYZ(origin.X()*(1-values[0]),origin.Y()*(1-values[1]),origin.Z()*(1-values[2])));
          BRepBuilderAPI_GTransform operation(source.shape,transform,true);
          if(!operation.IsDone())throw Error("kernel_failure","Nonuniform scaling failed");shape=operation.Shape();record_history(operation,source,input,shape,history,history_truncated);
        }
      } else if(type=="draft") {
        const auto input=text_field(feature,"input");const auto& source=impl_->features.at(input);
        BRepBuilderAPI_Copy copy(source.shape);BRepOffsetAPI_DraftAngle operation(copy.Shape());
        const auto neutral=parameter_plane(feature.at("neutral_plane"),parameters);
        const auto pull=parameter_direction(feature.at("direction"),parameters);
        if(std::abs(pull.Dot(neutral.Direction()))<1e-9)throw Error("invalid_model","Draft pull direction must cross its neutral plane");
        for(const auto index:select_faces(source,feature.at("faces"),parameters,input)) {
          const auto face=TopoDS::Face(copy.ModifiedShape(source.faces(index)));const auto kind=BRepAdaptor_Surface(face).GetType();
          if(kind!=GeomAbs_Plane&&kind!=GeomAbs_Cylinder&&kind!=GeomAbs_Cone)throw Error("invalid_model","Draft supports planar, cylindrical and conical faces",{{"source_feature_id",input}});
          operation.Add(face,pull,scalar(feature.at("angle_deg"),parameters,"deg")*std::numbers::pi/180,gp_Pln(neutral));
          if(!operation.AddDone())throw Error("kernel_failure","Draft face could not be added",{{"status",static_cast<int>(operation.Status())}});
        }
        operation.Build();if(!operation.IsDone())throw Error("kernel_failure","Draft failed; angle or geometry requires a topology change",{{"status",static_cast<int>(operation.Status())}});
        shape=operation.Shape();record_history(operation,source,input,shape,history,history_truncated,&copy);
      } else if(type=="twist_extrude") {
        const auto input=text_field(feature,"input");const auto& source=impl_->features.at(input);const auto plane=planes.at(input);
        const double distance=scalar(feature.at("distance"),parameters),angle=scalar(feature.at("angle_deg"),parameters,"deg")*std::numbers::pi/180;
        const auto center=feature.contains("center")?parameter_point(feature.at("center"),parameters):plane.Location();
        if(std::abs(gp_Vec(plane.Location(),center).Dot(gp_Vec(plane.Direction())))>1e-7)throw Error("invalid_model","Twist center must lie in the sketch plane");
        if(std::abs(angle)<1e-12) {
          BRepPrimAPI_MakePrism operation(source.shape,gp_Vec(plane.Direction())*distance,true);
          if(!operation.IsDone())throw Error("kernel_failure","Zero-angle twist extrusion failed");shape=operation.Shape();record_history(operation,source,input,shape,history,history_truncated);
        } else {
          const auto end=center.Translated(gp_Vec(plane.Direction())*distance);
          const auto spine=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(center,end).Edge()).Wire();
          const gp_Ax3 frame(center,plane.Direction(),plane.XDirection());
          const occ::handle<Geom_CylindricalSurface> cylinder=new Geom_CylindricalSurface(frame,1.0);
          const double extent=std::hypot(angle,distance);
          const occ::handle<Geom2d_Line> helix=new Geom2d_Line(gp_Pnt2d(0,0),gp_Dir2d(angle,distance));
          const auto guide_edge=BRepBuilderAPI_MakeEdge(helix,cylinder,0,extent).Edge();
          if(!BRepLib::BuildCurve3d(guide_edge,1e-8))throw Error("kernel_failure","Twist auxiliary helix construction failed");
          const auto guide=BRepBuilderAPI_MakeWire(guide_edge).Wire();
          std::vector<TopoDS_Shape> regions;
          for(TopExp_Explorer it(source.shape,TopAbs_FACE);it.More();it.Next()) {
            const auto wires=section_wires(TopoDS::Face(it.Current()),plane);std::vector<std::unique_ptr<BRepOffsetAPI_MakePipeShell>> operations;
            for(const auto& wire:wires) {
              auto operation=std::make_unique<BRepOffsetAPI_MakePipeShell>(spine);operation->SetMode(guide,false,BRepFill_NoContact);
              operation->SetTolerance(1e-7,1e-7,1e-6);operation->SetMaxSegments(1024);operation->Add(wire,false,false);operation->Build();
              if(!operation->IsDone()||!operation->MakeSolid())throw Error("kernel_failure","Twist extrusion failed to make a closed solid");
              check_shape(operation->Shape());operations.push_back(std::move(operation));
            }
            auto region=operations.front()->Shape();for(std::size_t i=1;i<operations.size();++i)region=extrusion_boolean<BRepAlgoAPI_Cut>(region,operations[i]->Shape());
            regions.push_back(region);
            for(const auto& operation:operations)record_history(*operation,source,input,region,history,history_truncated);
          }
          shape=join_extrusion_regions(regions);
        }
      } else if (type == "extrude") {
        const auto input = text_field(feature,"input");
        const auto plane=planes.at(input);const auto direction=feature.contains("direction")?parameter_direction(feature.at("direction"),parameters):plane.Direction();
        if(std::abs(direction.Dot(plane.Direction()))<1e-9)throw Error("invalid_model","Extrusion direction must cross the sketch plane");
        if(feature.contains("until"))shape=target_extrusion(shapes.at(input),direction,shapes.at(text_field(feature,"target")),text_field(feature,"until"));
        else {
          const double distance=scalar(feature.at("distance"),parameters),taper=feature.contains("taper_deg")?scalar(feature.at("taper_deg"),parameters,"deg"):0;
          std::vector<TopoDS_Shape> regions;
          for(const double sign:feature.value("both",false)?std::vector<double>{1,-1}:std::vector<double>{1}) {
            const auto travel=gp_Vec(direction)*(sign*distance);
            if(std::abs(taper)<1e-12) {
              BRepPrimAPI_MakePrism operation(shapes.at(input),travel,true);
              if(!operation.IsDone())throw Error("kernel_failure","Extrusion failed");
              regions.push_back(operation.Shape());record_history(operation,impl_->features.at(input),input,operation.Shape(),history,history_truncated);
            } else for(TopExp_Explorer face(shapes.at(input),TopAbs_FACE);face.More();face.Next())regions.push_back(taper_region(TopoDS::Face(face.Current()),plane,travel,taper));
          }
          shape=join_extrusion_regions(regions);
        }
      } else if (type == "revolve") {
        const auto& axis = feature.at("axis");
        BRepPrimAPI_MakeRevol operation(shapes.at(text_field(feature,"input")),gp_Ax1(parameter_point(axis.at("origin"),parameters),parameter_direction(axis.at("direction"),parameters)),scalar(feature.at("angle_deg"),parameters,"deg")*std::numbers::pi/180,true);
        if (!operation.IsDone()) throw Error("kernel_failure","Revolve failed");
        shape = operation.Shape();
        const auto input=text_field(feature,"input");
        record_history(operation,impl_->features.at(input),input,shape,history,history_truncated);
      } else if (type == "loft") {
        std::vector<std::vector<TopoDS_Wire>> profiles;
        for(const auto& section:feature.at("sections")) {
          const auto input=section.get<std::string>();const auto& source=shapes.at(input);
          if(count(source,TopAbs_FACE)!=1)throw Error("invalid_model","Each loft section must contain exactly one planar region",{{"source_feature_id",input}});
          profiles.push_back(section_wires(TopoDS::Face(source),planes.at(input)));
          if(profiles.back().size()!=profiles.front().size())throw Error("invalid_model","Loft sections must retain the same number of holes",{{"source_feature_id",input}});
        }
        if(profiles.front().size()>2&&!feature.contains("hole_order"))throw Error("invalid_model","Lofts with multiple holes require explicit hole_order landmarks to identify corresponding boundaries");
        if(feature.contains("hole_order"))for(std::size_t section=0;section<profiles.size();++section) {
          const auto& landmarks=feature.at("hole_order")[section];const auto& wires=profiles[section];
          if(landmarks.size()+1!=wires.size())throw Error("invalid_model","Hole landmark count must match every interior boundary");
          const auto input=feature.at("sections")[section].get<std::string>();std::vector<TopoDS_Wire> ordered{wires.front()};std::set<std::size_t> used;
          for(const auto& landmark:landmarks) {
            std::vector<std::size_t> matches;const auto point=parameter_point(landmark.at("point"),parameters);
            const double tolerance=scalar(landmark.at("tolerance"),parameters);
            for(std::size_t i=1;i<wires.size();++i) {GProp_GProps props;BRepGProp::SurfaceProperties(BRepBuilderAPI_MakeFace(gp_Pln(planes.at(input)),wires[i]).Face(),props);
              if(props.CentreOfMass().Distance(point)<=tolerance)matches.push_back(i);}
            if(matches.size()!=1||!used.insert(matches.front()).second)throw Error("selection_ambiguous","Loft hole landmark must uniquely identify one unused boundary centroid",{{"source_feature_id",input},{"actual_count",matches.size()}});
            ordered.push_back(wires[matches.front()]);
          }
          profiles[section]=std::move(ordered);
        }
        std::vector<std::unique_ptr<BRepOffsetAPI_ThruSections>> operations;
        for(std::size_t boundary=0;boundary<profiles.front().size();++boundary) {
          auto operation=std::make_unique<BRepOffsetAPI_ThruSections>(true,feature.value("ruled",false));
          operation->SetMutableInput(false);
          if(feature.contains("start_vertex"))operation->AddVertex(BRepBuilderAPI_MakeVertex(parameter_point(feature.at("start_vertex"),parameters)).Vertex());
          for(const auto& profile:profiles)operation->AddWire(profile[boundary]);
          if(feature.contains("end_vertex"))operation->AddVertex(BRepBuilderAPI_MakeVertex(parameter_point(feature.at("end_vertex"),parameters)).Vertex());
          operation->CheckCompatibility(true);operation->Build();
          if(!operation->IsDone())throw Error("kernel_failure","Loft failed",{{"boundary",boundary}});
          check_shape(operation->Shape());
          BRepAlgoAPI_Check self_interference(operation->Shape(),false,true);
          if(!self_interference.IsValid())throw Error("invalid_shape","Loft boundary self-intersects between its sections",{{"boundary",boundary}});
          operations.push_back(std::move(operation));
        }
        shape=operations.front()->Shape();
        for(std::size_t i=1;i<operations.size();++i) {
          const auto& hole=operations[i]->Shape();
          // An inner loft that leaves or crosses the outer body changes material
          // intent. Reject that case rather than clipping away its escaped region.
          GProp_GProps all,inside;BRepGProp::VolumeProperties(hole,all);
          const auto common=extrusion_boolean<BRepAlgoAPI_Common>(operations.front()->Shape(),hole);
          BRepGProp::VolumeProperties(common,inside);
          if(std::abs(all.Mass()-inside.Mass())>std::max(1e-7,all.Mass()*1e-7))throw Error("invalid_shape","Loft hole escapes its outer boundary",{{"boundary",i}});
          for(std::size_t previous=1;previous<i;++previous) {
            // An empty boolean Common alone is insufficient for ruled rational
            // lofts: OCCT can miss their intersection until STEP reimport.
            // Independent exact boundary distance also rejects contact/crossing.
            BRepExtrema_DistShapeShape separation(operations[previous]->Shape(),hole);
            if(!separation.IsDone())throw Error("kernel_failure","Loft hole separation could not be verified");
            if(separation.Value()<=1e-7)throw Error("invalid_shape","Loft hole tracks touch or cross between sections",{{"boundary",i},{"other_boundary",previous}});
            const auto intersection=extrusion_boolean<BRepAlgoAPI_Common>(operations[previous]->Shape(),hole);
            GProp_GProps overlap;BRepGProp::VolumeProperties(intersection,overlap,1e-9);
            if(overlap.Mass()>1e-8)throw Error("invalid_shape","Loft hole tracks overlap between sections",{{"boundary",i},{"other_boundary",previous}});
          }
          shape=extrusion_boolean<BRepAlgoAPI_Cut>(shape,hole);
        }
        check_shape(shape);
        BRepAlgoAPI_Check final_interference(shape,false,true);
        if(!final_interference.IsValid())throw Error("invalid_shape","Loft boundaries intersect between sections");
        for(const auto& operation:operations)for(const auto& section:feature.at("sections")) {
          const auto input=section.get<std::string>();record_history(*operation,impl_->features.at(input),input,shape,history,history_truncated);
        }
      } else if (type == "sweep") {
        std::vector<std::string> inputs;if(feature.contains("input"))inputs.push_back(text_field(feature,"input"));else for(const auto& section:feature.at("sections"))inputs.push_back(section.get<std::string>());
        const auto& input=inputs.front();
        const auto& path = feature.at("path");
        Json segments=path.is_object()?path.at("segments"):Json::array();
        if (path.is_array()) for (std::size_t i=1;i<path.size();++i)
          segments.push_back({{"type","line"},{"start",path[i-1]},{"end",path[i]}});
        const auto spine=curve_wire(segments,parameters);
        if (spine.start.Distance(planes.at(input).Location()) > 1e-7 || std::abs(gp_Dir(spine.tangent).Dot(planes.at(input).Direction())) < 1-1e-9) {
          throw Error("invalid_model","Sweep path must start at the sketch origin with a tangent perpendicular to its plane");
        }
        if(inputs.size()==1&&!feature.contains("orientation")&&!feature.contains("binormal")&&!feature.contains("guide")&&!feature.contains("transition")) {
          BRepOffsetAPI_MakePipe operation(spine.wire,shapes.at(input));operation.Build();
          if(!operation.IsDone())throw Error("kernel_failure","Sweep failed");shape=operation.Shape();
          record_history(operation,impl_->features.at(input),input,shape,history,history_truncated);
        } else if(inputs.size()>1) {
          std::vector<TopoDS_Face> faces;std::vector<gp_Ax2> section_planes;std::vector<const FeatureGeometry*> sources;
          for(const auto& id:inputs) {
            if(count(shapes.at(id),TopAbs_FACE)!=1)throw Error("invalid_model","Each varying sweep section must have exactly one planar region",{{"source_feature_id",id}});
            TopExp_Explorer face(shapes.at(id),TopAbs_FACE);faces.push_back(TopoDS::Face(face.Current()));section_planes.push_back(planes.at(id));sources.push_back(&impl_->features.at(id));
          }
          sweep_stations(spine,section_planes);
          shape=controlled_sweep(feature,parameters,spine,faces,section_planes,history,history_truncated,sources,inputs);
        } else {
          std::vector<TopoDS_Shape> regions;
          for(TopExp_Explorer face(shapes.at(input),TopAbs_FACE);face.More();face.Next())regions.push_back(controlled_sweep(feature,parameters,spine,{TopoDS::Face(face.Current())},{planes.at(input)},history,history_truncated,{&impl_->features.at(input)},inputs));
          shape=join_extrusion_regions(regions);
        }
      } else if (type == "assembly") {
        assembly=std::make_unique<FeatureGeometry>(build_assembly(feature,parameters,impl_->features,history,history_truncated));
        shape=assembly->shape;
      } else if (type == "transform" || type == "instance") {
        const auto transform=placement_transform(feature,parameters);
        BRepBuilderAPI_Transform operation(shapes.at(text_field(feature,"input")),transform,true);
        if (!operation.IsDone()) throw Error("kernel_failure","Transform failed");
        shape=operation.Shape();
        const auto input=text_field(feature,"input");
        record_history(operation,impl_->features.at(input),input,shape,history,history_truncated);
      } else if(type=="mirror") {
        const auto input=text_field(feature,"input");gp_Trsf transform;transform.SetMirror(parameter_plane(feature.at("plane"),parameters));
        BRepBuilderAPI_Transform operation(shapes.at(input),transform,true);if(!operation.IsDone())throw Error("kernel_failure","Mirror failed");
        shape=operation.Shape();record_history(operation,impl_->features.at(input),input,shape,history,history_truncated);
      } else if(type=="split") {
        const auto input=text_field(feature,"input");const auto plane=parameter_plane(feature.at("plane"),parameters);const auto source=shapes.at(input);
        BRepBuilderAPI_MakeFace face{gp_Pln(plane)};
        if(!face.IsDone())throw Error("kernel_failure","Split plane creation failed");
        const auto half=[&](bool top) {return BRepPrimAPI_MakeHalfSpace(face.Face(),plane.Location().Translated(gp_Vec(plane.Direction())*(top?1:-1))).Solid();};
        std::unique_ptr<BRepAlgoAPI_Common> top,bottom;
        const auto clip=[&](bool side) {
          auto operation=std::make_unique<BRepAlgoAPI_Common>();NCollection_List<TopoDS_Shape> a,b;a.Append(source);b.Append(half(side));
          operation->SetArguments(a);operation->SetTools(b);operation->SetNonDestructive(true);operation->SetRunParallel(false);operation->Build();
          if(!operation->IsDone()||operation->HasErrors())throw Error("kernel_failure","Split by plane failed");
          check_shape(operation->Shape());return operation;
        };
        top=clip(true);bottom=clip(false); // Both sides must contain material: misses and tangencies fail intent.
        const auto keep=text_field(feature,"keep");
        if(keep=="top")shape=top->Shape();else if(keep=="bottom")shape=bottom->Shape();
        else {TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);builder.Add(compound,top->Shape());builder.Add(compound,bottom->Shape());shape=compound;}
        if(keep!="bottom")record_history(*top,impl_->features.at(input),input,shape,history,history_truncated);
        if(keep!="top")record_history(*bottom,impl_->features.at(input),input,shape,history,history_truncated);
      } else if (type == "pattern" || type == "circular_pattern") {
        BRep_Builder builder;
        TopoDS_Compound compound; builder.MakeCompound(compound);
        const auto delta=type=="pattern"?vector3(feature.at("step"),parameters):std::array<double,3>{0,0,0};
        const auto input=text_field(feature,"input");
        const auto copies=pattern_count(feature.at("count"),parameters);
        const auto& source=impl_->features.at(input);
        replication_budget(static_cast<std::size_t>(count(source.shape,TopAbs_SOLID))*copies,static_cast<std::size_t>(source.faces.Extent())*copies);
        std::vector<std::unique_ptr<BRepBuilderAPI_Transform>> instances;
        for (int i=0; i<copies; ++i) {
          gp_Trsf transform;
          if (type=="pattern") transform.SetTranslation(gp_Vec(delta[0]*i,delta[1]*i,delta[2]*i));
          else {
            const auto& axis=feature.at("axis");
            transform.SetRotation(gp_Ax1(parameter_point(axis.at("origin"),parameters),parameter_direction(axis.at("direction"),parameters)),
              scalar(feature.at("angle_deg"),parameters,"deg")*i*std::numbers::pi/180);
          }
          auto operation=std::make_unique<BRepBuilderAPI_Transform>(shapes.at(input),transform,true);
          if (!operation->IsDone()) throw Error("kernel_failure","Pattern instance failed");
          builder.Add(compound,operation->Shape());
          instances.push_back(std::move(operation));
        }
        shape=compound;
        // Resolve every copy against the completed compound, never against a
        // copy's separate local enumeration. These IDs describe this evaluation.
        const FeatureGeometry target(shape);
        for (std::size_t i=0; i<instances.size() && !history_truncated; ++i)
          record_history(*instances[i],impl_->features.at(input),input,target,history,history_truncated,nullptr,static_cast<int>(i));
      } else if (type == "hole") {
        const auto input=text_field(feature,"input");
        const auto& body=shapes.at(input);
        const auto tool=BRepPrimAPI_MakeCylinder(gp_Ax2(origin,parameter_direction(feature.at("axis"),parameters)),scalar(feature.at("radius"),parameters),scalar(feature.at("depth"),parameters)).Shape();
        BRepAlgoAPI_Cut operation;
        NCollection_List<TopoDS_Shape> left,right;
        left.Append(body); right.Append(tool);
        operation.SetArguments(left); operation.SetTools(right);
        operation.SetNonDestructive(true); operation.SetRunParallel(false); operation.Build();
        if (!operation.IsDone() || operation.HasErrors()) throw Error("kernel_failure","Hole cut failed");
        shape=operation.Shape();
        // A hole that misses, stops short of, or only touches its input leaves
        // a valid but unchanged solid. That is a failed intent, not a revision.
        GProp_GProps before,after,cutter;
        BRepGProp::VolumeProperties(body,before); BRepGProp::VolumeProperties(shape,after); BRepGProp::VolumeProperties(tool,cutter);
        const double removed=before.Mass()-after.Mass();
        // Judge the removal against the hole's own volume, not the body's: a small
        // real hole in a very large block removes less than any fixed fraction of
        // the body, while a miss or a face contact removes none of the hole.
        if (!(removed > hole_removal_tolerance*cutter.Mass()))
          throw Error("invalid_model","Hole does not enter its input solid; it removes no material",
            {{"source_feature_id",input},{"removed_volume_mm3",removed},{"hole_volume_mm3",cutter.Mass()}});
        record_history(operation,impl_->features.at(input),input,shape,history,history_truncated);
      } else if (type == "import_step"||type=="import_step_surface") {
        shape=read_step(text_field(feature,"content"));
        if(type=="import_step_surface") {
          // Unwrap only singleton containers, preserving every face and shell.
          while(shape.ShapeType()==TopAbs_COMPOUND){TopoDS_Iterator it(shape);if(!it.More())break;const auto one=it.Value();it.Next();if(it.More())break;shape=one;}
        }
        if(feature.contains("solid_indices")) {
          ShapeMap solids;TopExp::MapShapes(shape,TopAbs_SOLID,solids);
          TopoDS_Compound selected;BRep_Builder builder;builder.MakeCompound(selected);
          for(const auto& value:feature.at("solid_indices")) {
            const auto index=value.get<int>();
            if(index>solids.Extent())throw Error("selection_missing","STEP solid index is absent from the pinned source",{{"solid_index",index},{"solid_count",solids.Extent()}});
            builder.Add(selected,solids(index));
          }
          shape=selected;
        }
      } else if (type == "cut") {
        BRepAlgoAPI_Cut operation;
        NCollection_List<TopoDS_Shape> left, right;
        left.Append(shapes.at(text_field(feature, "left"))); right.Append(shapes.at(text_field(feature, "right")));
        operation.SetArguments(left); operation.SetTools(right);
        operation.SetNonDestructive(true); operation.SetRunParallel(false); operation.Build();
        if (!operation.IsDone() || operation.HasErrors()) throw Error("kernel_failure", "Boolean cut failed");
        shape = operation.Shape();
        for (const auto* key : {"left","right"}) {
          const auto input=text_field(feature,key);
          record_history(operation,impl_->features.at(input),input,shape,history,history_truncated);
        }
      } else if (type == "intersection") {
        BRepAlgoAPI_Common operation;NCollection_List<TopoDS_Shape> left,right;
        left.Append(shapes.at(text_field(feature,"left")));right.Append(shapes.at(text_field(feature,"right")));
        operation.SetArguments(left);operation.SetTools(right);operation.SetNonDestructive(true);operation.SetRunParallel(false);operation.Build();
        if(!operation.IsDone()||operation.HasErrors())throw Error("kernel_failure","Solid intersection failed");shape=operation.Shape();
        for(const auto* key:{"left","right"}){const auto input=text_field(feature,key);record_history(operation,impl_->features.at(input),input,shape,history,history_truncated);}
      } else if (type == "fuse") {
        BRepAlgoAPI_Fuse operation;
        NCollection_List<TopoDS_Shape> left, right;
        left.Append(shapes.at(text_field(feature, "left"))); right.Append(shapes.at(text_field(feature, "right")));
        operation.SetArguments(left); operation.SetTools(right);
        operation.SetNonDestructive(true); operation.SetRunParallel(false); operation.Build();
        if (!operation.IsDone() || operation.HasErrors()) throw Error("kernel_failure", "Boolean fuse failed");
        shape = operation.Shape();
        for (const auto* key : {"left","right"}) {
          const auto input=text_field(feature,key);
          record_history(operation,impl_->features.at(input),input,shape,history,history_truncated);
        }
      } else if(type=="sheet_metal"||type=="sheet_unfold") {
        const auto input=text_field(feature,"input");const Json* sheet=&feature;
        if(type=="sheet_unfold") {
          sheet=nullptr;for(const auto& candidate:model.at("features"))if(candidate.at("id")==input){sheet=&candidate;break;}
          if(!sheet||sheet->at("type")!="sheet_metal")throw Error("invalid_model","Unfold requires preserved sheet-metal bend intent");
        }
        const auto profile=text_field(*sheet,"input");const auto& source=impl_->features.at(profile);
        const auto plan=sheet_plan(*sheet,parameters,source);
        BRepBuilderAPI_Copy copy(plan.face);
        BRepPrimAPI_MakePrism base(copy.Shape(),gp_Vec(plan.normal)*-plan.thickness,true);
        if(!base.IsDone())throw Error("kernel_failure","Sheet-metal base extrusion failed");
        shape=base.Shape();check_shape(shape);
        for(const auto& flange:plan.flanges)for(const auto& region:sheet_regions(flange,plan.thickness,type=="sheet_unfold"))shape=join_sheet_region(shape,region,flange.id);
        BRepAlgoAPI_Check interference(shape,false,true);
        if(!interference.IsValid()) {
          bool aborted=!interference.Result().IsEmpty(),bezier_extrusion=false;
          for(const auto& issue:interference.Result())aborted=aborted&&issue.GetCheckStatus()==BOPAlgo_OperationAborted;
          for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
            BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));
            if(surface.GetType()==GeomAbs_SurfaceOfExtrusion&&surface.BasisCurve()->GetType()==GeomAbs_BezierCurve)bezier_extrusion=true;
          }
          if(aborted&&bezier_extrusion) {
            // OCCT's swept-Bezier intersection path can abort. This is an exact
            // polynomial basis conversion, not approximation or tolerance healing.
            // Preserve analytic planes/cylinders and require the full native check.
            BRepTools_Modifier modifier(shape,new SheetBezierExtrusions);
            if(!modifier.IsDone())throw Error("invalid_shape","Swept-Bezier basis conversion did not complete");
            auto normalized=modifier.ModifiedShape(shape);
            check_shape(normalized);
            GProp_GProps before_volume,after_volume;
            BRepGProp::VolumeProperties(shape,before_volume,1e-9);BRepGProp::VolumeProperties(normalized,after_volume,1e-9);
            const auto before_bounds=bounds(shape),after_bounds=bounds(normalized);
            bool equivalent=count(shape,TopAbs_FACE)==count(normalized,TopAbs_FACE)&&count(shape,TopAbs_EDGE)==count(normalized,TopAbs_EDGE);
            for(const auto* key:{"min","max"})for(std::size_t axis=0;axis<3;++axis)equivalent=equivalent&&std::abs(before_bounds.at(key)[axis].get<double>()-after_bounds.at(key)[axis].get<double>())<1e-6;
            equivalent=equivalent&&std::abs(before_volume.Mass()-after_volume.Mass())<1e-7*std::max(1.0,before_volume.Mass());
            if(!equivalent)throw Error("invalid_shape","Exact swept-Bezier normalization failed geometric equivalence checks",{{"original_bounds",before_bounds},{"normalized_bounds",after_bounds},{"original_volume",before_volume.Mass()},{"normalized_volume",after_volume.Mass()},{"original_faces",count(shape,TopAbs_FACE)},{"normalized_faces",count(normalized,TopAbs_FACE)},{"original_edges",count(shape,TopAbs_EDGE)},{"normalized_edges",count(normalized,TopAbs_EDGE)}});
            BRepAlgoAPI_Check normalized_check(normalized,false,true);
            if(normalized_check.IsValid()){shape=normalized;interference.SetData(shape,false,true);interference.Perform();}
          }
        }
        if(!interference.IsValid()) {
          Json faults=Json::array();const std::vector<std::string> names={"unknown","bad_type","self_intersect","too_small_edge","nonrecoverable_face","incompatible_vertex","incompatible_edge","incompatible_face","operation_aborted","C0_geometry","invalid_curve_on_surface","not_valid"};
          for(const auto& issue:interference.Result()) {
            if(faults.size()>=16)break;const auto status=static_cast<std::size_t>(issue.GetCheckStatus());Json entities=Json::array();
            for(const auto& entity:issue.GetFaultyShapes1()) {
              if(entities.size()>=16)break;Json detail={{"bounds_mm",bounds(entity)}};
              if(entity.ShapeType()==TopAbs_FACE){detail["kind"]="face";detail["surface_kind"]=surface_kind(BRepAdaptor_Surface(TopoDS::Face(entity)).GetType());}
              else if(entity.ShapeType()==TopAbs_EDGE){detail["kind"]="edge";detail["curve_kind"]=curve_kind(BRepAdaptor_Curve(TopoDS::Edge(entity)).GetType());}
              else detail["kind"]="other";entities.push_back(detail);
            }
            faults.push_back({{"status",status<names.size()?names[status]:"unknown"},{"entities",entities}});
          }
          throw Error("invalid_shape","Sheet-metal geometry failed the native self-interference check",{{"native_faults",faults}});
        }
        const auto report=sheet_report(plan,type=="sheet_unfold"?input:id,type=="sheet_unfold");
        const auto expected=report.at(type=="sheet_unfold"?"flat_volume_mm3":"formed_volume_mm3").get<double>();
        GProp_GProps volume;BRepGProp::VolumeProperties(shape,volume,1e-9);
        if(std::abs(volume.Mass()-expected)>1e-7*std::max(1.0,expected))throw Error("invalid_shape","Sheet-metal geometry did not preserve its exact region and bend volumes",{{"expected_volume_mm3",expected},{"actual_volume_mm3",volume.Mass()}});
        if(type=="sheet_metal"&&plan.face.IsSame(source.shape))record_history(base,source,profile,shape,history,history_truncated,&copy);
      } else if(type=="shell"||type=="offset"||type=="thicken") {
        const auto input=text_field(feature,"input");const auto& source=impl_->features.at(input);
        BRepBuilderAPI_Copy copy(source.shape);
        const auto distance=scalar(feature.at(type=="offset"?"distance":"thickness"),parameters);
        const auto join=feature.value("join",std::string("arc"))=="arc"?GeomAbs_Arc:GeomAbs_Intersection;
        constexpr double offset_tolerance=1e-7;
        if(type=="shell") {
          ShapeMap solids;TopExp::MapShapes(copy.Shape(),TopAbs_SOLID,solids);
          if(solids.Extent()!=1)throw Error("invalid_model","Shell requires exactly one source solid; shell individual parts before combining",{{"source_feature_id",input},{"solid_count",solids.Extent()}});
          const auto selected=select_faces(source,feature.at("faces"),parameters,input);
          if(selected.size()==static_cast<std::size_t>(source.faces.Extent()))throw Error("invalid_model","Shell must retain at least one source face",{{"source_feature_id",input}});
          NCollection_List<TopoDS_Shape> closing;
          for(const auto i:selected)closing.Append(copy.ModifiedShape(source.faces(i)));
          if(selected.empty()) {
            // A sealed hollow body keeps both exact boundaries. Difference
            // establishes cavity orientation instead of a healing pass.
            BRepOffsetAPI_MakeOffsetShape parallel;
            parallel.PerformByJoin(solids(1),distance,offset_tolerance,BRepOffset_Skin,false,false,join,false);
            if(!parallel.IsDone())throw Error("kernel_failure","Closed shell offset failed; change thickness or input geometry",{{"source_feature_id",input}});
            const auto result=parallel.Shape();check_shape(result);
            GProp_GProps before,after;BRepGProp::VolumeProperties(solids(1),before);BRepGProp::VolumeProperties(result,after);
            if(count(result,TopAbs_SOLID)!=1||(after.Mass()-before.Mass())*distance<=0)throw Error("invalid_shape","Closed shell offset collapsed or reversed the requested thickness",{{"source_feature_id",input}});
            BRepAlgoAPI_Cut difference;NCollection_List<TopoDS_Shape> outer,inner;
            outer.Append(distance>0?result:solids(1));inner.Append(distance>0?solids(1):result);
            difference.SetArguments(outer);difference.SetTools(inner);difference.SetNonDestructive(true);difference.SetRunParallel(false);difference.Build();
            if(!difference.IsDone()||difference.HasErrors())throw Error("kernel_failure","Closed shell difference failed",{{"source_feature_id",input}});
            shape=difference.Shape();check_shape(shape);
            record_history(parallel,source,input,shape,history,history_truncated,&copy);
            record_history(difference,source,input,shape,history,history_truncated,&copy);
          } else {
            BRepOffsetAPI_MakeThickSolid operation;
            operation.MakeThickSolidByJoin(solids(1),closing,distance,offset_tolerance,BRepOffset_Skin,false,false,join,false);
            if(!operation.IsDone())throw Error("kernel_failure","Shell failed; change thickness, joins or selected opening faces",{{"source_feature_id",input}});
            shape=operation.Shape();check_shape(shape);
            record_history(operation,source,input,shape,history,history_truncated,&copy);
          }
          if(count(shape,TopAbs_SOLID)!=1)throw Error("invalid_shape","Shell must preserve one connected solid body",{{"source_feature_id",input}});
          if(distance<0) {
            GProp_GProps before,after;BRepGProp::VolumeProperties(copy.Shape(),before);BRepGProp::VolumeProperties(shape,after);
            if(after.Mass()>=before.Mass())throw Error("invalid_shape","Inward shell must remove material from its source",{{"source_feature_id",input}});
          }
          check_parallel_material(copy.Shape(),shape,distance,true,input);
        } else if(type=="offset") {
          ShapeMap solids;TopExp::MapShapes(copy.Shape(),TopAbs_SOLID,solids);
          TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);
          std::vector<std::unique_ptr<BRepOffsetAPI_MakeOffsetShape>> operations;
          for(int i=1;i<=solids.Extent();++i) {
            auto operation=std::make_unique<BRepOffsetAPI_MakeOffsetShape>();
            operation->PerformByJoin(solids(i),distance,offset_tolerance,BRepOffset_Skin,false,false,join,false);
            if(!operation->IsDone())throw Error("kernel_failure","Solid offset failed; change distance or input geometry",{{"source_feature_id",input},{"solid_index",i}});
            const auto result=operation->Shape();check_shape(result);
            if(count(result,TopAbs_SOLID)!=1)throw Error("invalid_shape","Offset must preserve each source solid; collapsed or split results are rejected",{{"source_feature_id",input},{"solid_index",i}});
            GProp_GProps before,after;BRepGProp::VolumeProperties(solids(i),before);BRepGProp::VolumeProperties(result,after);
            if((after.Mass()-before.Mass())*distance<=0)throw Error("invalid_shape","Solid offset must expand for positive distance and shrink for negative distance",{{"source_feature_id",input},{"solid_index",i},{"source_volume_mm3",before.Mass()},{"result_volume_mm3",after.Mass()}});
            check_parallel_material(solids(i),result,distance,false,input);
            builder.Add(compound,result);operations.push_back(std::move(operation));
          }
          shape=solids.Extent()==1?operations.front()->Shape():TopoDS_Shape(compound);
          const FeatureGeometry target(shape);
          // Result IDs come from the final compound, not from per-solid local
          // enumerations. Do not label source entities outside this operation
          // deleted: restrict each history query to its actual source solid.
          for(int i=1;i<=solids.Extent();++i) {
            const auto& scope=solids(i);
            record_history(*operations[i-1],source,input,target,history,history_truncated,&copy,-1,&scope);
          }
        } else {
          std::vector<TopoDS_Face> faces;
          if(feature.contains("faces"))for(const auto i:select_faces(source,feature.at("faces"),parameters,input))faces.push_back(TopoDS::Face(copy.ModifiedShape(source.faces(i))));
          else for(int i=1;i<=source.faces.Extent();++i)faces.push_back(TopoDS::Face(copy.ModifiedShape(source.faces(i))));
          std::vector<TopoDS_Shape> patches;
          const auto source_type=[&]() {for(const auto& declaration:model.at("features"))if(declaration.at("id")==input)return text_field(declaration,"type");return std::string{};}();
          if(feature.contains("faces")||is_surface_feature_type(source_type))patches.push_back(open_connected_patch(faces,input));
          else for(const auto& face:faces)patches.push_back(open_connected_patch({face},input));
          TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);
          std::vector<std::unique_ptr<BRepOffset_MakeOffset>> operations;
          std::vector<std::unique_ptr<BRepTools_Modifier>> normalizations;
          for(const auto& patch:patches) {
            auto operation=std::make_unique<BRepOffset_MakeOffset>();
            auto normalization=std::make_unique<BRepTools_Modifier>(patch,new BoundedPolynomialSupports);
            if(!normalization->IsDone())throw Error("kernel_failure","Exact polynomial support restriction failed");
            const auto normalized=normalization->ModifiedShape(patch);check_surface_shape(normalized);
            operation->Initialize(normalized,distance,offset_tolerance,BRepOffset_Skin,false,false,join,true,false);
            operation->MakeOffsetShape();
            if(!operation->IsDone())throw Error("kernel_failure","Surface thickening failed; change thickness, joins or selected patch",{{"source_feature_id",input},{"offset_status",static_cast<int>(operation->Error())}});
            const auto result=operation->Shape();check_shape(result);
            if(count(result,TopAbs_SOLID)!=1)throw Error("invalid_shape","Thicken must produce one closed solid per connected patch",{{"source_feature_id",input}});
            if(feature.contains("faces")&&!is_surface_feature_type(source_type))check_parallel_material(copy.Shape(),result,distance,true,input);
            builder.Add(compound,result);operations.push_back(std::move(operation));normalizations.push_back(std::move(normalization));
          }
          shape=patches.size()==1?operations.front()->Shape():TopoDS_Shape(compound);
          const FeatureGeometry target(shape);
          for(std::size_t i=0;i<patches.size();++i){OffsetSupportHistory evidence{*operations[i],*normalizations[i]};record_history(evidence,source,input,target,history,history_truncated,&copy,-1,&patches[i]);}
        }
        BRepAlgoAPI_Check interference(shape,false,true);
        if(!interference.IsValid())throw Error("invalid_shape","Shell, offset or thicken produced self-interfering geometry",{{"source_feature_id",input}});
      } else if (type == "fillet" || type == "chamfer") {
        const auto input=text_field(feature,"input");
        const auto& source=impl_->features.at(input);
        BRepBuilderAPI_Copy copy(source.shape);
        // Select and describe edges in the input feature's own evaluation, the
        // IDs topology(input) reports; the copy is reached only via its history.
        const auto& edges=source.edges;
        std::vector<int> selected;
        const auto& selector = feature.at("edges");
        Json candidates = Json::array();
        for (int i = 1; i <= edges.Extent(); ++i) {
          if (selector.is_string()) selected.push_back(i);
          else {
            const auto descriptor = edge_descriptor(TopoDS::Edge(edges(i)), i);
            if (candidates.size() < 64) candidates.push_back(descriptor);
            if (matches(descriptor, selector, parameters)) selected.push_back(i);
          }
        }
        if (selector.is_object() && selected.size() != selector.at("expected_count").get<std::size_t>()) {
          Json matched = Json::array();
          for (auto i : selected) if (matched.size() < 64) matched.push_back(edge_descriptor(TopoDS::Edge(edges(i)), i));
          const auto expected = selector.at("expected_count").get<std::size_t>();
          const auto code = selected.empty() ? "selection_missing" : selected.size() > expected ? "selection_ambiguous" : "selection_count_mismatch";
          throw Error(code, "Geometric selector did not match its expected edge count", {{"source_feature_id", feature.at("input")}, {"expected_count", expected}, {"actual_count", selected.size()}, {"matches", matched}, {"candidates", candidates}, {"candidates_truncated", edges.Extent() > 64}});
        }
        const auto finish=[&](auto& operation,const char* dimension) {
          for (const auto i:selected) operation.Add(scalar(feature.at(dimension),parameters),TopoDS::Edge(copy.ModifiedShape(edges(i))));
          operation.Build();
          if (!operation.IsDone()) throw Error("kernel_failure",type+" failed; change its dimension or the input geometry");
          shape=operation.Shape();record_history(operation,source,input,shape,history,history_truncated,&copy);
        };
        if (type=="fillet") {BRepFilletAPI_MakeFillet operation(copy.Shape());finish(operation,"radius");}
        else {
          BRepFilletAPI_MakeChamfer operation(copy.Shape());
          if(!feature.contains("reference_face"))finish(operation,"distance");
          else {
            const auto face_index=select_faces(source,feature.at("reference_face"),parameters,input).front();
            const auto reference=TopoDS::Face(source.faces(face_index));const auto mapped=TopoDS::Face(copy.ModifiedShape(reference));
            ShapeMap adjacent;TopExp::MapShapes(reference,TopAbs_EDGE,adjacent);
            for(const auto i:selected) {
              if(!adjacent.Contains(edges(i)))throw Error("invalid_model","Every asymmetric chamfer edge must touch its reference_face",{{"source_feature_id",input}});
              const auto edge=TopoDS::Edge(copy.ModifiedShape(edges(i)));const double first=scalar(feature.at("distance"),parameters);
              if(feature.contains("distance2"))operation.Add(first,scalar(feature.at("distance2"),parameters),edge,mapped);
              else operation.AddDA(first,scalar(feature.at("angle_deg"),parameters,"deg")*std::numbers::pi/180,edge,mapped);
            }
            operation.Build();if(!operation.IsDone())throw Error("kernel_failure","Asymmetric chamfer failed; change dimensions or geometry");
            shape=operation.Shape();record_history(operation,source,input,shape,history,history_truncated,&copy);
          }
        }
      }
      if(is_curve_feature_type(type))check_curve_shape(shape);
      else if(is_surface_feature_type(type))check_surface_intent(feature,shape);
      else if (!is_sketch_feature_type(type)) check_shape(shape);
      else {
        check_sketch_shape(shape,planes.at(id));
        // A one-region boolean compound is a face just like an authored sketch;
        // this preserves existing loft consumers without a container cast.
        if(count(shape,TopAbs_FACE)==1&&shape.ShapeType()!=TopAbs_FACE) {TopExp_Explorer face(shape,TopAbs_FACE);shape=face.Current();}
      }
      shapes.emplace(id, shape);
      impl_->features.emplace(id,assembly ? std::move(*assembly) : FeatureGeometry(shape));
      impl_->features.at(id).provenance=feature_provenance(feature,history,history_truncated);
      if(cache.stage&&cache.keys.contains(id)) {
        try {cache.stage(cache.keys.at(id).get<std::string>(),{{"feature_id",id},{"snapshot",snapshot_feature(impl_->features.at(id))}});}
        catch(const std::exception&) { /* Serialization is optional; the valid design still succeeds. */ }
      }
    } catch (const Error& e) {
      auto details = e.details; details["feature_id"] = id;
      throw Error(e.code, e.what(), details);
    } catch (const Standard_Failure& e) {
      throw occt_error(e, {{"feature_id", id}});
    }
  }
  impl_->output = text_field(model, "output");
  impl_->shape = shapes.at(impl_->output);
}
BuiltModel::BuiltModel(const Json& model,const Json& snapshot) : impl_(std::make_unique<Impl>()) {
  validate_model(model);impl_->model=model;
  try {
    if(snapshot.at("format")!=2||snapshot.at("features").size()!=model.at("features").size())
      throw Error("cache_miss","Cached feature format/count mismatch");
    std::map<std::string,gp_Ax2> planes;
    for(const auto& feature:model.at("features")) {
      const auto id=text_field(feature,"id");
      auto restored=restore_feature(feature,model.at("parameters"),impl_->features,snapshot.at("features").at(id));
      if(is_sketch_feature_type(text_field(feature,"type"))) {
        const auto plane=sketch_plane(feature,model.at("parameters"),planes,restored.shape);check_sketch_shape(restored.shape,plane);planes.emplace(id,plane);
      }
      impl_->features.emplace(id,std::move(restored));
    }
    impl_->output=text_field(model,"output");impl_->shape=impl_->features.at(impl_->output).shape;
  }catch(const Standard_Failure& error){throw occt_error(error,Json::object(),"cache_miss");}
}
Json BuiltModel::snapshot() const {
  try {
    Json features=Json::object();std::size_t bytes=0;
    for(const auto& [id,geometry]:impl_->features) {
      auto entry=snapshot_feature(geometry,32*1024*1024-bytes);
      bytes+=entry.at("brep").get_ref<const std::string&>().size();features[id]=std::move(entry);
    }
    return {{"format",2},{"features",std::move(features)}};
  }catch(const Standard_Failure& error){throw occt_error(error,Json::object(),"cache_miss");}
}
BuiltModel::~BuiltModel() = default;
BuiltModel::BuiltModel(BuiltModel&&) noexcept = default;
BuiltModel& BuiltModel::operator=(BuiltModel&&) noexcept = default;

Json BuiltModel::summary(const std::string& feature_id) const {
  try {
    const auto& geometry = impl_->feature(feature_id);
    const auto& shape = geometry.shape;
    const auto solids = count(shape, TopAbs_SOLID);
    GProp_GProps volume, area, linear;
    // Native pipe surfaces may be rational B-splines even for analytic input.
    // Fixed-order quadrature can misreport their mass; use adaptive integration.
    if (solids) {
      bool polynomial_surface=false;
      for(TopExp_Explorer face(shape,TopAbs_FACE);face.More();face.Next()) {
        const auto kind=BRepAdaptor_Surface(TopoDS::Face(face.Current())).GetType();
        polynomial_surface|=kind==GeomAbs_BSplineSurface||kind==GeomAbs_BezierSurface;
      }
      // Gauss-only integration can report a tiny error estimate while missing
      // the analytic volume of a rational affine-transformed cylinder by 1e-7
      // relative. Gauss-Kronrod resolves the surface/trim quadrature separately.
      if(polynomial_surface) {
        const double error=BRepGProp::VolumePropertiesGK(shape,volume,1e-9,false,true,true,false);
        if(error<0||!std::isfinite(volume.Mass()))throw Error("kernel_failure","Exact surface volume integration failed");
      } else BRepGProp::VolumeProperties(shape,volume,1e-9);
    }
    BRepGProp::SurfaceProperties(shape, area,1e-9);
    Bnd_Box box;
    BRepBndLib::AddOptimal(shape, box, false, false);
    const auto limits = box.Get();
    if(geometry.faces.IsEmpty())BRepGProp::LinearProperties(shape,linear);
    const auto center = solids ? volume.CentreOfMass() : geometry.faces.IsEmpty()?linear.CentreOfMass():area.CentreOfMass();
    Json result={{"valid", true}, {"units", "mm"}, {"volume_mm3", solids ? volume.Mass() : 0.0}, {"area_mm2", area.Mass()},
      {"center_of_mass_mm", {center.X(), center.Y(), center.Z()}},
      {"bounds_mm", {{"min", {limits.Xmin, limits.Ymin, limits.Zmin}}, {"max", {limits.Xmax, limits.Ymax, limits.Zmax}}}},
      {"solid_count", solids}, {"face_count", count(shape, TopAbs_FACE)},
      {"edge_count", count(shape, TopAbs_EDGE)}};
    if(impl_->model.contains("components"))result["components"]=component_status(impl_->model);
    const auto selected=feature_id.empty()?impl_->output:feature_id;
    const Json* declared=nullptr;for(const auto& feature:impl_->model.at("features"))if(feature.at("id")==selected){declared=&feature;break;}
    if(declared&&(declared->at("type")=="sheet_metal"||declared->at("type")=="sheet_unfold")) {
      const bool flat=declared->at("type")=="sheet_unfold";const Json* sheet=declared;
      const auto source_id=flat?text_field(*declared,"input"):selected;
      if(flat)for(const auto& feature:impl_->model.at("features"))if(feature.at("id")==source_id){sheet=&feature;break;}
      result["sheet_metal"]=sheet_report(sheet_plan(*sheet,impl_->model.at("parameters"),impl_->features.at(text_field(*sheet,"input"))),source_id,flat);
    }
    if (!geometry.parts.empty()) {
      result["assembly"]={{"parts",Json::array()},{"mates",geometry.mates},{"tree",geometry.tree}};
      result["assembly"]["mechanisms"]=assembly_mechanisms(impl_->model,feature_id.empty()?impl_->output:feature_id);
      if (!geometry.motion.is_null()) result["assembly"]["motion"]=geometry.motion;
      for (const auto& part:geometry.parts) {
        GProp_GProps mass; BRepGProp::VolumeProperties(part.shape,mass);
        result["assembly"]["parts"].push_back({{"id",part.id},{"input",part.input},{"transform",matrix(part.transform)},
          {"bounds_mm",bounds(part.shape)},{"volume_mm3",mass.Mass()}});
      }
    }
    return result;
  } catch (const Standard_Failure& e) { throw occt_error(e, {{"feature_id", feature_id.empty() ? impl_->output : feature_id}}); }
}

Json BuiltModel::measure(const Json& query,const Json& evaluated_topology,const std::string& feature_id) const {
  validate_measurement_query(query);
  if(query.at("action")=="section")return section(query,feature_id);
  const auto selected=feature_id.empty()?impl_->output:feature_id;
  try {
    const auto& geometry=impl_->feature(selected);
    std::optional<Json> current_topology;
    const auto resolve=[&](const Json& target)->TopoDS_Shape{
      const auto kind=text_field(target,"kind");
      if(kind=="part"){
        const auto id=text_field(target,"part_id");for(const auto& part:geometry.parts)if(part.id==id)return part.shape;
        throw Error("selection_missing","Measurement leaf occurrence is absent",{{"part_id",id}});
      }
      const auto entity=text_field(target,"entity_id");const auto* list=kind=="face"?"faces":"edges";
      if(evaluated_topology.value("feature_id",std::string{})!=selected||!evaluated_topology.contains(list))throw Error("stale_selection","Measurement topology does not match its evaluated feature");
      const Json* original=nullptr;
      for(const auto& item:evaluated_topology.at(list))if(item.at("id")==entity){if(original)throw Error("selection_ambiguous","Evaluation repeats a measurement entity");original=&item;}
      if(!original)throw Error("selection_missing","Measurement entity is absent from the evaluation",{{"entity_id",entity}});
      if(kind=="edge"&&original->value("degenerate",false))throw Error("selection_missing","Degenerate edges cannot be measured");
      if(!current_topology)current_topology=topology(selected);
      int match=0,matches=0,index=0;
      for(const auto& item:current_topology->at(list)){++index;if(measurement_descriptor_matches(*original,item)){match=index;++matches;}}
      if(matches!=1)throw Error(matches?"selection_ambiguous":"stale_selection","Cannot uniquely recover the evaluated measurement geometry",{{"entity_id",entity},{"matches",matches}});
      return kind=="face"?geometry.faces(match):geometry.edges(match);
    };
    Json requested=Json::array(),part_ids=Json::array();std::string coverage="explicit_pair";
    if(query.at("action")=="pair")requested.push_back(query.at("targets"));
    else {
      if(geometry.parts.size()<2)throw Error("invalid_argument","Assembly clearance needs at least two leaf occurrences");
      if(query.contains("part_ids")){part_ids=query.at("part_ids");coverage="explicit_leaf_subset";}
      else {if(geometry.parts.size()>measurement_part_limit)throw Error("limit_exceeded","All-pairs clearance exceeds 23 leaves; choose an explicit subset",{{"leaves",geometry.parts.size()},{"leaf_limit",measurement_part_limit},{"pair_limit",measurement_pair_limit}});
        for(const auto& part:geometry.parts)part_ids.push_back(part.id);coverage="all_assembly_leaves";}
      for(std::size_t a=0;a<part_ids.size();++a)for(std::size_t b=a+1;b<part_ids.size();++b)
        requested.push_back(Json::array({{{"kind","part"},{"part_id",part_ids[a]}},{{"kind","part"},{"part_id",part_ids[b]}}}));
    }
    Json pairs=Json::array();double minimum=std::numeric_limits<double>::max();std::size_t interference=0;bool too_close=false;
    std::map<std::string,TopoDS_Shape> shapes;
    const auto cached_shape=[&](const Json& target)->const TopoDS_Shape&{const auto key=target.dump();auto found=shapes.find(key);if(found==shapes.end())found=shapes.emplace(key,resolve(target)).first;return found->second;};
    for(const auto& targets:requested){
      const auto& a=cached_shape(targets[0]);const auto& b=cached_shape(targets[1]);
      BRepExtrema_DistShapeShape distance;distance.SetMultiThread(false);distance.LoadS1(a);distance.LoadS2(b);distance.SetDeflection(measurement_distance_tolerance);distance.Perform();
      if(!distance.IsDone()||distance.NbSolution()<1||!std::isfinite(distance.Value())||distance.Value()<0)throw Error("kernel_failure","Exact measurement distance did not produce finite witnesses");
      const auto gap=distance.Value();minimum=std::min(minimum,gap);Json witnesses=Json::array();int rejected=0;
      const auto supported=[&](const gp_Pnt& p,const TopoDS_Shape& shape){
        BRepExtrema_DistShapeShape check;check.SetMultiThread(false);check.LoadS1(BRepBuilderAPI_MakeVertex(p).Vertex());check.LoadS2(shape);
        check.SetDeflection(measurement_distance_tolerance);check.Perform();
        return check.IsDone()&&std::isfinite(check.Value())&&check.Value()<=measurement_distance_tolerance;
      };
      for(int i=1;i<=std::min<int>(distance.NbSolution(),measurement_witness_limit);++i){
        const auto& pa=distance.PointOnShape1(i);const auto& pb=distance.PointOnShape2(i);
        bool finite=true;for(const auto* p:{&pa,&pb})finite=finite&&std::isfinite(p->X())&&std::isfinite(p->Y())&&std::isfinite(p->Z());
        if(!finite||std::abs(pa.Distance(pb)-gap)>measurement_distance_tolerance||!supported(pa,a)||!supported(pb,b)){++rejected;continue;}
        witnesses.push_back({{"a_mm",point(pa)},{"b_mm",point(pb)}});
      }
      if(witnesses.empty())throw Error("kernel_failure","Exact measurement has no independently qualified closest-point witness");
      Json item={{"targets",targets},{"distance_mm",gap},{"witnesses",witnesses},{"solution_count",distance.NbSolution()},
        {"rejected_witness_count",rejected},{"witnesses_truncated",distance.NbSolution()>static_cast<int>(witnesses.size())},{"intersection_volume_mm3",nullptr},{"interference",nullptr}};
      if(targets[0].at("kind")=="part"&&targets[1].at("kind")=="part"){
        BRepAlgoAPI_Common common;NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(a);tools.Append(b);
        common.SetArguments(arguments);common.SetTools(tools);common.SetNonDestructive(true);common.SetRunParallel(false);common.Build();
        if(!common.IsDone()||common.HasErrors())throw Error("kernel_failure","Exact measurement intersection failed");
        double volume=0;
        if(!common.Shape().IsNull())for(TopExp_Explorer it(common.Shape(),TopAbs_SOLID);it.More();it.Next()){
          GProp_GProps properties;BRepGProp::VolumeProperties(it.Current(),properties,true);volume+=std::max(0.0,properties.Mass());}
        if(!std::isfinite(volume))throw Error("kernel_failure","Exact intersection volume is non-finite");
        item["intersection_volume_mm3"]=volume;item["interference"]=volume>measurement_volume_tolerance;
        if(item.at("interference")==true)++interference;
      }
      const auto analytic_axis=[](const TopoDS_Shape& shape)->std::optional<std::pair<gp_Dir,bool>>{
        if(shape.ShapeType()==TopAbs_FACE){BRepAdaptor_Surface surface(TopoDS::Face(shape));if(surface.GetType()==GeomAbs_Plane)return std::pair{surface.Plane().Axis().Direction(),true};}
        if(shape.ShapeType()==TopAbs_EDGE){BRepAdaptor_Curve curve(TopoDS::Edge(shape));if(curve.GetType()==GeomAbs_Line)return std::pair{curve.Line().Direction(),false};}
        return std::nullopt;
      };
      const auto axis_a=analytic_axis(a),axis_b=analytic_axis(b);
      if(axis_a&&axis_b){const auto cosine=std::clamp(std::abs(axis_a->first.Dot(axis_b->first)),0.0,1.0);const bool mixed=axis_a->second!=axis_b->second;
        item["angle_deg"]=(mixed?std::asin(cosine):std::acos(cosine))*180/std::numbers::pi;
        item["angle_method"]=mixed?"line_to_plane":axis_a->second?"unoriented_plane_normals":"unoriented_line_directions";}
      if(query.contains("minimum_clearance_mm")&&gap+measurement_distance_tolerance<query.at("minimum_clearance_mm").get<double>())too_close=true;
      pairs.push_back(std::move(item));
    }
    Json report={{"schema_version",1},{"units","mm"},{"action",query.at("action")},{"method","exact_BRep_minimum_distance"},{"coordinate_space","committed_source_pose"},
      {"coverage",coverage},{"part_ids",part_ids},{"pairs",pairs},{"minimum_distance_mm",minimum},{"interference_count",interference},
      {"status",interference||too_close?"fail":query.contains("minimum_clearance_mm")?"pass":"measured"},
      {"distance_tolerance_mm",measurement_distance_tolerance},{"intersection_volume_tolerance_mm3",measurement_volume_tolerance}};
    if(query.contains("minimum_clearance_mm"))report["minimum_clearance_mm"]=query.at("minimum_clearance_mm");
    return report;
  }catch(const Error& error){auto details=error.details;details["feature_id"]=selected;throw Error(error.code,error.what(),details);}
  catch(const Standard_Failure& error){throw occt_error(error,{{"feature_id",selected}});}
}

Json BuiltModel::topology(const std::string& feature_id, const QueryLimits& limits) const {
  try {
    const auto& geometry = impl_->feature(feature_id);
    topology_limit(geometry, limits);
    const auto id = feature_id.empty() ? impl_->output : feature_id;
    Json result = {{"schema_version", 1}, {"units", "mm"}, {"feature_id", id}, {"selection_lifetime", "evaluation"},
      {"faces", Json::array()}, {"edges", Json::array()}, {"provenance",geometry.provenance}};
    for (int i = 1; i <= geometry.faces.Extent(); ++i) {
      const auto face = TopoDS::Face(geometry.faces(i));
      auto item=face_descriptor(face,i);
      if (!geometry.parts.empty()) item["part_id"]=geometry.face_parts[i];
      result["faces"].push_back(item);
    }
    for (int i = 1; i <= geometry.edges.Extent(); ++i) {
      auto edge=edge_descriptor(TopoDS::Edge(geometry.edges(i)),i);
      if (!geometry.parts.empty()) edge["part_id"]=geometry.edge_parts[i];
      result["edges"].push_back(std::move(edge));
    }
    // A pick suggests a geometric rule only when that rule is unique here. The
    // caller must keep the rule, never the enumeration ID, for future rebuilds.
    for(auto& face:result["faces"]) {
      if(!geometry.parts.empty()||face.at("area_mm2").get<double>()>1e6)continue;
      const auto center=face.at("center_mm").get<std::array<double,3>>();
      if(std::any_of(center.begin(),center.end(),[](double n){return std::abs(n)>1e6;}))continue;
      Json selector={{"type","geometric"},{"feature_id",id},{"surface_kind",face.at("surface_kind")},{"expected_count",1},
        {"center",{{"point",face.at("center_mm")},{"tolerance",1e-5}}},{"area",{{"value",face.at("area_mm2")},{"tolerance",1e-5}}}};
      if(face.contains("normal"))selector["normal"]={{"vector",face.at("normal")},{"tolerance",1e-6}};
      int matching=0;for(const auto& candidate:result.at("faces"))if(matches_face(candidate,selector,Json::object()))++matching;
      if(matching==1)face["selector"]=selector;
    }
    for (auto& edge : result["edges"]) {
      // Assembly picks identify editable source parts. A geometric rule on the
      // placed aggregate is not an edit rule for those source coordinates.
      if (!geometry.parts.empty()) continue;
      if (edge.at("degenerate") == true) continue;
      if (edge.at("length_mm").get<double>() > 1e6) continue;
      const auto center = edge.at("center_mm").get<std::array<double,3>>();
      if (std::any_of(center.begin(),center.end(),[](double n){return std::abs(n)>1e6;})) continue;
      Json selector = {{"type", "geometric"}, {"feature_id", id}, {"curve_kind", edge.at("curve_kind")}, {"expected_count", 1},
        {"center", {{"point", edge.at("center_mm")}, {"tolerance", 1e-5}}},
        {"length", {{"value", edge.at("length_mm")}, {"tolerance", 1e-5}}}};
      if (edge.contains("direction")) selector["direction"] = {{"vector", edge.at("direction")}, {"tolerance", 1e-6}};
      int matching = 0;
      for (const auto& candidate : result.at("edges")) if (matches(candidate, selector, Json::object())) ++matching;
      if (matching == 1) edge["selector"] = selector;
    }
    return result;
  } catch (const Standard_Failure& e) { throw occt_error(e, {{"feature_id", feature_id.empty() ? impl_->output : feature_id}}); }
}

Json BuiltModel::mesh(const std::string& feature_id, const QueryLimits& limits) const {
  try {
    const auto& geometry = impl_->feature(feature_id);
    topology_limit(geometry, limits);
    const auto vertex_limit = std::min<std::size_t>(limits.mesh_vertices, 200000);
    const auto triangle_limit = std::min<std::size_t>(limits.mesh_triangles, 200000);
    const auto point_limit = std::min<std::size_t>(limits.edge_points, 200000);
    // Meshing only attaches triangulations to these exact retained faces. It does
    // not enumerate a copied shape and assume the copy kept its ordering.
    if(!geometry.faces.IsEmpty()){BRepMesh_IncrementalMesh tessellation(geometry.shape, 0.1, false, 0.5, false);
    if (!tessellation.IsDone()) throw Error("kernel_failure", "Tessellation failed");}
    Json result = {{"schema_version", 1}, {"units", "mm"}, {"feature_id", feature_id.empty() ? impl_->output : feature_id},
      {"selection_lifetime", "evaluation"}, {"linear_deflection_mm", 0.1}, {"angular_deflection_rad", 0.5},
      {"positions", Json::array()}, {"triangles", Json::array()}, {"triangle_faces", Json::array()}, {"edges", Json::array()}};
    for (int i = 1; i <= geometry.faces.Extent(); ++i) {
      const Json context={{"feature_id",feature_id.empty()?impl_->output:feature_id},{"face_id","face-"+std::to_string(i)},
        {"part_id",geometry.parts.empty()?Json(nullptr):Json(geometry.face_parts[i])}};
      for(const auto& face:tessellated_faces(TopoDS::Face(geometry.faces(i)),context)) {
      TopLoc_Location location;
      const auto triangulation = BRep_Tool::Triangulation(face, location);
      const auto offset = result.at("positions").size();
      if (offset + triangulation->NbNodes() > vertex_limit || result.at("triangles").size() + triangulation->NbTriangles() > triangle_limit)
        throw Error("limit_exceeded", "Mesh exceeds its vertex or triangle limit", {{"vertex_limit", vertex_limit}, {"triangle_limit", triangle_limit}});
      for (int node = 1; node <= triangulation->NbNodes(); ++node) result["positions"].push_back(point(triangulation->Node(node).Transformed(location.Transformation())));
      for (int triangle = 1; triangle <= triangulation->NbTriangles(); ++triangle) {
        int a, b, c;
        triangulation->Triangle(triangle).Get(a,b,c);
        if (face.Orientation() == TopAbs_REVERSED) std::swap(b,c);
        result["triangles"].push_back({offset+a-1, offset+b-1, offset+c-1});
        result["triangle_faces"].push_back("face-" + std::to_string(i));
      }
    }
    }
    std::size_t total_points = 0;
    for (int i = 1; i <= geometry.edges.Extent(); ++i) {
      const auto edge = TopoDS::Edge(geometry.edges(i));
      Json points = Json::array();
      if (!BRep_Tool::Degenerated(edge)) {
        BRepAdaptor_Curve curve(edge);
        GCPnts_QuasiUniformDeflection sampling(curve, 0.1);
        if (!sampling.IsDone()) throw Error("kernel_failure", "Edge tessellation failed");
        if (total_points + sampling.NbPoints() > point_limit)
          throw Error("limit_exceeded", "Mesh exceeds its edge point limit", {{"limit", point_limit}});
        total_points += sampling.NbPoints();
        for (int node = 1; node <= sampling.NbPoints(); ++node) points.push_back(point(sampling.Value(node)));
      }
      Json item={{"id", "edge-" + std::to_string(i)}, {"points", points}};
      if (!geometry.parts.empty()) item["part_id"]=geometry.edge_parts[i];
      result["edges"].push_back(std::move(item));
    }
    return result;
  } catch (const Standard_Failure& e) { throw occt_error(e, {{"feature_id", feature_id.empty() ? impl_->output : feature_id}}); }
}


Json BuiltModel::print_meshes(const std::string& feature_id) const {
  try {
    const auto& geometry=impl_->feature(feature_id);Json result=Json::array();
    if(!count(geometry.shape,TopAbs_SOLID))throw Error("invalid_argument","3MF print meshes require closed solid geometry");
    std::vector<std::pair<std::string,TopoDS_Shape>> leaves;
    if(geometry.parts.empty())leaves.emplace_back(feature_id.empty()?impl_->output:feature_id,geometry.shape);
    else for(const auto& part:geometry.parts)leaves.emplace_back(part.id,part.shape);
    std::size_t total_vertices=0,total_triangles=0;
    for(const auto& [name,shape]:leaves) {
      ShapeMap solids;TopExp::MapShapes(shape,TopAbs_SOLID,solids);
      for(int i=1;i<=solids.Extent();++i) {
        if(result.size()>=4096)throw Error("limit_exceeded","3MF export permits at most 4096 solids");
        const auto source_id=solids.Extent()==1?name:name+"/solid-"+std::to_string(i);
        const auto& exact_solid=solids(i);BRepBuilderAPI_Copy copy(exact_solid);auto solid=copy.Shape();
        BRepMesh_IncrementalMesh mesh(solid,.1,false,.5,false);
        if(!mesh.IsDone())throw Error("export_failed","Solid tessellation failed",{{"source_id",source_id}});
        bool missing=false;TopLoc_Location location;
        for(TopExp_Explorer face(solid,TopAbs_FACE);face.More();face.Next())if(BRep_Tool::Triangulation(TopoDS::Face(face.Current()),location).IsNull())missing=true;
        if(missing) {
          // Retessellate a private shell with a clean seam on a complete
          // analytic wall. Sewing coordinates boundary discretization across
          // neighbors. The exact model is retained; changed measurements fail.
          BRepBuilderAPI_Sewing sewing(1e-7);
          for(TopExp_Explorer it(solid,TopAbs_FACE);it.More();it.Next()) {
            auto face=TopoDS::Face(it.Current());
            if(BRep_Tool::Triangulation(face,location).IsNull()) {
              const auto clean=periodic_rectangle(face);
              if(!clean)throw Error("export_failed","Cannot produce a closed print mesh for this periodic trim",{{"source_id",source_id}});
              face=*clean;
            }
            sewing.Add(face);
          }
          sewing.Perform();const auto sewn=sewing.SewedShape();
          if(sewn.IsNull()||!BRepCheck_Analyzer(sewn).IsValid())throw Error("export_failed","Periodic print mesh boundary normalization failed",{{"source_id",source_id}});
          GProp_GProps va,vb,aa,ab;BRepGProp::VolumeProperties(solid,va);BRepGProp::VolumeProperties(sewn,vb);BRepGProp::SurfaceProperties(solid,aa);BRepGProp::SurfaceProperties(sewn,ab);
          bool same=std::abs(va.Mass()-vb.Mass())<=1e-7*std::max(1.0,std::abs(va.Mass()))&&std::abs(aa.Mass()-ab.Mass())<=1e-7*std::max(1.0,std::abs(aa.Mass()));
          const auto a=bounds(solid),b=bounds(sewn);for(const auto* end:{"min","max"})for(int k=0;k<3;++k)same=same&&std::abs(a.at(end)[k].get<double>()-b.at(end)[k].get<double>())<=1e-6;
          if(!same)throw Error("export_failed","Periodic mesh normalization changed source measurements",{{"source_id",source_id}});
          solid=sewn;BRepTools::Clean(solid);BRepMesh_IncrementalMesh retry(solid,.1,false,.5,false);
          if(!retry.IsDone())throw Error("export_failed","Periodic print tessellation failed",{{"source_id",source_id}});
        }
        using Key=std::array<long long,3>;std::map<Key,std::vector<std::size_t>> buckets;
        std::vector<gp_Pnt> vertices;std::vector<std::array<std::size_t,3>> triangles;
        const auto bb=bounds(solid);const auto base=bb.at("min").get<std::array<double,3>>();
        const auto vertex=[&](const gp_Pnt& p){
          constexpr double tolerance=1e-7;Key key{};
          for(int k=0;k<3;++k){const double local=p.Coord(k+1)-base[k];if(!std::isfinite(local)||std::abs(local)>1e6)throw Error("limit_exceeded","Print mesh extent exceeds 1,000,000 mm");key[k]=static_cast<long long>(std::floor(local/tolerance));}
          for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z){auto it=buckets.find({key[0]+x,key[1]+y,key[2]+z});if(it!=buckets.end())for(auto index:it->second)if(vertices[index].Distance(p)<=tolerance)return index;}
          if(++total_vertices>2000000)throw Error("limit_exceeded","Print export exceeds two million vertices");
          const auto index=vertices.size();vertices.push_back(p);buckets[key].push_back(index);return index;
        };
        int face_index=0;
        for(TopExp_Explorer it(solid,TopAbs_FACE);it.More();it.Next()) {
          for(const auto& face:tessellated_faces(TopoDS::Face(it.Current()),{{"source_id",source_id},{"face_id","face-"+std::to_string(++face_index)}})) {
            TopLoc_Location loc;const auto tri=BRep_Tool::Triangulation(face,loc);std::vector<std::size_t> ids(tri->NbNodes()+1);
            for(int n=1;n<=tri->NbNodes();++n)ids[n]=vertex(tri->Node(n).Transformed(loc.Transformation()));
            for(int n=1;n<=tri->NbTriangles();++n){int a,b,c;tri->Triangle(n).Get(a,b,c);if(face.Orientation()==TopAbs_REVERSED)std::swap(b,c);
              std::array<std::size_t,3> t={ids[a],ids[b],ids[c]};
              if(t[0]==t[1]||t[1]==t[2]||t[0]==t[2])continue; // Zero-area seam slivers after bounded vertex welding.
              if(++total_triangles>2000000)throw Error("limit_exceeded","Print export exceeds two million triangles");triangles.push_back(t);
            }
          }
        }
        std::map<std::pair<std::size_t,std::size_t>,std::pair<int,int>> edges;
        for(const auto& t:triangles)for(int k=0;k<3;++k){auto a=t[k],b=t[(k+1)%3];const int direction=a<b?1:-1;if(a>b)std::swap(a,b);auto& edge=edges[{a,b}];++edge.first;edge.second+=direction;}
        std::size_t open=0;for(const auto& [key,value]:edges)if(value.first!=2||value.second!=0)++open;
        if(open)throw Error("export_failed","Print mesh is not closed and consistently oriented",{{"source_id",source_id},{"invalid_edge_count",open}});
        double signed_volume=0;const gp_Pnt origin(base[0],base[1],base[2]);
        for(const auto& t:triangles)signed_volume+=gp_Vec(origin,vertices[t[0]]).Dot(gp_Vec(origin,vertices[t[1]]).Crossed(gp_Vec(origin,vertices[t[2]])))/6;
        if(!std::isfinite(signed_volume)||signed_volume<=0)throw Error("export_failed","Print mesh has nonpositive oriented volume",{{"source_id",source_id}});
        Json positions=Json::array(),indices=Json::array();for(const auto& p:vertices)positions.push_back(point(p));for(const auto& t:triangles)indices.push_back(t);
        result.push_back({{"source_id",source_id},{"bounds_mm",bb},{"positions",positions},{"triangles",indices}});
      }
    }
    return result;
  }catch(const Standard_Failure& e){throw occt_error(e,{{"feature_id",feature_id.empty()?impl_->output:feature_id}},"export_failed");}
}

Json BuiltModel::section(const Json& query,const std::string& feature_id) const {
  validate_section_query(query);const auto selected=feature_id.empty()?impl_->output:feature_id;
  try {
    const auto& geometry=impl_->feature(selected);const auto data=summary(selected);
    const auto n=query.at("plane").at("normal").get<std::array<double,3>>();const auto offset=query.at("plane").at("offset_mm").get<double>();
    const auto explode=query.value("explode",Json{{"distance_mm",0},{"directions",Json::array()}});const auto distance=number(explode.at("distance_mm"));
    const auto& box=data.at("bounds_mm");std::array<double,3> center{};double span=0;
    for(std::size_t k=0;k<3;++k){const auto lo=box.at("min")[k].get<double>(),hi=box.at("max")[k].get<double>();center[k]=lo+(hi-lo)/2;span=std::max(span,hi-lo);}
    std::map<std::string,std::array<double,3>> directions,shifts;
    for(const auto& item:explode.at("directions"))directions.emplace(text_field(item,"part_id"),item.at("direction").get<std::array<double,3>>());
    std::map<std::string,TopoDS_Shape> leaves;for(const auto& part:geometry.parts)leaves.emplace(part.id,part.shape);
    if(leaves.empty()&&(distance>0||!directions.empty()||query.contains("part_ids")))throw Error("invalid_argument","Section leaf scopes and exploded offsets require an assembly");
    for(const auto& [id,unused]:directions)if(!leaves.contains(id))throw Error("selection_missing","Section direction names an absent leaf",{{"part_id",id}});
    std::size_t ordinal=0;for(const auto& [id,shape]:leaves){
      std::array<double,3> v{};
      if(directions.contains(id))v=directions.at(id);
      else {const auto b=bounds(shape);for(std::size_t k=0;k<3;++k)v[k]=(b.at("min")[k].get<double>()+(b.at("max")[k].get<double>()-b.at("min")[k].get<double>())/2-center[k])/span;
        if(std::hypot(v[0],v[1],v[2])<1e-12){v={0,0,0};v[ordinal%3]=ordinal%2?-1:1;}}
      const auto length=std::hypot(v[0],v[1],v[2]);for(auto& component:v)component=component/length*distance;shifts[id]=v;++ordinal;
    }
    std::vector<std::pair<Json,TopoDS_Shape>> sources;std::string coverage="feature_solids";
    if(leaves.empty())sources.emplace_back(nullptr,geometry.shape);
    else if(query.contains("part_ids")){coverage="explicit_leaf_subset";for(const auto& value:query.at("part_ids")){const auto id=value.get<std::string>();if(!leaves.contains(id))throw Error("selection_missing","Section subset names an absent leaf",{{"part_id",id}});sources.emplace_back(value,leaves.at(id));}}
    else {coverage="all_assembly_leaves";for(const auto& part:geometry.parts)sources.emplace_back(part.id,part.shape);}
    Json sections=Json::array(),regions=Json::array(),curves=Json::array(),positions=Json::array(),triangles=Json::array(),triangle_regions=Json::array();
    double total_area=0,total_length=0;std::size_t total_solids=0,total_points=0,total_contacts=0;
    const auto finite=[&](const gp_Pnt& p,double plane_offset){
      if(!std::isfinite(p.X())||!std::isfinite(p.Y())||!std::isfinite(p.Z())||std::max({std::abs(p.X()),std::abs(p.Y()),std::abs(p.Z())})>1e12||std::abs(n[0]*p.X()+n[1]*p.Y()+n[2]*p.Z()-plane_offset)>section_point_tolerance_mm)
        throw Error("kernel_failure","Section point is non-finite, outside coordinate bounds or off its source plane");
      return point(p);
    };
    for(const auto& [owner,source]:sources){
      const auto shift=owner.is_null()?std::array<double,3>{0,0,0}:shifts.at(owner.get<std::string>());const auto plane_offset=offset-n[0]*shift[0]-n[1]*shift[1]-n[2]*shift[2];
      double area=0,length=0;const auto region_start=regions.size(),curve_start=curves.size();Json contacts=Json::array();
      ShapeMap solids;TopExp::MapShapes(source,TopAbs_SOLID,solids);if(solids.IsEmpty())throw Error("invalid_argument","Exact sections require source solids");
      if(total_solids+solids.Extent()>section_part_limit)throw Error("limit_exceeded","Section exceeds its aggregate 1024-solid budget");total_solids+=solids.Extent();
      for(int solid=1;solid<=solids.Extent();++solid){
        const auto b=bounds(solids(solid));double lo=0,hi=0;for(std::size_t k=0;k<3;++k){const auto a=n[k]*b.at("min")[k].get<double>(),z=n[k]*b.at("max")[k].get<double>();lo+=std::min(a,z);hi+=std::max(a,z);}
        if(plane_offset<lo-1e-7||plane_offset>hi+1e-7)continue;
        const auto square=n[0]*n[0]+n[1]*n[1]+n[2]*n[2];const gp_Pln plane(gp_Pnt(n[0]*plane_offset/square,n[1]*plane_offset/square,n[2]*plane_offset/square),gp_Dir(n[0],n[1],n[2]));
        const auto face=BRepBuilderAPI_MakeFace(plane).Face();NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(solids(solid));tools.Append(face);
        BRepAlgoAPI_Section intersection;intersection.SetArguments(arguments);intersection.SetTools(tools);intersection.SetNonDestructive(true);intersection.SetRunParallel(false);intersection.Approximation(false);intersection.Build();
        if(!intersection.IsDone()||intersection.HasErrors())throw Error("kernel_failure","Native source-plane intersection failed");
        ShapeMap edges;TopExp::MapShapes(intersection.Shape(),TopAbs_EDGE,edges);
        if(regions.size()+curves.size()+edges.Extent()+total_contacts>section_entity_limit)throw Error("limit_exceeded","Section exceeds its aggregate entity budget");
        for(int index=1;index<=edges.Extent();++index){
          const auto edge=TopoDS::Edge(edges(index));auto item=edge_descriptor(edge,1);item["id"]="section-"+std::to_string(curves.size()+1);item["part_id"]=owner;item["solid_index"]=solid;item["points"]=Json::array();
          if(item.at("degenerate").get<bool>()){
            ShapeMap vertices;TopExp::MapShapes(edge,TopAbs_VERTEX,vertices);
            if(vertices.IsEmpty())throw Error("kernel_failure","Degenerate section edge has no native contact vertex");
            item["center_mm"]=finite(BRep_Tool::Pnt(TopoDS::Vertex(vertices(1))),plane_offset);
          }else{
            const auto center=item.at("center_mm").get<std::array<double,3>>();item["center_mm"]=finite(gp_Pnt(center[0],center[1],center[2]),plane_offset);
          }
          if(!item.at("degenerate").get<bool>()){
            BRepAdaptor_Curve curve(edge);GCPnts_QuasiUniformDeflection sampling(curve,section_deflection_mm);if(!sampling.IsDone())throw Error("kernel_failure","Section curve tessellation failed");
            if(total_points+sampling.NbPoints()>section_point_limit)throw Error("limit_exceeded","Section exceeds its aggregate edge-point budget");total_points+=sampling.NbPoints();
            for(int i=1;i<=sampling.NbPoints();++i)item["points"].push_back(finite(sampling.Value(i),plane_offset));
          }
          const auto value=item.at("length_mm").get<double>();if(!std::isfinite(value)||value<0)throw Error("kernel_failure","Section curve length is invalid");length+=value;curves.push_back(std::move(item));
        }
        ShapeMap edge_vertices,vertices;for(int i=1;i<=edges.Extent();++i)if(!BRep_Tool::Degenerated(TopoDS::Edge(edges(i))))TopExp::MapShapes(edges(i),TopAbs_VERTEX,edge_vertices);
        TopExp::MapShapes(intersection.Shape(),TopAbs_VERTEX,vertices);
        for(int i=1;i<=vertices.Extent();++i)if(!edge_vertices.Contains(vertices(i))){
          if(regions.size()+curves.size()+total_contacts+1>section_entity_limit)throw Error("limit_exceeded","Section exceeds its contact-point budget");
          contacts.push_back(finite(BRep_Tool::Pnt(TopoDS::Vertex(vertices(i))),plane_offset));++total_contacts;
        }
        BRepAlgoAPI_Common material;material.SetArguments(arguments);material.SetTools(tools);material.SetNonDestructive(true);material.SetRunParallel(false);material.Build();
        if(!material.IsDone()||material.HasErrors())throw Error("kernel_failure","Section material intersection failed");
        ShapeMap faces;TopExp::MapShapes(material.Shape(),TopAbs_FACE,faces);
        if(regions.size()+curves.size()+faces.Extent()+total_contacts>section_entity_limit)throw Error("limit_exceeded","Section exceeds its aggregate entity budget");
        for(int i=1;i<=faces.Extent();++i){
          const auto cap=TopoDS::Face(faces(i));if(!BRepCheck_Analyzer(cap).IsValid())throw Error("kernel_failure","Section material face is invalid");
          GProp_GProps surface,boundary;BRepGProp::SurfaceProperties(cap,surface);BRepGProp::LinearProperties(cap,boundary);
          if(!std::isfinite(surface.Mass())||surface.Mass()<=0||!std::isfinite(boundary.Mass())||boundary.Mass()<0)throw Error("kernel_failure","Section cap measures are invalid");
          const auto id="cap-"+std::to_string(regions.size()+1);regions.push_back({{"id",id},{"part_id",owner},{"solid_index",solid},{"area_mm2",surface.Mass()},{"perimeter_mm",boundary.Mass()},{"center_mm",finite(surface.CentreOfMass(),plane_offset)},{"wire_count",count(cap,TopAbs_WIRE)}});area+=surface.Mass();
          BRepMesh_IncrementalMesh tessellation(cap,section_deflection_mm,false,.5,false);if(!tessellation.IsDone())throw Error("kernel_failure","Section cap tessellation failed");
          TopLoc_Location location;const auto mesh=BRep_Tool::Triangulation(cap,location);if(mesh.IsNull())throw Error("kernel_failure","Section cap has no triangulation");
          const auto base=positions.size();if(base+mesh->NbNodes()>section_vertex_limit||triangles.size()+mesh->NbTriangles()>section_triangle_limit)throw Error("limit_exceeded","Section exceeds aggregate mesh bounds");
          for(int node=1;node<=mesh->NbNodes();++node)positions.push_back(finite(mesh->Node(node).Transformed(location.Transformation()),plane_offset));
          for(int t=1;t<=mesh->NbTriangles();++t){int a,b,c;mesh->Triangle(t).Get(a,b,c);if(cap.Orientation()==TopAbs_REVERSED)std::swap(b,c);triangles.push_back({base+a-1,base+b-1,base+c-1});triangle_regions.push_back(id);}
        }
      }
      total_area+=area;total_length+=length;sections.push_back({{"part_id",owner},{"source_plane_offset_mm",plane_offset},{"displacement_mm",shift},{"area_mm2",area},{"boundary_length_mm",length},
        {"region_count",regions.size()-region_start},{"curve_count",curves.size()-curve_start},{"contact_points",contacts},{"status",area>0?"area":curves.size()>curve_start||!contacts.empty()?"tangent":"empty"}});
    }
    if(!std::isfinite(total_area)||!std::isfinite(total_length))throw Error("kernel_failure","Section totals are non-finite");
    Json result={{"schema_version",1},{"units","mm"},{"action","section"},{"method","native_BRep_planar_section"},{"coordinate_space","committed_source_pose"},{"plane_coordinate_space","displayed_world_mm"},
      {"coverage",coverage},{"area_semantics","sum_of_solid_sections"},{"plane",query.at("plane")},{"explode",explode},{"sections",sections},{"regions",regions},{"curves",curves},
      {"mesh",{{"positions",positions},{"triangles",triangles},{"triangle_regions",triangle_regions},{"linear_deflection_mm",section_deflection_mm}}},
      {"area_mm2",total_area},{"boundary_length_mm",total_length},{"status",total_area>0?"area":!curves.empty()||total_contacts?"tangent":"empty"},{"selection_lifetime","section_result"},{"tolerance_mm",1e-7},{"point_tolerance_mm",section_point_tolerance_mm}};
    if(result.dump().size()>section_bytes_limit)throw Error("limit_exceeded","Section report exceeds 8 MiB");return result;
  }catch(const Error& e){auto details=e.details;details["feature_id"]=selected;throw Error(e.code,e.what(),details);}
  catch(const Standard_Failure& e){throw occt_error(e,{{"feature_id",selected}});}
}

namespace {
constexpr double fabrication_tolerance=1e-6;
Json fabrication_check(const std::string& id,const std::string& status,const std::string& method,
                       const std::string& reason,Json evidence=Json::object()) {
  return {{"id",id},{"status",status},{"method",method},{"reason",reason},{"evidence",std::move(evidence)}};
}
std::string fabrication_status(const Json& checks) {
  bool unknown=false;
  for(const auto& c:checks){if(c.at("status")=="fail")return "fail";if(c.at("status")=="unknown")unknown=true;}
  return unknown||checks.empty()?"unknown":"pass";
}
struct FabricationFrame {
  gp_Dir x,y,z;gp_Trsf transform;
  explicit FabricationFrame(const Json& profile):
    x(profile.at("orientation").at("x_direction")[0].get<double>(),profile.at("orientation").at("x_direction")[1].get<double>(),profile.at("orientation").at("x_direction")[2].get<double>()),
    y(0,1,0),
    z(profile.at("orientation").at("build_direction")[0].get<double>(),profile.at("orientation").at("build_direction")[1].get<double>(),profile.at("orientation").at("build_direction")[2].get<double>()) {
    y=gp_Dir(gp_Vec(z).Crossed(gp_Vec(x)));x=gp_Dir(gp_Vec(y).Crossed(gp_Vec(z)));
    transform.SetTransformation(gp_Ax3(gp_Pnt(0,0,0),z,x));
  }
  double height(const gp_Pnt& p)const{return gp_Vec(gp_Pnt(0,0,0),p).Dot(gp_Vec(z));}
};
Json fabrication_mesh_checks(const Json& mesh,const Json& profile,double bed_height) {
  using Cell=std::array<long long,3>;
  std::map<Cell,std::vector<std::size_t>> cells;std::vector<gp_Pnt> welded;
  std::vector<std::size_t> ids;ids.reserve(mesh.at("positions").size());
  for(const auto& p:mesh.at("positions")) {
    gp_Pnt position(p[0].get<double>(),p[1].get<double>(),p[2].get<double>());
    Cell cell={static_cast<long long>(std::floor(position.X()/fabrication_tolerance)),
      static_cast<long long>(std::floor(position.Y()/fabrication_tolerance)),static_cast<long long>(std::floor(position.Z()/fabrication_tolerance))};
    std::optional<std::size_t> found;
    for(int a=-1;a<=1&&!found;++a)for(int b=-1;b<=1&&!found;++b)for(int c=-1;c<=1&&!found;++c) {
      const auto it=cells.find({cell[0]+a,cell[1]+b,cell[2]+c});
      if(it!=cells.end())for(const auto id:it->second)if(position.Distance(welded[id])<=fabrication_tolerance){found=id;break;}
    }
    if(!found){found=welded.size();welded.push_back(position);cells[cell].push_back(*found);}ids.push_back(*found);
  }
  struct Edge {std::size_t count=0;int balance=0;};
  std::map<std::array<std::size_t,2>,Edge> edges;
  std::size_t degenerate=0,overhangs=0,bed_faces=0;
  double volume=0,area=0,overhang_area=0,max_angle=0;
  Json witnesses=Json::array();const FabricationFrame frame(profile);
  const auto origin=welded.empty()?gp_Pnt(0,0,0):welded.front();
  for(std::size_t i=0;i<mesh.at("triangles").size();++i) {
    const auto& t=mesh.at("triangles")[i];std::array<std::size_t,3> v={ids.at(t[0].get<std::size_t>()),ids.at(t[1].get<std::size_t>()),ids.at(t[2].get<std::size_t>())};
    const auto& a=welded[v[0]];const auto& b=welded[v[1]];const auto& c=welded[v[2]];
    const auto cross=gp_Vec(a,b).Crossed(gp_Vec(a,c));const auto twice_area=cross.Magnitude();area+=twice_area/2;
    volume+=gp_Vec(origin,a).Dot(gp_Vec(origin,b).Crossed(gp_Vec(origin,c)))/6;
    if(v[0]==v[1]||v[1]==v[2]||v[2]==v[0]||twice_area<=fabrication_tolerance*fabrication_tolerance){++degenerate;continue;}
    for(int j=0;j<3;++j){auto u=v[j],w=v[(j+1)%3];const auto forward=u<w;if(!forward)std::swap(u,w);auto& edge=edges[{u,w}];++edge.count;edge.balance+=forward?1:-1;}
    if(profile.at("process")=="fdm") {
      if(std::abs(frame.height(a)-bed_height)<=fabrication_tolerance&&std::abs(frame.height(b)-bed_height)<=fabrication_tolerance&&std::abs(frame.height(c)-bed_height)<=fabrication_tolerance){++bed_faces;continue;}
      const auto cosine=cross.Dot(gp_Vec(frame.z))/twice_area;
      const auto angle=std::asin(std::clamp(-cosine,0.0,1.0))*180/std::acos(-1.0);max_angle=std::max(max_angle,angle);
      if(profile.contains("overhang_angle_deg")&&angle>profile.at("overhang_angle_deg").get<double>()+1e-6) {
        ++overhangs;overhang_area+=twice_area/2;
        if(witnesses.size()<32)witnesses.push_back({{"triangle_index",i},{"face_id",mesh.at("triangle_faces")[i]},
          {"point_mm",{(a.X()+b.X()+c.X())/3,(a.Y()+b.Y()+c.Y())/3,(a.Z()+b.Z()+c.Z())/3}},{"angle_from_vertical_deg",angle}});
      }
    }
  }
  std::size_t boundary=0,nonmanifold=0,winding=0;
  for(const auto& [key,edge]:edges){if(edge.count==1)++boundary;else if(edge.count!=2)++nonmanifold;else if(edge.balance!=0)++winding;}
  Json checks=Json::array({fabrication_check("mesh_topology",degenerate||boundary||nonmanifold||winding||volume<=0?"fail":"pass",
    "native_tessellation_welded_edge_incidence","Topology and winding of the measured mesh only; not a self-intersection certificate.",
    {{"linear_deflection_mm",mesh.at("linear_deflection_mm")},{"angular_deflection_rad",mesh.at("angular_deflection_rad")},
     {"weld_tolerance_mm",fabrication_tolerance},{"triangle_count",mesh.at("triangles").size()},{"welded_vertices",welded.size()},
     {"degenerate_triangles",degenerate},{"boundary_edges",boundary},{"nonmanifold_edges",nonmanifold},{"inconsistent_edges",winding},
     {"signed_volume_mm3",volume},{"area_mm2",area}}),
    fabrication_check("mesh_self_intersections","unknown","not_evaluated","Triangle/triangle self-intersection and slicer repair have not been evaluated.")});
  if(profile.at("process")=="fdm")checks.push_back(fabrication_check("fdm_overhang",
    !profile.contains("overhang_angle_deg")?"unknown":overhangs?"fail":"pass","all_native_mesh_triangles",
    profile.contains("overhang_angle_deg")?"Triangles exceeding the supplied angle; support design and slicing remain unevaluated.":"No overhang angle was supplied.",
    {{"max_angle_from_vertical_deg",max_angle},{"triangles_exceeding_limit",overhangs},{"area_exceeding_limit_mm2",overhang_area},
     {"bed_contact_triangles_excluded",bed_faces},{"bed_translation_mm",-bed_height},{"witnesses",witnesses},{"witnesses_truncated",overhangs>witnesses.size()}}));
  return checks;
}
struct FabricationSample {gp_Pnt p;gp_Dir n;int face;};
std::vector<FabricationSample> fabrication_samples(const FeatureGeometry& geometry,std::size_t limit) {
  std::vector<FabricationSample> samples;const auto faces=geometry.faces.Extent();
  // Evenly distribute the finite budget over faces, with two interior UV samples
  // per admitted face. UV centroids are reclassified against the exact trim.
  const auto admitted=std::min<std::size_t>(faces,(limit+1)/2);
  for(std::size_t slot=0;slot<admitted&&samples.size()<limit;++slot) {
    const auto index=1+static_cast<int>(slot*faces/admitted);const auto face=TopoDS::Face(geometry.faces(index));
    TopLoc_Location location;const auto triangles=BRep_Tool::Triangulation(face,location);
    if(triangles.IsNull()||!triangles->HasUVNodes())continue;
    BRepAdaptor_Surface surface(face);
    for(int pick=0;pick<2&&samples.size()<limit;++pick) {
      const auto start=1+(triangles->NbTriangles()*(pick?3:1))/4;
      for(int attempt=0;attempt<std::min(8,triangles->NbTriangles());++attempt) {
        const auto tri=1+(start-1+attempt)%triangles->NbTriangles();int a,b,c;triangles->Triangle(tri).Get(a,b,c);
        const auto ua=triangles->UVNode(a),ub=triangles->UVNode(b),uc=triangles->UVNode(c);
        const gp_Pnt2d uv((ua.X()+ub.X()+uc.X())/3,(ua.Y()+ub.Y()+uc.Y())/3);
        if(BRepClass_FaceClassifier(face,uv,1e-9,true).State()!=TopAbs_IN)continue;
        BRepLProp_SLProps props(surface,uv.X(),uv.Y(),1,1e-9);if(!props.IsNormalDefined())continue;
        auto n=props.Normal();if(face.Orientation()==TopAbs_REVERSED)n.Reverse();
        const auto p=surface.Value(uv.X(),uv.Y());
        bool duplicate=false;for(const auto& old:samples)if(old.face==index&&p.Distance(old.p)<fabrication_tolerance)duplicate=true;
        if(!duplicate)samples.push_back({p,n,index});break;
      }
    }
  }
  return samples;
}
bool fabrication_inside(const TopoDS_Shape& shape,const gp_Pnt& p) {
  for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next())if(BRepClass3d_SolidClassifier(it.Current(),p,1e-8).State()==TopAbs_IN)return true;
  return false;
}
std::optional<double> fabrication_ray(IntCurvesFace_ShapeIntersector& ray,const gp_Pnt& p,const gp_Dir& direction,double length) {
  ray.Perform(gp_Lin(p,direction),fabrication_tolerance,length);
  if(!ray.IsDone())throw Error("kernel_failure","Exact fabrication ray intersection failed");
  double nearest=length+1;
  for(int i=1;i<=ray.NbPnt();++i)if(ray.WParameter(i)>=fabrication_tolerance)nearest=std::min(nearest,ray.WParameter(i));
  return nearest<=length?std::optional<double>(nearest):std::nullopt;
}
Json fabrication_surface_checks(const FeatureGeometry& geometry,const Json& profile,const Json& oriented_bounds,
                                const std::vector<FabricationSample>& samples) {
  const FabricationFrame frame(profile);const auto process=text_field(profile,"process");
  const auto lo=oriented_bounds.at("min").get<std::array<double,3>>(),hi=oriented_bounds.at("max").get<std::array<double,3>>();
  const auto length=std::hypot(hi[0]-lo[0],hi[1]-lo[1],hi[2]-lo[2])+1;
  IntCurvesFace_ShapeIntersector ray;ray.Load(geometry.shape,1e-8);
  double minimum=std::numeric_limits<double>::max(),draft_min=90;
  std::size_t chords=0,thin=0,draft_count=0,draft_bad=0,undercuts=0,access_count=0,blocked=0,unresolved=0,parting_samples=0;
  Json wall_witnesses=Json::array(),mold_witnesses=Json::array(),access_witnesses=Json::array();std::set<int> covered;
  for(const auto& sample:samples) {
    covered.insert(sample.face);
    auto inward=sample.n;inward.Reverse();
    if(fabrication_inside(geometry.shape,sample.p.Translated(gp_Vec(inward)*fabrication_tolerance))) {
      const auto chord=fabrication_ray(ray,sample.p,inward,length);
      if(chord&&fabrication_inside(geometry.shape,sample.p.Translated(gp_Vec(inward)*(*chord/2)))) {
        ++chords;minimum=std::min(minimum,*chord);
        if(profile.contains("minimum_wall_mm")&&*chord+fabrication_tolerance<profile.at("minimum_wall_mm").get<double>()) {
          ++thin;if(wall_witnesses.size()<32)wall_witnesses.push_back({{"face_id","face-"+std::to_string(sample.face)},
            {"point_mm",point(sample.p)},{"inward_direction",direction(inward)},{"chord_mm",*chord}});
        }
      }else ++unresolved;
    }else ++unresolved;
    if(process=="cnc"&&sample.n.Dot(frame.z)>1e-7) {
      ++access_count;const auto hit=fabrication_ray(ray,sample.p,frame.z,length);
      if(hit){++blocked;if(access_witnesses.size()<32)access_witnesses.push_back({{"face_id","face-"+std::to_string(sample.face)},{"point_mm",point(sample.p)},{"obstruction_distance_mm",*hit}});}
    }
    if(process=="molding"&&profile.contains("parting_plane_mm")) {
      const auto height=frame.height(sample.p)-profile.at("parting_plane_mm").get<double>();
      if(std::abs(height)<=fabrication_tolerance){++parting_samples;continue;}
      auto pull=frame.z;if(height<0)pull.Reverse();
      const auto dot=std::clamp(sample.n.Dot(pull),-1.0,1.0);
      const auto draft=std::asin(dot)*180/std::acos(-1.0);
      ++draft_count;draft_min=std::min(draft_min,draft);
      const auto hit=fabrication_ray(ray,sample.p,pull,length);
      const auto undercut=dot < -1e-7||hit.has_value();
      if(undercut)++undercuts;
      const auto insufficient=profile.contains("minimum_draft_deg")&&draft+1e-6<profile.at("minimum_draft_deg").get<double>();
      if(insufficient)++draft_bad;
      if((undercut||insufficient)&&mold_witnesses.size()<32)mold_witnesses.push_back({{"face_id","face-"+std::to_string(sample.face)},
        {"point_mm",point(sample.p)},{"pull_direction",direction(pull)},{"draft_deg",draft},{"undercut",undercut}});
    }
  }
  Json coverage={{"samples",samples.size()},{"sampled_faces",covered.size()},{"total_faces",geometry.faces.Extent()},
    {"sampling","up_to_two_exact_trimmed_UV_points_per_admitted_face"},{"maximum_samples",fabrication_sample_limit}};
  Json wall=coverage;wall["measured_chords"]=chords;wall["unresolved_chords"]=unresolved;wall["below_limit"]=thin;wall["witnesses"]=wall_witnesses;
  wall["witnesses_truncated"]=thin>wall_witnesses.size();if(chords)wall["minimum_sampled_chord_mm"]=minimum;
  Json checks=Json::array({fabrication_check("sampled_wall_thickness",
    !profile.contains("minimum_wall_mm")||!chords?"unknown":thin?"fail":unresolved?"unknown":"pass",
    "exact_inward_normal_ray_chords_at_finite_UV_samples","Only sampled normal chords are measured; thin regions between samples can be missed.",wall),
    fabrication_check("global_minimum_wall","unknown","not_evaluated","Finite surface samples do not prove the global minimum wall thickness.")});
  if(process=="cnc") {
    Json radii=Json::array();std::size_t unsupported=0,too_small=0;
    for(int i=1;i<=geometry.faces.Extent();++i) {
      const auto face=TopoDS::Face(geometry.faces(i));BRepAdaptor_Surface surface(face);
      if(surface.GetType()!=GeomAbs_Cylinder)continue;
      const auto cylinder=surface.Cylinder();
      const auto sample=std::find_if(samples.begin(),samples.end(),[&](const auto& s){return s.face==i;});
      if(sample==samples.end()){++unsupported;continue;}
      gp_Vec radial(cylinder.Location(),sample->p);radial-=gp_Vec(cylinder.Axis().Direction())*radial.Dot(gp_Vec(cylinder.Axis().Direction()));
      if(radial.Dot(gp_Vec(sample->n))>=0)continue;
      if(std::abs(cylinder.Axis().Direction().Dot(frame.z))<1-1e-7){++unsupported;continue;}
      radii.push_back({{"face_id","face-"+std::to_string(i)},{"radius_mm",cylinder.Radius()}});
      if(profile.contains("tool_radius_mm")&&cylinder.Radius()+fabrication_tolerance<profile.at("tool_radius_mm").get<double>())++too_small;
    }
    checks.push_back(fabrication_check("cnc_internal_cylinder_radius",!profile.contains("tool_radius_mm")||radii.empty()?"unknown":too_small?"fail":unsupported?"unknown":"pass",
      "exact_axial_concave_cylindrical_faces","Checks axial cylindrical concavities only, not all pocket corners or swept cutter reach.",
      {{"cylinders",radii},{"below_tool_radius",too_small},{"unsupported_cylinders",unsupported}}));
    checks.push_back(fabrication_check("cnc_point_access",!access_count?"unknown":blocked?"fail":"pass","exact_axial_rays_at_upward_facing_samples",
      "Point visibility from the stated tool axis; cutter radius, stock, fixtures and toolpaths remain unevaluated.",
      {{"coverage",coverage},{"samples_examined",access_count},{"blocked_samples",blocked},{"witnesses",access_witnesses}}));
    checks.push_back(fabrication_check("cnc_toolpath_and_stock","unknown","not_evaluated","Swept cutter access, non-cylindrical internal corners, stock and fixtures have not been evaluated."));
  } else if(process=="sheet_laser") {
    std::size_t unsupported=0,invalid=0,caps_min=0,caps_max=0;Json faces=Json::array();
    for(int i=1;i<=geometry.faces.Extent();++i) {
      const auto face=TopoDS::Face(geometry.faces(i));BRepAdaptor_Surface surface(face);
      const auto kind=surface.GetType();bool bad=false,unknown=false;
      if(kind==GeomAbs_Plane) {
        const auto plane=surface.Plane();const auto dot=std::abs(plane.Axis().Direction().Dot(frame.z));
        if(dot>1-1e-9) {
          const auto z=frame.height(plane.Location());
          if(std::abs(z-lo[2])<=fabrication_tolerance)++caps_min;
          else if(std::abs(z-hi[2])<=fabrication_tolerance)++caps_max;
          else bad=true;
        } else if(dot>1e-9)bad=true;
      }else if(kind==GeomAbs_Cylinder)bad=std::abs(surface.Cylinder().Axis().Direction().Dot(frame.z))<1-1e-9;
      else if(kind==GeomAbs_SurfaceOfExtrusion)bad=std::abs(surface.Direction().Dot(frame.z))<1-1e-9;
      else if(kind==GeomAbs_Cone||kind==GeomAbs_Sphere||kind==GeomAbs_Torus)bad=true;
      else unknown=true;
      if(bad)++invalid;if(unknown)++unsupported;
      if((bad||unknown)&&faces.size()<32)faces.push_back({{"face_id","face-"+std::to_string(i)},{"surface_kind",surface_kind(kind)},{"unsupported",unknown}});
    }
    const auto prismatic=invalid?"fail":unsupported?"unknown":caps_min&&caps_max?"pass":"fail";
    checks.push_back(fabrication_check("sheet_prismatic",prismatic,"exact_caps_and_parallel_plane_cylinder_extrusion_walls",
      "Constant-height sheet extrusion in the supplied orientation; unsupported surface types remain unknown.",
      {{"thickness_mm",hi[2]-lo[2]},{"bottom_caps",caps_min},{"top_caps",caps_max},{"nonprismatic_faces",invalid},{"unsupported_faces",unsupported},{"witnesses",faces}}));
    const auto stock=profile.contains("sheet_thickness_mm");
    checks.push_back(fabrication_check("sheet_stock_thickness",!stock||std::string(prismatic)!="pass"?"unknown":
      std::abs(hi[2]-lo[2]-profile.at("sheet_thickness_mm").get<double>())<=profile.at("sheet_thickness_tolerance_mm").get<double>()+fabrication_tolerance?"pass":"fail",
      "exact_oriented_height_of_verified_prismatic_shape","Stock comparison requires an explicit nominal thickness and allowance.",
      {{"measured_thickness_mm",hi[2]-lo[2]}}));
    checks.push_back(fabrication_check("sheet_kerf_bends_and_material","unknown","not_evaluated","Kerf, bend development, material, machine and vendor acceptance have not been evaluated."));
  } else if(process=="molding") {
    const auto measured=draft_count>0;Json evidence={{"coverage",coverage},{"measured_samples",draft_count},{"on_parting_plane_samples_skipped",parting_samples},
      {"insufficient_draft_samples",draft_bad},{"undercut_samples",undercuts},{"witnesses",mold_witnesses}};
    if(measured)evidence["minimum_sampled_signed_draft_deg"]=draft_min;
    checks.push_back(fabrication_check("molding_sampled_draft",!profile.contains("minimum_draft_deg")||!measured?"unknown":draft_bad?"fail":"pass",
      "exact_surface_normals_at_finite_UV_samples","Signed draft from the supplied build/pull axis; both sides pull away from the stated parting plane.",evidence));
    checks.push_back(fabrication_check("molding_sampled_undercuts",!measured?"unknown":undercuts?"fail":"pass","exact_normals_and_axial_rays_at_finite_UV_samples",
      "Only sampled normals and pull rays are checked; global release, cores and tooling remain unevaluated.",evidence));
    checks.push_back(fabrication_check("molding_global_release_and_flow","unknown","not_evaluated","Global mold release, parting surfaces, cores, shrinkage, fill and material have not been evaluated."));
  }
  return checks;
}
}

Json BuiltModel::fabrication_review(const Json& options,const std::string& feature_id)const {
  validate_fabrication_options(options);
  const auto selected=feature_id.empty()?impl_->output:feature_id;
  try {
    const auto& geometry=impl_->feature(selected);if(!count(geometry.shape,TopAbs_SOLID))throw Error("invalid_argument","Fabrication review requires solid output",{{"feature_id",selected}});
    std::map<std::string,Json> sources,overrides;
    if(geometry.parts.empty())sources[selected]=Json::array({selected});
    else for(const auto& part:geometry.parts){if(!sources.contains(part.input))sources[part.input]=Json::array();sources[part.input].push_back(part.id);}
    if(sources.size()>fabrication_part_limit)throw Error("limit_exceeded","Too many fabrication source parts");
    for(const auto& part:options.value("parts",Json::array())) {
      const auto id=text_field(part,"feature_id");if(!sources.contains(id))throw Error("invalid_argument","Part process profile must name a selected leaf source",{{"feature_id",id}});
      overrides[id]=part.at("profile");
    }
    Json result={{"schema_version",1},{"status","unknown"},{"coordinate_policy","source_features_and_saved_occurrences"},
      {"selection_lifetime","report"},{"geometric_tolerance_mm",fabrication_tolerance},{"checks",Json::array()},{"parts",Json::array()},
      {"guidance",{{"basis","caller_limits_only"},{"sources",Json::array()}}}};
    std::size_t total_triangles=0,samples_left=fabrication_total_samples;
    for(const auto& [id,paths]:sources) {
      try {
        const auto& part=impl_->feature(id);const auto& profile=overrides.contains(id)?overrides.at(id):options.at("profile");const FabricationFrame frame(profile);
        const auto transformed=BRepBuilderAPI_Transform(part.shape,frame.transform,true).Shape();const auto box=bounds(transformed);
        Json size=Json::array();bool fits=true;for(int axis=0;axis<3;++axis){const auto extent=box.at("max")[axis].get<double>()-box.at("min")[axis].get<double>();size.push_back(extent);
          if(profile.contains("build_envelope_mm")&&extent>profile.at("build_envelope_mm")[axis].get<double>()+fabrication_tolerance)fits=false;}
        Json checks=Json::array({fabrication_check("build_envelope",!profile.contains("build_envelope_mm")?"unknown":fits?"pass":"fail","exact_oriented_BRep_bounds",
          "Source coordinates expressed in the supplied build frame; extents assume translation to the bed minimum, without rotation search, supports or fixtures.",
          {{"bounds_mm",box},{"size_mm",size},{"normalized_basis",Json::array({direction(frame.x),direction(frame.y),direction(frame.z)})}})});
        const auto triangles=mesh(id);total_triangles+=triangles.at("triangles").size();
        if(total_triangles>200000)throw Error("limit_exceeded","Fabrication review exceeds its aggregate triangle budget");
        for(const auto& check:fabrication_mesh_checks(triangles,profile,box.at("min")[2].get<double>()))checks.push_back(check);
        const auto samples=fabrication_samples(part,std::min(samples_left,fabrication_sample_limit));samples_left-=samples.size();
        for(const auto& check:fabrication_surface_checks(part,profile,box,samples))checks.push_back(check);
        if(profile.at("process")=="fdm")checks.push_back(fabrication_check("fdm_slicing_and_material","unknown","not_evaluated","Supports, material, machine/profile compatibility and actual slicing have not been evaluated."));
        result["parts"].push_back({{"feature_id",id},{"quantity",paths.size()},{"part_ids",paths},{"profile",profile},{"status",fabrication_status(checks)},{"checks",checks}});
      }catch(const Error& error){auto details=error.details;details["feature_id"]=id;throw Error(error.code,error.what(),details);}
    }
    Json pairs=options.value("clearance_pairs",Json::array());bool complete=!options.contains("clearance_pairs");
    const auto auto_pair_limit=23; // 23*22/2 = 253, within the exact pair budget.
    if(complete&&geometry.parts.size()<=auto_pair_limit)for(std::size_t a=0;a<geometry.parts.size();++a)for(std::size_t b=a+1;b<geometry.parts.size();++b)pairs.push_back({{"a",geometry.parts[a].id},{"b",geometry.parts[b].id}});
    if(complete&&geometry.parts.size()>auto_pair_limit)complete=false;
    Json measured=Json::array();std::size_t interference=0,too_close=0;double minimum=std::numeric_limits<double>::max();
    for(const auto& pair:pairs) {
      const auto a=text_field(pair,"a"),b=text_field(pair,"b");
      const auto find=[&](const std::string& id)->const TopoDS_Shape&{for(const auto& part:geometry.parts)if(part.id==id)return part.shape;throw Error("invalid_argument","Clearance pair requires a selected leaf occurrence",{{"part_id",id}});};
      const auto& sa=find(a);const auto& sb=find(b);
      BRepExtrema_DistShapeShape distance;distance.SetMultiThread(false);distance.LoadS1(sa);distance.LoadS2(sb);distance.SetDeflection(1e-7);distance.Perform();
      if(!distance.IsDone())throw Error("kernel_failure","Exact occurrence distance failed",{{"a",a},{"b",b}});
      BRepAlgoAPI_Common common;NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(sa);tools.Append(sb);
      common.SetArguments(arguments);common.SetTools(tools);common.SetNonDestructive(true);common.SetRunParallel(false);common.Build();
      if(!common.IsDone())throw Error("kernel_failure","Exact occurrence intersection failed",{{"a",a},{"b",b}});
      // Common can contain only a touching face/edge. Integrate solids only:
      // an open face's divergence integral is not common material volume.
      double overlap=0;
      if(!common.Shape().IsNull())for(TopExp_Explorer it(common.Shape(),TopAbs_SOLID);it.More();it.Next()) {
        GProp_GProps volume;BRepGProp::VolumeProperties(it.Current(),volume,true);overlap+=std::max(0.0,volume.Mass());
      }
      const auto gap=distance.Value();minimum=std::min(minimum,gap);
      const auto intersects=overlap>1e-9;if(intersects)++interference;
      if(options.contains("minimum_clearance_mm")&&(intersects||gap+fabrication_tolerance<options.at("minimum_clearance_mm").get<double>()))++too_close;
      Json item={{"a",a},{"b",b},{"distance_mm",gap},{"intersection_volume_mm3",overlap},{"interference",intersects}};
      if(distance.NbSolution()){item["point_a_mm"]=point(distance.PointOnShape1(1));item["point_b_mm"]=point(distance.PointOnShape2(1));}
      measured.push_back(item);
    }
    const auto has_pairs=!measured.empty();Json evidence={{"pairs",measured},{"scope",complete?"all_selected_occurrences":"explicit_or_unmeasured_pairs"},
      {"selected_occurrences",geometry.parts.size()},{"pair_limit",fabrication_pair_limit},{"intersection_volume_tolerance_mm3",1e-9}};
    if(has_pairs)evidence["minimum_distance_mm"]=minimum;
    result["checks"].push_back(fabrication_check("assembly_interference",!has_pairs?"unknown":interference?"fail":"pass","exact_BRep_pair_common_volume",
      has_pairs?"Positive common material volume for the recorded saved-pose pairs.":"No occurrence pairs measured; more than 23 occurrences require explicit bounded pairs.",evidence));
    result["checks"].push_back(fabrication_check("assembly_clearance",!has_pairs||!options.contains("minimum_clearance_mm")?"unknown":too_close?"fail":"pass","exact_BRep_pair_minimum_distance",
      "Checks the recorded saved-pose pairs against the caller's gap; touching is allowed only at a supplied zero gap.",evidence));
    auto aggregate=result.at("checks");for(const auto& part:result.at("parts"))aggregate.push_back({{"status",part.at("status")}});
    result["status"]=fabrication_status(aggregate);return result;
  }catch(const Standard_Failure& error){throw occt_error(error,{{"feature_id",selected}});}
}

namespace {
constexpr double drawing_tolerance = 0.02;
constexpr std::size_t drawing_entity_limit = 10000;
constexpr std::size_t drawing_point_limit = 200000;
constexpr double tau = 2 * std::numbers::pi;

gp_Ax2 drawing_frame(const std::string& orientation, const std::string& section_axis) {
  if (orientation == "front" || (orientation == "section" && section_axis == "y"))
    return gp_Ax2(gp_Pnt(0,0,0), gp_Dir(0,-1,0), gp_Dir(1,0,0));
  if (orientation == "top" || (orientation == "section" && section_axis == "z"))
    return gp_Ax2(gp_Pnt(0,0,0), gp_Dir(0,0,1), gp_Dir(1,0,0));
  if (orientation == "right" || (orientation == "section" && section_axis == "x"))
    return gp_Ax2(gp_Pnt(0,0,0), gp_Dir(1,0,0), gp_Dir(0,1,0));
  if (orientation == "isometric")
    return gp_Ax2(gp_Pnt(0,0,0), gp_Dir(1,-1,1), gp_Dir(1,1,0));
  throw Error("invalid_argument", "Unknown drawing orientation or section axis");
}
Json balloon_anchors(const Json& requested,const std::vector<AssemblyPart>& parts,
                     const TopoDS_Shape& shape,const gp_Ax2& frame,const std::string& feature_id) {
  constexpr double tolerance=1e-5;
  if (parts.empty()) throw Error("invalid_argument","Balloons require an assembly output",{{"feature_id",feature_id}});
  if (!requested.is_array() || requested.empty() || requested.size()>64)
    throw Error("invalid_argument","A view permits 1 to 64 balloon anchors",{{"feature_id",feature_id}});
  struct Boundary { TopoDS_Shape shape; Bnd_Box box; };
  std::map<std::string,Boundary> boundaries;
  for (const auto& part:parts) {
    // Solid-point distance is zero for an arbitrary interior point. Distance
    // to a compound of boundary faces is the required surface attachment test.
    BRep_Builder builder; TopoDS_Compound boundary; builder.MakeCompound(boundary);
    ShapeMap faces; TopExp::MapShapes(part.shape,TopAbs_FACE,faces);
    for (int i=1;i<=faces.Extent();++i) builder.Add(boundary,faces(i));
    Bnd_Box box; BRepBndLib::AddOptimal(part.shape,box,false,false); box.Enlarge(tolerance);
    boundaries.emplace(part.id,Boundary{boundary,box});
  }
  Bnd_Box box; BRepBndLib::AddOptimal(shape,box,false,false);
  const auto limits=box.Get();
  const double ray_length=std::hypot(limits.Xmax-limits.Xmin,limits.Ymax-limits.Ymin,limits.Zmax-limits.Zmin)+1;
  IntCurvesFace_ShapeIntersector ray;
  ray.Load(shape,1e-7);
  Json result=Json::array(); std::set<std::string> seen;
  for (const auto& anchor:requested) {
    std::string part_id;
    try {
      fields(anchor,{"part_id","point"}); part_id=text_field(anchor,"part_id");
      if (!seen.insert(part_id).second) throw Error("invalid_argument","A view repeats a balloon part");
      const auto part=std::find_if(parts.begin(),parts.end(),[&](const auto& item){return item.id==part_id;});
      if (part==parts.end()) throw Error("selection_missing","Balloon refers to an unknown assembly part");
      const auto source=parameter_point(anchor.at("point"),Json::object());
      const auto world=source.Transformed(part->transform);
      const auto vertex=BRepBuilderAPI_MakeVertex(world).Vertex();
      BRepExtrema_DistShapeShape distance(vertex,boundaries.at(part_id).shape);
      if (!distance.IsDone() || !std::isfinite(distance.Value())) throw Error("kernel_failure","Cannot verify balloon boundary attachment");
      if (distance.Value()>tolerance) throw Error("selection_missing","Balloon anchor is not on the named part boundary",
        {{"distance_mm",distance.Value()},{"tolerance_mm",tolerance}});
      const auto attached=distance.PointOnShape2(1);
      for (int i=2;i<=distance.NbSolution();++i) if (attached.Distance(distance.PointOnShape2(i))>1e-7)
        throw Error("selection_ambiguous","Balloon anchor is equally close to distinct boundary locations");
      const auto attached_vertex=BRepBuilderAPI_MakeVertex(attached).Vertex();
      for (const auto& [other,boundary]:boundaries) {
        if (other==part_id || boundary.box.IsOut(attached)) continue;
        BRepExtrema_DistShapeShape coincidence(attached_vertex,boundary.shape);
        if (!coincidence.IsDone()) throw Error("kernel_failure","Cannot verify balloon part ownership");
        if (coincidence.Value()<=tolerance) throw Error("selection_ambiguous","Balloon anchor touches another part boundary",
          {{"other_part_id",other},{"tolerance_mm",tolerance}});
      }
      // The drawing frame's +Z points toward the orthographic camera. Any
      // boundary farther along that ray occludes the anchor, including a back
      // face of this same part. Tangential hits are conservatively occluding.
      ray.Perform(gp_Lin(attached,frame.Direction()),-tolerance,ray_length);
      if (!ray.IsDone()) throw Error("kernel_failure","Cannot verify balloon anchor visibility");
      for (int i=1;i<=ray.NbPnt();++i) if (ray.WParameter(i)>tolerance)
        throw Error("invalid_drawing","Balloon anchor is occluded in this view",{{"occlusion_distance_mm",ray.WParameter(i)}});
      const gp_Vec position(frame.Location(),attached);
      result.push_back({{"part_id",part_id},{"point",{position.Dot(gp_Vec(frame.XDirection())),position.Dot(gp_Vec(frame.YDirection()))}}});
    } catch (const Error& e) {
      auto details=e.details; details["feature_id"]=feature_id;
      if (!part_id.empty()) details["part_id"]=part_id;
      throw Error(e.code,e.what(),details);
    } catch (const Standard_Failure& e) {
      throw occt_error(e,{{"feature_id",feature_id},{"part_id",part_id}});
    }
  }
  return result;
}

double drawing_number(double value) {
  if (!std::isfinite(value) || std::abs(value) > 1e9)
    throw Error("limit_exceeded", "Projected drawing coordinates must be finite and within +/-1000000000 mm");
  return value == 0 ? 0 : value;
}
Json point2(const gp_Pnt& p) { return {drawing_number(p.X()), drawing_number(p.Y())}; }
double positive_angle(double angle) {
  angle = std::fmod(angle, tau);
  if (angle < 0) angle += tau;
  return angle >= tau - 1e-12 ? 0 : angle;
}
std::string drawing_number_key(double value) {
  // Sub-kernel-tolerance quantization removes numerically identical front/back
  // projections. It never supplies model identity or a design reference.
  return std::to_string(std::llround(drawing_number(value) * 1e7));
}
std::string drawing_point_key(const Json& p) {
  return drawing_number_key(p.at(0).get<double>()) + "," + drawing_number_key(p.at(1).get<double>());
}
std::string drawing_entity_key(const Json& entity) {
  const auto kind = entity.at("kind").get<std::string>();
  if (kind == "circle" || kind == "arc") {
    auto key = kind + ":" + drawing_point_key(entity.at("center")) + ":" + drawing_number_key(entity.at("radius").get<double>());
    if (kind == "arc") key += ":" + drawing_number_key(entity.at("start_deg").get<double>()) + ":" + drawing_number_key(entity.at("end_deg").get<double>());
    return key;
  }
  std::string forward, reverse;
  for (const auto& p : entity.at("points")) forward += drawing_point_key(p) + ";";
  for (auto i = entity.at("points").rbegin(); i != entity.at("points").rend(); ++i) reverse += drawing_point_key(*i) + ";";
  return kind + ":" + std::min(forward, reverse);
}

struct DrawingBudget {
  std::size_t entities = 0, points = 0, examined_edges = 0;
};
template<class Curve>
bool drawing_linear_poles(const Curve& curve, gp_Pnt& a, gp_Pnt& b) {
  if (curve->NbPoles() > 4096) throw Error("limit_exceeded", "Drawing spline exceeds its pole limit", {{"limit",4096}});
  a = curve->Pole(1); b = a;
  for (int i = 2; i <= curve->NbPoles(); ++i) {
    const auto pole = curve->Pole(i);
    if (pole.SquareDistance(a) > b.SquareDistance(a)) b = pole;
  }
  const double dx=b.X()-a.X(), dy=b.Y()-a.Y(), length=std::hypot(dx,dy);
  if (length <= 1e-9) return false;
  for (int i = 1; i <= curve->NbPoles(); ++i) {
    const auto pole = curve->Pole(i);
    if (std::abs(dx*(pole.Y()-a.Y())-dy*(pole.X()-a.X()))/length > 1e-9) return false;
  }
  return true;
}
bool drawing_linear_spline(const BRepAdaptor_Curve& curve, const Bnd_Box& bounds, gp_Pnt& a, gp_Pnt& b) {
  const bool linear = curve.GetType()==GeomAbs_BSplineCurve ? drawing_linear_poles(curve.BSpline(),a,b) :
                      curve.GetType()==GeomAbs_BezierCurve ? drawing_linear_poles(curve.Bezier(),a,b) : false;
  if (!linear) return false;
  const auto box = bounds.Get();
  // Projection may fold a circle into a collinear rational spline. Pole
  // collinearity proves that its entire trace is a line; curve extrema retain
  // the full trace even if its two parameter endpoints happen to coincide.
  const auto origin=a;
  const double dx=b.X()-a.X(), dy=b.Y()-a.Y();
  if (std::abs(dx)>=std::abs(dy)) {
    a.SetCoord(box.Xmin,origin.Y()+(box.Xmin-origin.X())*dy/dx,0);
    b.SetCoord(box.Xmax,origin.Y()+(box.Xmax-origin.X())*dy/dx,0);
  } else {
    a.SetCoord(origin.X()+(box.Ymin-origin.Y())*dx/dy,box.Ymin,0);
    b.SetCoord(origin.X()+(box.Ymax-origin.Y())*dx/dy,box.Ymax,0);
  }
  // AddOptimal may add the kernel confusion tolerance for splines. Prefer
  // actual endpoints when they coincide with those extrema within that margin.
  for (const double u : {curve.FirstParameter(),curve.LastParameter()}) {
    const auto p=curve.Value(u);
    if (std::hypot(p.X()-a.X(),p.Y()-a.Y())<2e-7) a=p;
    if (std::hypot(p.X()-b.X(),p.Y()-b.Y())<2e-7) b=p;
  }
  return true;
}
struct DrawingView {
  Json entities = Json::array();
  std::map<std::string,std::size_t> identities;
  Bnd_Box bounds;
  DrawingBudget& budget;
  explicit DrawingView(DrawingBudget& value) : budget(value) {}

  void append(const TopoDS_Shape& shape, bool hidden) {
    if (shape.IsNull()) return;
    ShapeMap edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    if (budget.examined_edges + edges.Extent() > 40000)
      throw Error("limit_exceeded", "Drawing projection exceeds its raw edge limit", {{"limit",40000}});
    budget.examined_edges += edges.Extent();
    for (int i = 1; i <= edges.Extent(); ++i) {
      const auto edge = TopoDS::Edge(edges(i));
      if (BRep_Tool::Degenerated(edge)) continue;
      BRepAdaptor_Curve curve(edge);
      const double first = curve.FirstParameter(), last = curve.LastParameter();
      if (!std::isfinite(first) || !std::isfinite(last) || last <= first)
        throw Error("kernel_failure", "Drawing contains an invalid curve parameter range");
      Bnd_Box curve_bounds;
      BndLib_Add3dCurve::AddOptimal(curve,first,last,0,curve_bounds);
      if (curve_bounds.IsVoid() || curve_bounds.IsOpen())
        throw Error("kernel_failure", "Drawing curve has no finite bounds");
      Json entity = {{"hidden",hidden}};
      auto start = curve.Value(first), end = curve.Value(last);
      if (curve.GetType() == GeomAbs_Line || drawing_linear_spline(curve,curve_bounds,start,end)) {
        if (std::hypot(start.X()-end.X(),start.Y()-end.Y()) <= 1e-9) continue;
        entity["kind"] = "line";
        entity["points"] = {point2(start),point2(end)};
      } else if (curve.GetType() == GeomAbs_Circle) {
        const auto circle = curve.Circle();
        const auto center = circle.Location();
        // HLR has already projected its curves to XY. Section curves were
        // transformed to the section frame, so their circles also lie in XY.
        if (std::abs(circle.Axis().Direction().Z()) < 1-1e-8)
          throw Error("kernel_failure", "Projected circle is not in the drawing plane");
        entity["center"] = point2(center);
        entity["radius"] = drawing_number(circle.Radius());
        if (last-first >= tau-1e-9) {
          entity["kind"] = "circle";
        } else {
          const auto ccw_start = circle.Axis().Direction().Z() > 0 ? start : end;
          const auto angle = positive_angle(std::atan2(ccw_start.Y()-center.Y(),ccw_start.X()-center.X()));
          entity["kind"] = "arc";
          entity["start_deg"] = angle * 180 / std::numbers::pi;
          entity["end_deg"] = (angle + last-first) * 180 / std::numbers::pi;
        }
      } else {
        // UniformDeflection's controlled mode checks actual chordal error. Its
        // C2 precondition is met separately on every continuity interval;
        // endpoints preserve corners and periodic closures. Half the public
        // tolerance leaves room for the kernel's projection/intersection error.
        const int interval_count = curve.NbIntervals(GeomAbs_C2);
        if (interval_count < 1 || interval_count > 4096)
          throw Error("limit_exceeded", "Drawing curve exceeds its continuity interval limit", {{"limit",4096}});
        NCollection_Array1<double> intervals(1,interval_count+1);
        curve.Intervals(intervals,GeomAbs_C2);
        Json points = Json::array();
        for (int interval = 1; interval <= interval_count; ++interval) {
          GCPnts_UniformDeflection sampling(curve, drawing_tolerance/2, intervals(interval), intervals(interval+1), true);
          if (!sampling.IsDone() || sampling.NbPoints() < 2)
            throw Error("kernel_failure", "Drawing curve approximation failed");
          const auto added = static_cast<std::size_t>(sampling.NbPoints() - (points.empty() ? 0 : 1));
          if (budget.points + points.size() + added > drawing_point_limit)
            throw Error("limit_exceeded", "Drawing exceeds its point limit", {{"limit",drawing_point_limit}});
          for (int p = points.empty() ? 1 : 2; p <= sampling.NbPoints(); ++p) points.push_back(point2(sampling.Value(p)));
        }
        entity["kind"] = "polyline";
        entity["points"] = std::move(points);
      }
      const auto key = drawing_entity_key(entity);
      if (const auto found = identities.find(key); found != identities.end()) {
        if (!hidden) entities.at(found->second)["hidden"] = false;
        continue;
      }
      const auto point_count = entity.contains("points") ? entity.at("points").size() : 1;
      if (budget.entities >= drawing_entity_limit || budget.points + point_count > drawing_point_limit)
        throw Error("limit_exceeded", "Drawing exceeds its entity or point limit", {{"entity_limit",drawing_entity_limit},{"point_limit",drawing_point_limit}});
      ++budget.entities; budget.points += point_count;
      // This bounds the underlying analytic curve, including arc extrema and
      // spline extrema between sampled polyline vertices, not just its endpoints.
      const auto b = curve_bounds.Get();
      bounds.Add(gp_Pnt(drawing_number(b.Xmin),drawing_number(b.Ymin),0));
      bounds.Add(gp_Pnt(drawing_number(b.Xmax),drawing_number(b.Ymax),0));
      identities.emplace(key,entities.size());
      entities.push_back(std::move(entity));
    }
  }
  void remove_covered_hidden_lines() {
    struct Segment { double x,y,ex,ey; };
    std::vector<Segment> visible;
    for (const auto& entity : entities) if (entity.at("kind")=="line" && entity.at("hidden")==false) {
      const auto& p=entity.at("points"); visible.push_back({p[0][0],p[0][1],p[1][0],p[1][1]});
    }
    Json cleaned=Json::array();
    for (auto& entity : entities) {
      if (entity.at("kind")!="line" || entity.at("hidden")==false) {cleaned.push_back(std::move(entity)); continue;}
      const auto& p=entity.at("points");
      const double x=p[0][0],y=p[0][1],dx=p[1][0].get<double>()-x,dy=p[1][1].get<double>()-y;
      const double length=std::hypot(dx,dy),ux=dx/length,uy=dy/length;
      std::vector<std::pair<double,double>> covered;
      for (const auto& v : visible) {
        if (std::abs(ux*(v.y-y)-uy*(v.x-x))>2e-7 || std::abs(ux*(v.ey-y)-uy*(v.ex-x))>2e-7) continue;
        const double a=ux*(v.x-x)+uy*(v.y-y),b=ux*(v.ex-x)+uy*(v.ey-y);
        const double lo=std::max(0.0,std::min(a,b)),hi=std::min(length,std::max(a,b));
        if (hi>lo) covered.emplace_back(lo,hi);
      }
      if (covered.empty()) {cleaned.push_back(std::move(entity)); continue;}
      std::sort(covered.begin(),covered.end());
      double cursor=0;
      const auto fragment=[&](double a,double b) {
        if (b-a<=2e-7) return;
        if (++budget.entities>drawing_entity_limit || (budget.points+=2)>drawing_point_limit)
          throw Error("limit_exceeded", "Drawing line clipping exceeds its entity or point limit", {{"entity_limit",drawing_entity_limit},{"point_limit",drawing_point_limit}});
        cleaned.push_back({{"kind","line"},{"hidden",true},{"points",{{x+ux*a,y+uy*a},{x+ux*b,y+uy*b}}}});
      };
      for (const auto& [lo,hi] : covered) { if (lo>cursor) fragment(cursor,lo); cursor=std::max(cursor,hi); }
      if (cursor<length) fragment(cursor,length);
    }
    entities=std::move(cleaned);
  }
};
}

Json BuiltModel::drawing(const Json& spec, Json* exact_projections, const std::string& feature_id) const {
  fields(spec,{"views"},{"hidden_lines"});
  if (!spec.at("views").is_array() || spec.at("views").empty() || spec.at("views").size()>6)
    throw Error("invalid_argument", "A drawing requires 1 to 6 views");
  if (spec.contains("hidden_lines") && !spec.at("hidden_lines").is_boolean())
    throw Error("invalid_argument", "hidden_lines must be boolean");
  const bool hidden = spec.value("hidden_lines",true);
  const auto selected=feature_id.empty()?impl_->output:feature_id;
  const auto& selected_geometry=impl_->feature(selected);
  topology_limit(selected_geometry,QueryLimits{});
  DrawingBudget budget;
  std::size_t projection_bytes=0;
  if (exact_projections) *exact_projections=Json::array();
  Json result = {{"views",Json::array()},{"tolerance_mm",drawing_tolerance},{"view_budgets",Json::array()}};
  std::set<std::string> ids;
  std::size_t anchor_count=0;
  for (const auto& view : spec.at("views")) {
    const DrawingBudget usage_before=budget;
    fields(view,{"id","orientation"},{"section","hatch","explode","balloon_anchors"});
    const auto id = text_field(view,"id"); identifier(id);
    if (!ids.insert(id).second) throw Error("invalid_argument", "Drawing view IDs must be unique", {{"view_id",id}});
    try {
      const auto orientation = text_field(view,"orientation");
      TopoDS_Shape view_shape=selected_geometry.shape;
      auto view_parts=selected_geometry.parts;
      if (view.contains("explode")) {
        const auto& geometry=selected_geometry;
        if (geometry.parts.empty()) throw Error("invalid_argument","Exploded views require an assembly output");
        const auto& explode=view.at("explode");
        if (!explode.is_array() || explode.empty() || explode.size()>assembly_leaf_limit)
          throw Error("invalid_argument","Exploded views require 1 to 1024 leaf occurrence translations");
        std::map<std::string,gp_Trsf> translations;
        for (const auto& item:explode) {
          fields(item,{"part_id","translation"});
          const auto part_id=text_field(item,"part_id");
          if (std::none_of(geometry.parts.begin(),geometry.parts.end(),[&](const auto& part){return part.id==part_id;}))
            throw Error("invalid_argument","Exploded view refers to an unknown assembly part",{{"part_id",part_id}});
          const auto delta=vector3(item.at("translation"),Json::object());
          gp_Trsf translation; translation.SetTranslation(gp_Vec(delta[0],delta[1],delta[2]));
          if (!translations.emplace(part_id,translation).second)
            throw Error("invalid_argument","Exploded view repeats a part",{{"part_id",part_id}});
        }
        BRep_Builder builder; TopoDS_Compound compound; builder.MakeCompound(compound);
        for (auto& part:view_parts) {
          const auto found=translations.find(part.id);
          const gp_Trsf translation=found==translations.end() ? gp_Trsf{} : found->second;
          // Translations are world coordinates applied after mate placement.
          // Copy every part so projections never mutate the assembled result.
          BRepBuilderAPI_Transform copy(part.shape,translation,true);
          if (!copy.IsDone()) throw Error("kernel_failure","Exploded part placement failed",{{"part_id",part.id}});
          builder.Add(compound,copy.Shape());
          part.shape=copy.Shape(); part.transform=translation*part.transform;
        }
        view_shape=compound;
      }
      if (view.contains("hatch") && (orientation!="section" || !view.at("hatch").is_boolean()))
        throw Error("invalid_argument", "Only section views accept a boolean hatch flag");
      std::string axis;
      double offset = 0;
      if (orientation == "section") {
        if (!view.contains("section")) throw Error("invalid_argument", "Section view requires a section plane");
        fields(view.at("section"),{"axis","offset"});
        axis = text_field(view.at("section"),"axis"); offset = number(view.at("section").at("offset"));
      } else if (view.contains("section")) throw Error("invalid_argument", "Only section views accept a section plane");
      const auto frame = drawing_frame(orientation,axis);
      Json projected_anchors;
      if (view.contains("balloon_anchors")) {
        if (orientation=="section") throw Error("invalid_argument","Section balloons are not supported",{{"feature_id",selected}});
        anchor_count+=view.at("balloon_anchors").size();
        if (anchor_count>64) throw Error("limit_exceeded","A drawing permits at most 64 balloon anchors",{{"feature_id",selected}});
        projected_anchors=balloon_anchors(view.at("balloon_anchors"),view_parts,view_shape,frame,selected);
      }
      DrawingView projected(budget);
      Json regions=Json::array();
      if (orientation == "section") {
        gp_Pnt location(0,0,0);
        if (axis == "x") location.SetX(offset);
        else if (axis == "y") location.SetY(offset);
        else location.SetZ(offset);
        const auto plane = BRepBuilderAPI_MakeFace(gp_Pln(location,frame.Direction())).Face();
        NCollection_List<TopoDS_Shape> arguments, tools;
        arguments.Append(view_shape); tools.Append(plane);
        BRepAlgoAPI_Section section;
        section.SetArguments(arguments); section.SetTools(tools);
        section.SetNonDestructive(true); section.SetRunParallel(false);
        section.Approximation(true); section.Build();
        if (!section.IsDone() || section.HasErrors()) throw Error("kernel_failure", "Plane section failed");
        gp_Trsf transform; transform.SetTransformation(gp_Ax3(frame));
        BRepBuilderAPI_Transform flatten(section.Shape(),transform,true);
        projected.append(flatten.Shape(),false);
        if (view.value("hatch",true) && !projected.entities.empty()) {
          // Intersect the solid with the plane to retain material faces and
          // their inner wires. Raw section edges alone cannot distinguish
          // cavities, touching solids or overlapping regions reliably.
          // Pattern compounds may contain interfering solids. Boolean common
          // on that entire compound is not a reliable material classification;
          // intersect each solid and let the renderer union its hatch intervals.
          ShapeMap solids; TopExp::MapShapes(view_shape,TopAbs_SOLID,solids);
          for (int s=1;s<=solids.Extent();++s) {
            NCollection_List<TopoDS_Shape> solid_argument; solid_argument.Append(solids(s));
            BRepAlgoAPI_Common material;
            material.SetArguments(solid_argument); material.SetTools(tools);
            material.SetNonDestructive(true); material.SetRunParallel(false); material.Build();
            if (!material.IsDone() || material.HasErrors())
              throw Error("kernel_failure", "Section material intersection failed");
            BRepBuilderAPI_Transform flat_material(material.Shape(),transform,true);
            ShapeMap faces; TopExp::MapShapes(flat_material.Shape(),TopAbs_FACE,faces);
            for (int f=1;f<=faces.Extent();++f) {
              if (!BRepCheck_Analyzer(faces(f)).IsValid()) throw Error("kernel_failure", "Section material face is invalid");
              DrawingView region(budget); region.append(faces(f),false);
              regions.push_back(std::move(region.entities));
            }
          }
          if (regions.empty()) throw Error("invalid_drawing", "Section has no material area to hatch; disable hatching for a tangent profile");
        }
      } else {
        occ::handle<HLRBRep_Algo> algorithm = new HLRBRep_Algo;
        algorithm->Add(view_shape,0);
        algorithm->Projector(HLRAlgo_Projector(frame));
        algorithm->Update(); algorithm->Hide();
        HLRBRep_HLRToShape extraction(algorithm);
        // Sharp boundaries, tangent transitions and silhouettes form the view.
        // C2 sewn seams and arbitrary surface isoparameters are not part edges.
        Json exact=Json::array();
        const auto append=[&](const TopoDS_Shape& curves,bool concealed) {
          if (exact_projections) {
            SnapshotBuffer buffer(32*1024*1024-projection_bytes); std::ostream stream(&buffer);
            if (!curves.IsNull()) BRepTools::Write(curves,stream,false,false,TopTools_FormatVersion_CURRENT);
            if (!stream) throw Error("limit_exceeded","Exact projection diagnostics exceed cache budget");
            projection_bytes+=buffer.bytes.size();
            exact.push_back({{"hidden",concealed},{"brep",std::move(buffer.bytes)}});
          }
          projected.append(curves,concealed);
        };
        append(extraction.VCompound(),false);
        append(extraction.Rg1LineVCompound(),false);
        append(extraction.OutLineVCompound(),false);
        if (hidden) {
          append(extraction.HCompound(),true);
          append(extraction.Rg1LineHCompound(),true);
          append(extraction.OutLineHCompound(),true);
        }
        if (exact_projections) exact_projections->push_back({{"id",id},{"curves",std::move(exact)}});
      }
      projected.remove_covered_hidden_lines();
      if (projected.entities.empty()) throw Error(orientation == "section" ? "empty_section" : "empty_projection", "Drawing view contains no curves");
      const auto b = projected.bounds.Get();
      Json output={{"id",id},{"orientation",orientation},{"bounds_mm",{b.Xmin,b.Ymin,b.Xmax,b.Ymax}},{"entities",std::move(projected.entities)}};
      if (view.contains("explode")) output["explode"]=view.at("explode");
      if (!projected_anchors.is_null()) output["balloon_anchors"]=std::move(projected_anchors);
      if (orientation=="section" && view.value("hatch",true)) output["section_regions"]=std::move(regions);
      result["views"].push_back(std::move(output));
      // What this view cost, so a cached view still counts toward the drawing-wide limits.
      result["view_budgets"].push_back({{"entities",budget.entities-usage_before.entities},{"points",budget.points-usage_before.points},
        {"examined_edges",budget.examined_edges-usage_before.examined_edges}});
    } catch (const Error& e) {
      auto details = e.details; details["view_id"] = id;
      throw Error(e.code,e.what(),details);
    } catch (const Standard_Failure& e) { throw occt_error(e,{{"view_id",id}}); }
  }
  return result;
}

void check_drawing_totals(const Json& requested_views, const Json& view_budgets, const std::string& feature_id) {
  std::size_t anchors=0, entities=0, points=0, examined_edges=0;
  for (const auto& view : requested_views)
    if (view.contains("balloon_anchors")) anchors+=view.at("balloon_anchors").size();
  if (anchors>64) throw Error("limit_exceeded","A drawing permits at most 64 balloon anchors",{{"feature_id",feature_id}});
  for (const auto& usage : view_budgets) {
    entities+=usage.at("entities").get<std::size_t>(); points+=usage.at("points").get<std::size_t>();
    examined_edges+=usage.at("examined_edges").get<std::size_t>();
  }
  if (examined_edges>40000) throw Error("limit_exceeded","Drawing projection exceeds its raw edge limit",{{"limit",40000}});
  if (entities>drawing_entity_limit || points>drawing_point_limit)
    throw Error("limit_exceeded","Drawing exceeds its entity or point limit",{{"entity_limit",drawing_entity_limit},{"point_limit",drawing_point_limit}});
}

Json BuiltModel::robot_frames(const Json& model,const std::string& feature_id) const {
  const auto id=feature_id.empty()?impl_->output:feature_id;
  const auto& geometry=impl_->feature(id);
  if(geometry.parts.empty()) throw Error("invalid_argument","Robot export requires an assembly",{{"feature_id",id}});
  std::map<std::string,const Json*> definitions;
  for(const auto& feature:model.at("features")) definitions.emplace(text_field(feature,"id"),&feature);
  const auto& parameters=model.at("parameters");
  std::map<std::string,gp_Trsf> world;
  auto result=composed_robot_motion(model,id);
  result["feature_id"]=id;result["links"]=Json::array();result["joints"]=Json::array();result["assemblies"]=Json::array();
  const auto qualify=[](const std::string& path,const std::string& name){return path.empty()?name:path+"/"+name;};
  // Each assembly's source frame is rigidly attached to its grounded roots.
  // Use the lexically first root's ultimate leaf as its physical anchor. Other
  // roots attach rigidly to that leaf. No fictitious assembly body or mass is
  // introduced, and every moving child still has exactly one incoming joint.
  std::function<std::string(const std::string&,const std::string&)> anchor=[&](const std::string& source,const std::string& path) {
    const auto& feature=*definitions.at(source);
    if (feature.at("type")!="assembly") return path;
    std::set<std::string> children;
    for (const auto& mate:feature.value("mates",Json::array())) children.insert(text_field(mate,"child"));
    const Json* root=nullptr;
    for (const auto& part:feature.at("parts")) if (!children.contains(text_field(part,"id")) && (!root || part.at("id")<root->at("id"))) root=&part;
    if (!root) throw Error("invalid_model","Assembly has no grounded root",{{"feature_id",source}});
    return anchor(text_field(*root,"input"),qualify(path,text_field(*root,"id")));
  };
  struct Joint {std::string parent,child,mate,type;};
  std::vector<Joint> pending;
  try {
    for (const auto& part:geometry.parts) world[part.id]=part.transform;
    std::function<void(const std::string&,const std::string&,const gp_Trsf&)> visit;
    visit=[&](const std::string& source,const std::string& path,const gp_Trsf& parent_transform) {
      const auto& feature=*definitions.at(source);const auto& local=impl_->feature(source);
      const auto grounded=anchor(source,path);
      result["assemblies"].push_back({{"assembly_id",source},{"path",path},{"anchor_part_id",grounded},{"world",matrix(parent_transform)}});
      std::map<std::string,const Json*> incoming;
      std::map<std::string,std::string> representatives;
      if (feature.contains("mates")) for (const auto& mate:feature.at("mates")) incoming.emplace(text_field(mate,"child"),&mate);
      for (const auto& part:feature.at("parts")) representatives[text_field(part,"id")]=anchor(text_field(part,"input"),qualify(path,text_field(part,"id")));
      for (const auto& part:feature.at("parts")) {
        const auto pid=text_field(part,"id"),input=text_field(part,"input"),occurrence=qualify(path,pid),leaf=representatives.at(pid);
        const auto placement=parent_transform*local.placements.at(pid);
        if (incoming.contains(pid)) {
          const auto& mate=*incoming.at(pid);
          world[leaf]=placement*local_frame(mate.at("child_frame"),parameters);
          pending.push_back({representatives.at(text_field(mate,"parent")),leaf,qualify(path,text_field(mate,"id")),text_field(mate,"type")});
        } else if (path.empty() || leaf!=grounded) pending.push_back({path.empty()?std::string():grounded,leaf,"","rigid"});
        if (definitions.at(input)->at("type")=="assembly") visit(input,occurrence,placement);
      }
    };
    visit(id,"",gp_Trsf{});
    for (const auto& part:geometry.parts) result["links"].push_back({{"name",robot_name("part",part.id)},{"part_id",part.id},{"input",part.input},
      {"world",matrix(world.at(part.id))},{"mesh_origin",matrix(world.at(part.id).Inverted()*part.transform)}});
    std::set<std::string> attached;
    for (const auto& spec:pending) {
      if (!attached.insert(spec.child).second) throw Error("kernel_failure","Composed robot link has multiple parents",{{"part_id",spec.child}});
      const auto parent=spec.parent.empty()?std::string("world"):robot_name("part",spec.parent),child=robot_name("part",spec.child);
      const auto parent_world=spec.parent.empty()?gp_Trsf{}:world.at(spec.parent);
      const auto child_world=world.at(spec.child),origin=parent_world.Inverted()*child_world;
      if (spec.mate.empty()) {
        result["joints"].push_back({{"name",robot_name("root",spec.child)},{"type","fixed"},{"parent",parent},{"child",child},{"origin",matrix(origin)}});
        continue;
      }
      const auto& mid=spec.mate;const auto& type=spec.type;
      auto joint=[&](const std::string& suffix,const std::string& kind,const std::string& p,const std::string& c,const gp_Trsf& placement,const std::string& coordinate) {
        Json item={{"name",robot_name("mate",mid)+suffix},{"type",kind},{"parent",p},{"child",c},{"origin",matrix(placement)},{"mate_id",mid}};
        if(!coordinate.empty()) item["coordinate"]=coordinate;
        result["joints"].push_back(std::move(item));
      };
      if(type=="cylindrical") {
        double travel=0;
        for(const auto& dof:result.at("motion").at("dofs")) if(dof.at("mate_id")==mid && dof.at("coordinate")=="travel_mm") travel=dof.at("value");
        gp_Trsf shift;shift.SetTranslation(gp_Vec(0,0,travel));
        const auto carrier=child_world*shift.Inverted();const auto name=robot_name("carrier",mid);
        result["links"].push_back({{"name",name},{"carrier_for",mid},{"world",matrix(carrier)}});
        joint("_angle","revolute",parent,name,parent_world.Inverted()*carrier,"angle_deg");
        joint("_travel","prismatic",name,child,shift,"travel_mm");
      } else joint(type=="revolute"?"_angle":type=="slider"?"_travel":"_fixed",type=="slider"?"prismatic":type=="rigid"?"fixed":"revolute",
        parent,child,origin,type=="revolute"?"angle_deg":type=="slider"?"travel_mm":"");
    }
    if (attached.size()!=geometry.parts.size()) throw Error("kernel_failure","Composed robot does not cover every physical leaf");
    if (result.at("links").size()>robot_link_limit) throw Error("limit_exceeded","Robot export permits at most 8192 links");
    return result;
  } catch(const Standard_Failure& e) {throw occt_error(e,{{"feature_id",id}});}
}

void BuiltModel::export_file(const std::filesystem::path& path, const std::string& format,const std::string& feature_id) const {
  try {
    // STEP accepts UTF-8 paths. STL's filename overload opens a narrow standard
    // stream, so use its stream overload with a native filesystem path below.
    const auto utf8 = path.u8string();
    const std::string filename(utf8.begin(), utf8.end());
    if (format == "step") {
      STEPControl_Writer writer;
      // Set the pinned OCCT ToSTEP representation operations explicitly: its
      // shared transfer actor can otherwise retain another writer's flags.
      ShapeProcess::OperationsFlags export_operations;
      export_operations.set(ShapeProcess::SplitCommonVertex);
      export_operations.set(ShapeProcess::DirectFaces);
      writer.SetShapeProcessFlags(export_operations);
      if (writer.Transfer(impl_->feature(feature_id).shape, STEPControl_AsIs) != IFSelect_RetDone || writer.Write(filename.c_str()) != IFSelect_RetDone)
        throw Error("export_failed", "STEP writer failed");
    } else if (format == "stl") {
      const auto selected=feature_id.empty()?impl_->output:feature_id;
      const auto is_surface=[&]() {for(const auto& declaration:impl_->model.at("features"))if(declaration.at("id")==selected)return is_surface_feature_type(text_field(declaration,"type"));return false;}();
      const auto meshes=is_surface?Json::array({mesh(feature_id)}):print_meshes(feature_id);std::uint32_t triangles=0;
      for(const auto& mesh:meshes)triangles+=static_cast<std::uint32_t>(mesh.at("triangles").size());
      std::ofstream output(path,std::ios::binary|std::ios::trunc);
      if(!output)throw Error("export_failed","Cannot open STL output");
      const std::string header=is_surface?"Agent CAD surface mesh (may be open)":"Agent CAD closed solid mesh";std::array<char,80> padded{};std::copy(header.begin(),header.end(),padded.begin());output.write(padded.data(),padded.size());
      const auto u32=[&](std::uint32_t value){for(int k=0;k<4;++k)output.put(static_cast<char>((value>>(8*k))&255));};
      const auto f32=[&](double value){const float f=static_cast<float>(value);if(!std::isfinite(f))throw Error("export_failed","STL coordinate exceeds float representation");u32(std::bit_cast<std::uint32_t>(f));};
      u32(triangles);
      for(const auto& mesh:meshes)for(const auto& t:mesh.at("triangles")) {
        std::array<gp_Pnt,3> p;for(int k=0;k<3;++k){const auto v=mesh.at("positions").at(t[k].get<std::size_t>()).get<std::array<double,3>>();p[k]=gp_Pnt(v[0],v[1],v[2]);}
        gp_Vec normal=gp_Vec(p[0],p[1]).Crossed(gp_Vec(p[0],p[2]));if(normal.Magnitude()>0)normal.Normalize();
        for(int k=1;k<=3;++k)f32(normal.Coord(k));for(const auto& vertex:p)for(int k=1;k<=3;++k)f32(vertex.Coord(k));output.put(0);output.put(0);
      }
      output.flush();if(!output)throw Error("export_failed","STL stream write failed");output.close();if(output.fail())throw Error("export_failed","STL stream close failed");
    } else throw Error("invalid_argument", "Export format must be step or stl");
  } catch (const Standard_Failure& e) {
    throw occt_error(e, Json::object(), "export_failed");
  }
}
}
