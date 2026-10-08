#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
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
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BezierCurve.hxx>
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
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Shell.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GCPnts_UniformDeflection.hxx>
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
#include <TopoDS_Iterator.hxx>
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
  Json motion;
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
void check_shape(const TopoDS_Shape& shape) {
  if (shape.IsNull() || !BRepCheck_Analyzer(shape).IsValid())
    throw Error("invalid_shape", "Feature did not produce valid solid geometry");
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
void topology_limit(const FeatureGeometry& feature, const QueryLimits& limits) {
  const auto requested = static_cast<std::size_t>(feature.faces.Extent()) + feature.edges.Extent();
  const auto maximum = std::min<std::size_t>(limits.topology_entities, 10000);
  if (requested > maximum) throw Error("limit_exceeded", "Topology enumeration exceeds its entity limit", {{"limit", maximum}, {"requested", requested}});
}
template<class Operation>
void record_history(Operation& operation, const FeatureGeometry& source, const std::string& source_id,
                    const FeatureGeometry& target, Json& history, bool& truncated,
                    BRepBuilderAPI_Copy* copy = nullptr, int instance_index = -1) {
  if (truncated) return;
  const auto append = [&](Json item) {
    if (history.size() >= 10000) { truncated=true; return false; }
    history.push_back(std::move(item));
    return true;
  };
  for (const auto& [kind, entities] : std::initializer_list<std::pair<std::string,const ShapeMap*>>{{"face",&source.faces},{"edge",&source.edges}}) {
    for (int i=1; i<=entities->Extent(); ++i) {
      const auto original=(*entities)(i);
      const auto entity=copy ? copy->ModifiedShape(original) : original;
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
FeatureGeometry build_assembly(const Json& feature,const Json& parameters,
                               const std::map<std::string,FeatureGeometry>& sources,
                               Json& history,bool& history_truncated) {
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
  BRep_Builder builder; TopoDS_Compound compound; builder.MakeCompound(compound);
  std::vector<AssemblyPart> resolved;
  std::vector<std::unique_ptr<BRepBuilderAPI_Transform>> operations;
  for (const auto& part:parts) {
    const auto id=text_field(part,"id"),input=text_field(part,"input");
    try {
      // copyGeom=true keeps repeated, coincident parts independent, including
      // identity placements. No Boolean operation changes their solid volumes.
      auto operation=std::make_unique<BRepBuilderAPI_Transform>(sources.at(input).shape,transforms.at(id),true);
      if (!operation->IsDone()) throw Error("kernel_failure","Assembly part placement failed",{{"part_id",id}});
      check_shape(operation->Shape());
      resolved.push_back({id,input,transforms.at(id),operation->Shape()});
      builder.Add(compound,operation->Shape());
      operations.push_back(std::move(operation));
    } catch (const Error& e) {
      auto details=e.details; details["part_id"]=id; throw Error(e.code,e.what(),details);
    } catch (const Standard_Failure& e) { throw occt_error(e,{{"part_id",id}}); }
  }
  check_shape(compound);
  FeatureGeometry result(compound); result.parts=std::move(resolved);
  if (!motion.at("dofs").empty()) result.motion=motion;
  result.face_parts.resize(result.faces.Extent()+1); result.edge_parts.resize(result.edges.Extent()+1);
  for (std::size_t p=0;p<result.parts.size();++p) {
    const auto& part=result.parts[p];
    const FeatureGeometry geometry(part.shape);
    for (const auto& [source,target,owners]:std::initializer_list<std::tuple<const ShapeMap*,const ShapeMap*,std::vector<std::string>*>>{
        {&geometry.faces,&result.faces,&result.face_parts},{&geometry.edges,&result.edges,&result.edge_parts}}) {
      for (int i=1;i<=source->Extent();++i) {
        const int index=target->FindIndex((*source)(i));
        if (!index || !(*owners)[index].empty()) throw Error("kernel_failure","Assembly topology ownership is ambiguous",{{"part_id",part.id}});
        (*owners)[index]=part.id;
      }
    }
    const auto start=history.size();
    record_history(*operations[p],sources.at(part.input),part.input,result,history,history_truncated);
    for (std::size_t i=start;i<history.size();++i) history[i]["part_id"]=part.id;
  }
  for (const auto* owners:{&result.face_parts,&result.edge_parts})
    for (std::size_t i=1;i<owners->size();++i) if ((*owners)[i].empty()) throw Error("kernel_failure","Assembly topology has no owning part");
  if (feature.contains("mates")) for (const auto& mate:feature.at("mates"))
    result.mates.push_back({{"id",mate.at("id")},{"type",mate.at("type")},{"parent",mate.at("parent")},{"child",mate.at("child")}});
  return result;
}
Json feature_provenance(const Json& feature,const Json& history,bool history_truncated) {
  Json dependencies=Json::array();
  for (const auto* key:{"input","left","right"}) if (feature.contains(key)) dependencies.push_back(feature.at(key));
  if (feature.contains("sections")) dependencies=feature.at("sections");
  if (feature.at("type")=="assembly") {
    std::set<std::string> added;
    for (const auto& part:feature.at("parts")) if (added.insert(text_field(part,"input")).second) dependencies.push_back(part.at("input"));
  }
  Json result={{"feature_id",feature.at("id")},{"feature_type",feature.at("type")},{"dependencies",dependencies},
    {"reference_policy","geometric_replay"},{"history_lifetime","evaluation"},{"history",history},{"history_truncated",history_truncated}};
  if (feature.at("type")=="import_step") result["content_sha256"]=feature.at("sha256");
  return result;
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
            curve=new Geom_BezierCurve(poles);
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

BuiltModel::BuiltModel(const Json& model) : impl_(std::make_unique<Impl>()) {
  validate_model(model);
  std::map<std::string, TopoDS_Shape> shapes;
  std::map<std::string, gp_Ax2> planes;
  const auto& parameters = model.at("parameters");
  for (const auto& feature : model.at("features")) {
    const auto id = text_field(feature, "id");
    const auto type = text_field(feature, "type");
    try {
      TopoDS_Shape shape;
      std::unique_ptr<FeatureGeometry> assembly;
      Json history=Json::array();
      bool history_truncated=false;
      const auto position = feature.contains("origin") ? vector3(feature.at("origin"), parameters) : std::array<double,3>{0,0,0};
      const gp_Pnt origin(position[0], position[1], position[2]);
      if (type == "box") {
        const auto size = vector3(feature.at("size"), parameters);
        shape = BRepPrimAPI_MakeBox(origin, size[0], size[1], size[2]).Shape();
      } else if (type == "cylinder") {
        shape = BRepPrimAPI_MakeCylinder(gp_Ax2(origin, gp_Dir(0,0,1)), scalar(feature.at("radius"), parameters), scalar(feature.at("height"), parameters)).Shape();
      } else if (type == "external_thread") {
        shape=external_thread(scalar(feature.at("major_diameter"),parameters),scalar(feature.at("pitch"),parameters),scalar(feature.at("length"),parameters),origin,feature.value("handedness",std::string("right"))=="left");
      } else if (type == "sketch") {
        const auto plane = parameter_plane(feature.at("workplane"),parameters);
        planes.emplace(id,plane);
        shape = sketch_face(feature.at("profile"),parameters,plane);
      } else if (type == "extrude") {
        const auto input = text_field(feature,"input");
        BRepPrimAPI_MakePrism operation(shapes.at(input),gp_Vec(planes.at(input).Direction())*scalar(feature.at("distance"),parameters),true);
        if (!operation.IsDone()) throw Error("kernel_failure","Extrusion failed");
        shape = operation.Shape();
        record_history(operation,impl_->features.at(input),input,shape,history,history_truncated);
      } else if (type == "revolve") {
        const auto& axis = feature.at("axis");
        BRepPrimAPI_MakeRevol operation(shapes.at(text_field(feature,"input")),gp_Ax1(parameter_point(axis.at("origin"),parameters),parameter_direction(axis.at("direction"),parameters)),scalar(feature.at("angle_deg"),parameters,"deg")*std::numbers::pi/180,true);
        if (!operation.IsDone()) throw Error("kernel_failure","Revolve failed");
        shape = operation.Shape();
        const auto input=text_field(feature,"input");
        record_history(operation,impl_->features.at(input),input,shape,history,history_truncated);
      } else if (type == "loft") {
        BRepOffsetAPI_ThruSections operation(true,feature.value("ruled",false));
        operation.SetMutableInput(false);
        for (const auto& section : feature.at("sections")) {
          const auto& source=shapes.at(section.get<std::string>());
          if (count(source,TopAbs_WIRE)!=1) throw Error("invalid_model","Loft sections must have one boundary; loft outer and inner profiles separately and subtract explicitly",{{"source_feature_id",section}});
          operation.AddWire(BRepTools::OuterWire(TopoDS::Face(source)));
        }
        operation.CheckCompatibility(true);
        operation.Build();
        if (!operation.IsDone()) throw Error("kernel_failure","Loft failed");
        shape = operation.Shape();
        const FeatureGeometry target(shape);
        for (const auto& section : feature.at("sections")) {
          const auto input=section.get<std::string>();
          record_history(operation,impl_->features.at(input),input,target,history,history_truncated);
        }
      } else if (type == "sweep") {
        const auto input = text_field(feature,"input");
        const auto& path = feature.at("path");
        Json segments=path.is_object()?path.at("segments"):Json::array();
        if (path.is_array()) for (std::size_t i=1;i<path.size();++i)
          segments.push_back({{"type","line"},{"start",path[i-1]},{"end",path[i]}});
        const auto spine=curve_wire(segments,parameters);
        if (spine.start.Distance(planes.at(input).Location()) > 1e-7 || std::abs(gp_Dir(spine.tangent).Dot(planes.at(input).Direction())) < 1-1e-9) {
          throw Error("invalid_model","Sweep path must start at the sketch origin with a tangent perpendicular to its plane");
        }
        BRepOffsetAPI_MakePipe operation(spine.wire,shapes.at(input));
        operation.Build();
        if (!operation.IsDone()) throw Error("kernel_failure","Sweep failed");
        shape=operation.Shape();
        record_history(operation,impl_->features.at(input),input,shape,history,history_truncated);
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
      } else if (type == "pattern" || type == "circular_pattern") {
        BRep_Builder builder;
        TopoDS_Compound compound; builder.MakeCompound(compound);
        const auto delta=type=="pattern"?vector3(feature.at("step"),parameters):std::array<double,3>{0,0,0};
        const auto input=text_field(feature,"input");
        const auto copies=feature.at("count").get<int>();
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
      } else if (type == "import_step") {
        STEPControl_Reader reader;
        // Import transfers the supplied geometry; optional shape-healing passes
        // must not silently change its topology or orientation before validation.
        reader.SetShapeProcessFlags(ShapeProcess::OperationsFlags{});
        std::istringstream stream(text_field(feature,"content"));
        if (reader.ReadStream("embedded.step",stream) != IFSelect_RetDone)
          throw Error("kernel_failure","Embedded STEP content could not be imported");
        reader.SetSystemLengthUnit(1.0); // OCCT length-unit scale 1 is millimeters.
        // TransferRoots skips roots that fail and returns only the successful
        // count. Anything short of every root is a lost part, never an import.
        const int roots=reader.NbRootsForTransfer();
        const int transferred=reader.TransferRoots();
        const Json counts={{"transferred_roots",transferred},{"total_roots",roots}};
        if (roots <= 0 || transferred <= 0)
          throw Error("kernel_failure","Embedded STEP content could not be imported",counts);
        if (transferred != roots)
          throw Error("kernel_failure","Embedded STEP import transferred only "+std::to_string(transferred)+" of "+
            std::to_string(roots)+" root entities; partial imports are rejected",counts);
        shape=reader.OneShape();
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
        else {BRepFilletAPI_MakeChamfer operation(copy.Shape());finish(operation,"distance");}
      }
      if (type != "sketch") check_shape(shape);
      shapes.emplace(id, shape);
      impl_->features.emplace(id,assembly ? std::move(*assembly) : FeatureGeometry(shape));
      impl_->features.at(id).provenance=feature_provenance(feature,history,history_truncated);
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
BuiltModel::BuiltModel(const Json& model, const Json& snapshot) : impl_(std::make_unique<Impl>()) {
  validate_model(model);
  try {
    if(snapshot.at("features").size()!=model.at("features").size())
      throw Error("cache_miss","Cached feature count mismatch");
    for(const auto& feature:model.at("features")) {
      const auto id=text_field(feature,"id");
      const auto& entry=snapshot.at("features").at(id);
      if (feature.at("type")=="assembly") {
        if (!entry.value("assembly",false)) throw Error("cache_miss","Missing cached assembly marker");
        // Recompute placements and ownership from saved intent and restored
        // exact input shapes. B-rep deserialization cannot prove old enumeration
        // IDs still denote the same part, so none are trusted from the cache.
        Json history=Json::array(); bool truncated=false;
        auto geometry=build_assembly(feature,model.at("parameters"),impl_->features,history,truncated);
        if (geometry.faces.Extent()!=entry.at("faces") || geometry.edges.Extent()!=entry.at("edges"))
          throw Error("cache_miss","Cached assembly topology count mismatch");
        geometry.provenance=feature_provenance(feature,history,truncated);
        impl_->features.emplace(id,std::move(geometry));
        continue;
      }
      std::istringstream stream(entry.at("brep").get<std::string>());
      TopoDS_Shape shape; BRepTools::Read(shape,stream,BRep_Builder{});
      if(stream.fail() || shape.IsNull()) throw Error("cache_miss","Cannot read cached B-rep");
      if(feature.at("type")!="sketch") check_shape(shape);
      else if(!BRepCheck_Analyzer(shape).IsValid()) throw Error("cache_miss","Invalid cached sketch");
      auto& geometry=impl_->features.emplace(id,FeatureGeometry(shape)).first->second;
      if(geometry.faces.Extent()!=entry.at("faces") || geometry.edges.Extent()!=entry.at("edges"))
        throw Error("cache_miss","Cached topology count mismatch");
      geometry.provenance=entry.at("provenance");
    }
    impl_->output=text_field(model,"output");
    impl_->shape=impl_->features.at(impl_->output).shape;
  } catch(const Standard_Failure& e) { throw occt_error(e,Json::object(),"cache_miss"); }
}
Json BuiltModel::snapshot() const {
  try {
    Json features=Json::object(); std::size_t bytes=0;
    for(const auto& [id,geometry]:impl_->features) {
      if (!geometry.parts.empty()) {
        features[id]={{"assembly",true},{"faces",geometry.faces.Extent()},{"edges",geometry.edges.Extent()}};
        continue;
      }
      SnapshotBuffer buffer(32*1024*1024-bytes); std::ostream stream(&buffer);
      // Tessellation is derived data. Store exact curves, surfaces and topology.
      BRepTools::Write(geometry.shape,stream,false,false,TopTools_FormatVersion_CURRENT);
      auto brep=std::move(buffer.bytes); bytes+=brep.size();
      if(!stream || bytes>32*1024*1024) throw Error("limit_exceeded","Geometry snapshot exceeds cache budget");
      features[id]={{"brep",std::move(brep)},{"faces",geometry.faces.Extent()},
        {"edges",geometry.edges.Extent()},{"provenance",geometry.provenance}};
    }
    return {{"features",std::move(features)}};
  } catch(const Standard_Failure& e) { throw occt_error(e,Json::object(),"cache_miss"); }
}
BuiltModel::~BuiltModel() = default;
BuiltModel::BuiltModel(BuiltModel&&) noexcept = default;
BuiltModel& BuiltModel::operator=(BuiltModel&&) noexcept = default;

Json BuiltModel::summary(const std::string& feature_id) const {
  try {
    const auto& geometry = impl_->feature(feature_id);
    const auto& shape = geometry.shape;
    const auto solids = count(shape, TopAbs_SOLID);
    GProp_GProps volume, area;
    if (solids) BRepGProp::VolumeProperties(shape, volume);
    BRepGProp::SurfaceProperties(shape, area);
    Bnd_Box box;
    BRepBndLib::AddOptimal(shape, box, false, false);
    const auto limits = box.Get();
    const auto center = solids ? volume.CentreOfMass() : area.CentreOfMass();
    Json result={{"valid", true}, {"units", "mm"}, {"volume_mm3", solids ? volume.Mass() : 0.0}, {"area_mm2", area.Mass()},
      {"center_of_mass_mm", {center.X(), center.Y(), center.Z()}},
      {"bounds_mm", {{"min", {limits.Xmin, limits.Ymin, limits.Zmin}}, {"max", {limits.Xmax, limits.Ymax, limits.Zmax}}}},
      {"solid_count", solids}, {"face_count", count(shape, TopAbs_FACE)},
      {"edge_count", count(shape, TopAbs_EDGE)}};
    if (!geometry.parts.empty()) {
      result["assembly"]={{"parts",Json::array()},{"mates",geometry.mates}};
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

Json BuiltModel::topology(const std::string& feature_id, const QueryLimits& limits) const {
  try {
    const auto& geometry = impl_->feature(feature_id);
    topology_limit(geometry, limits);
    const auto id = feature_id.empty() ? impl_->output : feature_id;
    Json result = {{"schema_version", 1}, {"units", "mm"}, {"feature_id", id}, {"selection_lifetime", "evaluation"},
      {"faces", Json::array()}, {"edges", Json::array()}, {"provenance",geometry.provenance}};
    for (int i = 1; i <= geometry.faces.Extent(); ++i) {
      const auto face = TopoDS::Face(geometry.faces(i));
      GProp_GProps props;
      BRepGProp::SurfaceProperties(face, props);
      BRepAdaptor_Surface surface(face);
      Json item = {{"id", "face-" + std::to_string(i)}, {"surface_kind", surface_kind(surface.GetType())},
        {"area_mm2", props.Mass()}, {"center_mm", point(props.CentreOfMass())}, {"bounds_mm", bounds(face)}};
      if (!geometry.parts.empty()) item["part_id"]=geometry.face_parts[i];
      if (surface.GetType() == GeomAbs_Plane) {
        auto normal = surface.Plane().Axis().Direction();
        if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
        item["normal"] = direction(normal);
      }
      result["faces"].push_back(item);
    }
    for (int i = 1; i <= geometry.edges.Extent(); ++i) {
      auto edge=edge_descriptor(TopoDS::Edge(geometry.edges(i)),i);
      if (!geometry.parts.empty()) edge["part_id"]=geometry.edge_parts[i];
      result["edges"].push_back(std::move(edge));
    }
    // A pick suggests a geometric rule only when that rule is unique here. The
    // caller must keep the rule, never the enumeration ID, for future rebuilds.
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
    BRepMesh_IncrementalMesh tessellation(geometry.shape, 0.1, false, 0.5, false);
    if (!tessellation.IsDone()) throw Error("kernel_failure", "Tessellation failed");
    Json result = {{"schema_version", 1}, {"units", "mm"}, {"feature_id", feature_id.empty() ? impl_->output : feature_id},
      {"selection_lifetime", "evaluation"}, {"linear_deflection_mm", 0.1}, {"angular_deflection_rad", 0.5},
      {"positions", Json::array()}, {"triangles", Json::array()}, {"triangle_faces", Json::array()}, {"edges", Json::array()}};
    for (int i = 1; i <= geometry.faces.Extent(); ++i) {
      const auto face = TopoDS::Face(geometry.faces(i));
      TopLoc_Location location;
      const auto triangulation = BRep_Tool::Triangulation(face, location);
      if (triangulation.IsNull()) throw Error("kernel_failure", "A face has no triangulation", {{"face_id", "face-" + std::to_string(i)}});
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

Json BuiltModel::drawing(const Json& spec, Json* exact_projections) const {
  fields(spec,{"views"},{"hidden_lines"});
  if (!spec.at("views").is_array() || spec.at("views").empty() || spec.at("views").size()>6)
    throw Error("invalid_argument", "A drawing requires 1 to 6 views");
  if (spec.contains("hidden_lines") && !spec.at("hidden_lines").is_boolean())
    throw Error("invalid_argument", "hidden_lines must be boolean");
  const bool hidden = spec.value("hidden_lines",true);
  topology_limit(impl_->feature(""),QueryLimits{});
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
      TopoDS_Shape view_shape=impl_->shape;
      auto view_parts=impl_->feature("").parts;
      if (view.contains("explode")) {
        const auto& geometry=impl_->feature("");
        if (geometry.parts.empty()) throw Error("invalid_argument","Exploded views require an assembly output");
        const auto& explode=view.at("explode");
        if (!explode.is_array() || explode.empty() || explode.size()>64)
          throw Error("invalid_argument","Exploded views require 1 to 64 part translations");
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
        if (orientation=="section") throw Error("invalid_argument","Section balloons are not supported",{{"feature_id",impl_->output}});
        anchor_count+=view.at("balloon_anchors").size();
        if (anchor_count>64) throw Error("limit_exceeded","A drawing permits at most 64 balloon anchors",{{"feature_id",impl_->output}});
        projected_anchors=balloon_anchors(view.at("balloon_anchors"),view_parts,view_shape,frame,impl_->output);
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
  const Json* assembly=nullptr;
  for(const auto& feature:model.at("features")) if(feature.at("id")==id) assembly=&feature;
  if(!assembly || assembly->at("type")!="assembly") throw Error("invalid_argument","Robot export requires an assembly",{{"feature_id",id}});
  const auto& parameters=model.at("parameters");
  std::map<std::string,const Json*> incoming;
  if(assembly->contains("mates")) for(const auto& mate:assembly->at("mates")) incoming[text_field(mate,"child")]=&mate;
  std::map<std::string,gp_Trsf> world;
  Json result={{"feature_id",id},{"links",Json::array()},{"joints",Json::array()},{"motion",assembly_motion(*assembly,parameters)}};
  try {
    for(const auto& part:geometry.parts) {
      const auto frame=incoming.contains(part.id)?local_frame(incoming.at(part.id)->at("child_frame"),parameters):gp_Trsf{};
      world[part.id]=part.transform*frame;
      result["links"].push_back({{"name","part_"+part.id},{"part_id",part.id},{"input",part.input},
        {"world",matrix(world.at(part.id))},{"mesh_origin",matrix(frame.Inverted())}});
    }
    for(const auto& part:geometry.parts) {
      if(!incoming.contains(part.id)) {
        result["joints"].push_back({{"name","root_"+part.id},{"type","fixed"},{"parent","world"},{"child","part_"+part.id},{"origin",matrix(world.at(part.id))}});
        continue;
      }
      const auto& mate=*incoming.at(part.id);const auto mid=text_field(mate,"id"),type=text_field(mate,"type"),parent=text_field(mate,"parent");
      auto origin=world.at(parent).Inverted()*world.at(part.id);
      auto joint=[&](const std::string& suffix,const std::string& kind,const std::string& p,const std::string& c,const gp_Trsf& placement,const std::string& coordinate) {
        Json item={{"name","mate_"+mid+suffix},{"type",kind},{"parent",p},{"child",c},{"origin",matrix(placement)},{"mate_id",mid}};
        if(!coordinate.empty()) item["coordinate"]=coordinate;
        result["joints"].push_back(std::move(item));
      };
      if(type=="cylindrical") {
        double travel=0;
        for(const auto& dof:geometry.motion.at("dofs")) if(dof.at("mate_id")==mid && dof.at("coordinate")=="travel_mm") travel=dof.at("value");
        gp_Trsf shift;shift.SetTranslation(gp_Vec(0,0,travel));
        const auto carrier=world.at(part.id)*shift.Inverted();const auto name="carrier_"+mid;
        result["links"].push_back({{"name",name},{"carrier_for",mid},{"world",matrix(carrier)}});
        joint("_angle","revolute","part_"+parent,name,world.at(parent).Inverted()*carrier,"angle_deg");
        joint("_travel","prismatic",name,"part_"+part.id,shift,"travel_mm");
      } else joint(type=="revolute"?"_angle":type=="slider"?"_travel":"_fixed",type=="slider"?"prismatic":type=="rigid"?"fixed":"revolute",
        "part_"+parent,"part_"+part.id,origin,type=="revolute"?"angle_deg":type=="slider"?"travel_mm":"");
    }
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
      BRepBuilderAPI_Copy copy(impl_->feature(feature_id).shape);
      BRepMesh_IncrementalMesh mesh(copy.Shape(), 0.1, false, 0.5, false);
      if (!mesh.IsDone()) throw Error("export_failed", "Tessellation failed");
      StlAPI_Writer writer;
      writer.ASCIIMode() = false;
      std::ofstream output(path, std::ios::binary | std::ios::trunc);
      if (!output || !writer.Write(copy.Shape(), output)) throw Error("export_failed", "STL writer failed");
      output.flush();
      if (!output) throw Error("export_failed", "STL stream write failed");
      output.close();
      if (output.fail()) throw Error("export_failed", "STL stream close failed");
    } else throw Error("invalid_argument", "Export format must be step or stl");
  } catch (const Standard_Failure& e) {
    throw occt_error(e, Json::object(), "export_failed");
  }
}
}
