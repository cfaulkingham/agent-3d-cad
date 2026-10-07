#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/hash.hpp"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <STEPControl_Writer.hxx>
#include <RWStl.hxx>
#include <STEPControl_Reader.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <numbers>
#include <random>
#include <sstream>
using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message) { ++checks; if (!value) throw std::runtime_error(message); }
void near(double actual,double expected,double tolerance=1e-5) { require(std::abs(actual-expected)<tolerance,"Expected "+std::to_string(expected)+", got "+std::to_string(actual)); }
void error(const std::string& code,const std::function<void()>& action) {
  try { action(); } catch(const Error& e) { require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what()); return; }
  throw std::runtime_error("Expected "+code);
}
Json document(Json features,const std::string& output) { return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",features},{"output",output}}; }
Json plane(Json origin={0,0,0}) { return {{"origin",origin},{"normal",{0,0,1}},{"x_direction",{1,0,0}}}; }
Json sketch(const std::string& id,Json profile,Json workplane=plane()) { return {{"id",id},{"type","sketch"},{"profile",profile},{"workplane",workplane}}; }
Json rectangle(double width=20,double height=30) { return {{"type","rectangle"},{"width",width},{"height",height}}; }
Json circle(double radius=2) { return {{"type","circle"},{"radius",radius}}; }
Json extruded(Json profile=rectangle()) { return document(Json::array({sketch("profile",profile),{{"id","part"},{"type","extrude"},{"input","profile"},{"distance",10}}}),"part"); }
Json expression(const std::string& op,Json a,Json b,const std::string& unit="mm") { return {{"expression",{{"op",op},{"args",Json::array({a,b})},{"unit",unit}}}}; }
void thread_tests() {
  const auto directory=std::filesystem::temp_directory_path()/("agentcad-thread-"+std::to_string(std::random_device{}()));
  require(std::filesystem::create_directory(directory),"isolated thread STEP workspace");
  struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}} cleanup{directory};
  auto model=document(Json::array({{{"id","stud"},{"type","external_thread"},{"major_diameter",20},{"pitch",2.5},{"length",20}}}),"stud");
  const auto started=std::chrono::steady_clock::now();
  BuiltModel thread(model);
  const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
  std::cout<<"M20x2.5x20 thread build: "<<seconds<<" seconds\n";
  const auto summary=thread.summary();
  require(summary.at("solid_count")==1,"thread is one connected solid");
  near(summary.at("bounds_mm").at("min")[2],0,1e-4);near(summary.at("bounds_mm").at("max")[2],20,1e-4);
  near(summary.at("bounds_mm").at("max")[0],10,1e-4);near(summary.at("bounds_mm").at("min")[0],-10,1e-4);
  require(summary.at("volume_mm3").get<double>()>std::numbers::pi*8.46*8.46*20 && summary.at("volume_mm3").get<double>()<std::numbers::pi*10*10*20,"thread volume lies between its root core and major-diameter cylinder");
  const auto roundtrip=[&](const BuiltModel& built,const std::string& name) {
    const auto path=directory/(name+".step");built.export_file(path,"step");
    STEPControl_Reader reader;const auto bytes=path.u8string();const std::string filename(bytes.begin(),bytes.end());
    require(reader.ReadFile(filename.c_str())==IFSelect_RetDone && reader.TransferRoots()>0,"thread STEP reload succeeds");
    const auto shape=reader.OneShape();require(BRepCheck_Analyzer(shape).IsValid(),"thread STEP retains valid exact B-rep geometry");
    int count=0;TopoDS_Solid solid;
    for(TopExp_Explorer item(shape,TopAbs_SOLID);item.More();item.Next()){solid=TopoDS::Solid(item.Current());++count;}
    require(count==1,"thread STEP retains one solid (got "+std::to_string(count)+", shape type "+std::to_string(shape.ShapeType())+")");
    GProp_GProps volume;BRepGProp::VolumeProperties(shape,volume);
    near(volume.Mass(),built.summary().at("volume_mm3"),.01);
    return solid;
  };
  const auto solid=roundtrip(thread,"right");
  const auto inside=[](const TopoDS_Solid& solid,double radius,double angle,double z) {
    BRepClass3d_SolidClassifier classifier(solid,gp_Pnt(radius*std::cos(angle),radius*std::sin(angle),z),1e-6);
    return classifier.State()==TopAbs_IN;
  };
  // At a radius near the crest, each quarter turn advances the material by
  // P/4. Stacked toroidal rings cannot satisfy both these inside/outside pairs.
  for(int quarter=0;quarter<4;++quarter) {
    const double angle=quarter*std::numbers::pi/2;
    require(inside(solid,9.7,angle,5+quarter*.625),"right-hand helix advances one quarter pitch per quarter turn");
    if(quarter)require(!inside(solid,9.7,angle,5),"thread ridge is not a stack of planar rings");
  }
  const double pitch_diameter=20-3*std::sqrt(3.0)*2.5/8,minor_diameter=20-17*std::sqrt(3.0)*2.5/24;
  near(pitch_diameter,18.3762023679,1e-9);near(minor_diameter,16.9328266949,1e-9);
  require(inside(solid,pitch_diameter/2-.001,0,5+2.5/4) && !inside(solid,pitch_diameter/2+.001,0,5+2.5/4),"actual 60-degree flank crosses the nominal pitch diameter at half-pitch tooth width");
  require(inside(solid,minor_diameter/2-.001,0,6.25) && !inside(solid,minor_diameter/2+.001,0,6.25),"actual root is the nominal external minor diameter");
  require(inside(solid,9.999,0,5) && !inside(solid,10.001,0,5),"crest reaches exactly the nominal major diameter");
  require(inside(solid,minor_diameter/2-.01,0,19.9) && !inside(solid,9.7,0,19.9),"positive-Z lead chamfer eases the free tip while retaining the core");
  const auto mesh=thread.mesh();require(mesh.at("triangles").size()>100,"continuous thread tessellates for viewing");
  auto left=model;left["features"][0]["handedness"]="left";
  BuiltModel left_thread(left);const auto left_solid=roundtrip(left_thread,"left");
  near(left_thread.summary().at("volume_mm3"),summary.at("volume_mm3"),.01);
  require(inside(left_solid,9.7,std::numbers::pi/2,5-.625) && !inside(left_solid,9.7,std::numbers::pi/2,5+.625),"left-hand profile reverses the helical phase");
  auto edited=model;edited["parameters"]={{"pitch",2.5},{"length",20}};
  edited["features"][0]["pitch"]={{"parameter","pitch"}};edited["features"][0]["length"]=expression("add",Json{{"parameter","length"}},1);
  edited["parameters"]["pitch"]=3;
  BuiltModel rebuilt(edited);near(rebuilt.summary().at("bounds_mm").at("max")[2],21,1e-4);
  const auto rebuilt_solid=roundtrip(rebuilt,"rebuilt");require(inside(rebuilt_solid,9.7,std::numbers::pi/2,6+.75) && !inside(rebuilt_solid,9.7,std::numbers::pi/2,6),"parameter and expression rebuild changes actual helical pitch");
  auto knob=document(Json::array({{{"id","grip"},{"type","cylinder"},{"radius",22.5},{"height",18}},
    {{"id","stud"},{"type","external_thread"},{"major_diameter",20},{"pitch",2.5},{"length",21},{"origin",{0,0,17}}},
    {{"id","knob"},{"type","fuse"},{"left","grip"},{"right","stud"}}}),"knob");
  BuiltModel joined(knob);require(joined.summary().at("solid_count")==1,"thread core fuses to the knob head as one solid");
  near(joined.summary().at("bounds_mm").at("min")[2],0,1e-4);near(joined.summary().at("bounds_mm").at("max")[2],38,1e-4);
  roundtrip(joined,"knob");
  std::ifstream example_stream(std::filesystem::path(CAD_SOURCE_DIR)/"examples/m20-knob.create.json");
  Json example;example_stream>>example;
  BuiltModel full_knob(example.at("model"));roundtrip(full_knob,"full-knob");
  require(full_knob.summary().at("solid_count")==1,"full scalloped knob example retains one STEP-exportable solid");
  near(full_knob.summary().at("bounds_mm").at("max")[2],38,1e-4);
  for(const auto& dimensions : {std::array<double,3>{.3,.1,.1},std::array<double,3>{20,2.5,40},std::array<double,3>{200,2,4}}) {
    auto boundary=model;boundary["features"][0]["major_diameter"]=dimensions[0];boundary["features"][0]["pitch"]=dimensions[1];boundary["features"][0]["length"]=dimensions[2];
    BuiltModel valid(boundary);require(valid.summary().at("solid_count")==1,"thread bounds produce a valid connected solid");
    near(valid.summary().at("bounds_mm").at("max")[2],dimensions[2],1e-4);
  }
  for(const auto& [key,value] : std::initializer_list<std::pair<const char*,double>>{{"pitch",0},{"pitch",.09},{"pitch",8},{"pitch",.19},{"length",0},{"length",2.4},{"length",40.1},{"major_diameter",201},{"major_diameter",0}}) {
    auto invalid=model;invalid["features"][0][key]=value;error("invalid_model",[&]{validate_model(invalid);});
  }
  auto invalid=model;invalid["features"][0]["handedness"]="clockwise";error("invalid_model",[&]{validate_model(invalid);});
  invalid=model;invalid["features"][0]["pitch"]=expression("add",1,1,"deg");error("invalid_model",[&]{validate_model(invalid);});
}
void tests() {
  auto model=extruded();
  BuiltModel prism(model);
  near(prism.summary().at("volume_mm3"),6000);
  require(prism.summary("profile").at("solid_count")==0,"sketch is an explicit intermediate face");
  require(!prism.topology().at("provenance").at("history").empty(),"extrusion records OCCT history evidence");
  model["features"][0]["workplane"]={{"origin",{1,2,3}},{"normal",{0,1,0}},{"x_direction",{1,0,0}}};
  model["features"][1]["distance"]=5;
  auto tilted=BuiltModel(model).summary();
  near(tilted.at("volume_mm3"),3000);
  near(tilted.at("bounds_mm").at("max")[1],7);
  near(tilted.at("bounds_mm").at("min")[2],-27);
  model["features"][1]["distance"]=-5;
  near(BuiltModel(model).summary().at("bounds_mm").at("min")[1],-3);
  near(BuiltModel(extruded(circle(5))).summary().at("volume_mm3"),250*std::numbers::pi);
  near(BuiltModel(extruded({{"type","polygon"},{"points",{{0,0},{4,0},{0,3}}}})).summary().at("volume_mm3"),60);
  near(BuiltModel(extruded({{"type","polygon"},{"points",{{0,3},{4,0},{0,0}}}})).summary().at("volume_mm3"),60);
  model=extruded({{"type","polygon"},{"points",{{0,0},{4,4},{0,4},{4,0}}}});
  error("invalid_shape",[&]{BuiltModel invalid(model);});
  model=extruded(); model["features"][0]["workplane"]["x_direction"]={0,0,1};
  error("invalid_model",[&]{validate_model(model);});
  model=extruded(); model["output"]="profile";
  error("invalid_model",[&]{validate_model(model);});
  model=document(Json::array({sketch("profile",rectangle(3,4),plane({2,0,0})),
    {{"id","part"},{"type","revolve"},{"input","profile"},{"axis",{{"origin",{0,0,0}},{"direction",{0,1,0}}}},{"angle_deg",360}}}),"part");
  near(BuiltModel(model).summary().at("volume_mm3"),84*std::numbers::pi);
  model["features"][1]["angle_deg"]=180;
  near(BuiltModel(model).summary().at("volume_mm3"),42*std::numbers::pi);
  model=document(Json::array({sketch("bottom",rectangle(10,15)),sketch("top",rectangle(20,30),plane({0,0,10})),
    {{"id","part"},{"type","loft"},{"sections",{"bottom","top"}},{"ruled",true}}}),"part");
  for (const bool ruled : {true,false}) {
    model["features"][2]["ruled"]=ruled;
    BuiltModel loft(model);
    near(loft.summary().at("volume_mm3"),3500);
    const auto topology=loft.topology();
    const auto& provenance=topology.at("provenance");
    require(provenance.at("dependencies")==Json::array({"bottom","top"}),"loft retains all section dependencies");
    require(provenance.at("history_truncated")==false,"small loft history is complete");
    std::map<std::string,Json> source_entities,result_entities;
    for (const auto* input : {"bottom","top"}) {
      const auto section=loft.topology(input);
      require(section.at("faces").size()==1 && section.at("edges").size()==4,"loft preserves each original section topology");
      for (const auto* kind : {"faces","edges"})
        for (const auto& entity : section.at(kind)) source_entities.emplace(std::string(input)+"/"+entity.at("id").get<std::string>(),entity);
    }
    near(loft.summary("bottom").at("area_mm2"),150);
    near(loft.summary("top").at("area_mm2"),600);
    for (const auto* kind : {"faces","edges"})
      for (const auto& entity : topology.at(kind)) result_entities.emplace(entity.at("id"),entity);
    std::size_t generated_faces=0;
    for (const auto& entry : provenance.at("history")) {
      require(source_entities.contains(entry.at("source_feature_id").get<std::string>()+"/"+entry.at("source_id").get<std::string>()),"loft history source belongs to the stated section");
      require(!entry.contains("instance_index"),"loft history has no pattern instance index");
      if (!entry.contains("result_id")) continue;
      require(result_entities.contains(entry.at("result_id")),"loft history result belongs to evaluated loft");
      if (entry.at("relation")=="generated" && entry.at("result_kind")=="face") {
        require(entry.at("source_kind")=="edge","loft side face is generated from a section edge");
        const auto& face=result_entities.at(entry.at("result_id"));
        near(face.at("bounds_mm").at("min")[2],0);
        near(face.at("bounds_mm").at("max")[2],10);
        ++generated_faces;
      }
    }
    require(generated_faces>=4,"loft exposes available generated side-face evidence");
  }
  model=document(Json::array({sketch("profile",circle()),{{"id","part"},{"type","sweep"},{"input","profile"},{"path",{{0,0,0},{0,0,10}}}}}),"part");
  near(BuiltModel(model).summary().at("volume_mm3"),40*std::numbers::pi);
  model["features"][1]["path"]={{1,0,0},{1,0,10}};
  error("invalid_model",[&]{BuiltModel invalid(model);});
  model=extruded();
  model["features"].push_back({{"id","moved"},{"type","transform"},{"input","part"},{"translation",{100,0,0}},
    {"rotation",{{"origin",{0,0,0}},{"axis",{0,0,1}},{"angle_deg",90}}}});
  model["output"]="moved";
  auto moved=BuiltModel(model).summary();
  near(moved.at("volume_mm3"),6000); near(moved.at("bounds_mm").at("min")[0],70); near(moved.at("bounds_mm").at("max")[0],100);
  model["features"].push_back({{"id","copies"},{"type","pattern"},{"input","moved"},{"count",3},{"step",{50,0,0}}});
  model["output"]="copies";
  auto copies=BuiltModel(model).summary(); near(copies.at("volume_mm3"),18000); require(copies.at("solid_count")==3,"pattern creates independent exact solids");
  model=extruded(); model["features"].push_back({{"id","reused"},{"type","instance"},{"input","part"},{"translation",{10,0,0}}}); model["output"]="reused";
  near(BuiltModel(model).summary().at("bounds_mm").at("min")[0],10);
  model=extruded(); model["features"].push_back({{"id","bored"},{"type","hole"},{"input","part"},{"origin",{10,15,0}},{"axis",{0,0,1}},{"radius",2},{"depth",10}}); model["output"]="bored";
  BuiltModel bored(model); near(bored.summary().at("volume_mm3"),6000-40*std::numbers::pi);
  const auto history=bored.topology().at("provenance").at("history");
  require(!history.empty(),"hole records source-to-result history");
  for(const auto& h:history) require(h.at("source_feature_id")=="part","history is feature-qualified");
  model=extruded(); model["parameters"]["thickness"]=5;
  model["features"][1]["distance"]=expression("multiply",Json{{"parameter","thickness"}},expression("add",1,1,"dimensionless"));
  near(BuiltModel(model).summary().at("volume_mm3"),6000);
  model["parameters"]["thickness"]=7;
  near(BuiltModel(model).summary().at("volume_mm3"),8400);
  error("invalid_model",[&]{scalar(expression("add",1,2,"deg"),Json::object());});
  error("invalid_model",[&]{scalar(expression("multiply",2,expression("add",1,2)),Json::object());});
  error("invalid_model",[&]{scalar(expression("divide",1,0),Json::object());});
  error("invalid_model",[&]{scalar(expression("shell",1,2),Json::object());});
  error("invalid_argument",[&]{scalar(expression("multiply",1e6,2),Json::object());});
  Json deep=1; for(int i=0;i<18;++i) deep=expression("add",deep,1);
  error("limit_exceeded",[&]{scalar(deep,Json::object());});
  Json wide=1; for(int i=0;i<7;++i) wide=expression("add",wide,wide);
  error("limit_exceeded",[&]{scalar(wide,Json::object());});
  model=extruded(); model["features"][1]["distance"]=expression("add",1,2,"deg");
  error("invalid_model",[&]{validate_model(model);});
  const auto directory=std::filesystem::temp_directory_path()/("agentcad-modeling-"+std::to_string(std::random_device{}()));
  require(std::filesystem::create_directory(directory),"isolated STEP workspace");
  struct Cleanup {std::filesystem::path path; ~Cleanup(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}} cleanup{directory};
  const auto step_path=directory/std::filesystem::path(u8"prism-\u00E9.step");
  const auto stl_path=directory/std::filesystem::path(u8"prism-\u00E9.stl");
  prism.export_file(step_path,"step");
  prism.export_file(stl_path,"stl");
  require(std::filesystem::file_size(stl_path)>84,"STL export accepts a Unicode filesystem path");
  std::ifstream stl_input(stl_path,std::ios::binary);
  const auto stl_mesh=RWStl::ReadBinaryStream(stl_input);
  require(!stl_mesh.IsNull() && stl_mesh->NbTriangles()>0,"Unicode STL path contains readable binary triangles");
  std::ifstream input(step_path); std::ostringstream data; data<<input.rdbuf();
  const auto content=data.str();
  model=document(Json::array({{{"id","imported"},{"type","import_step"},{"content",content},{"sha256",sha256(content)}}}),"imported");
  near(BuiltModel(model).summary().at("volume_mm3"),6000);
  require(BuiltModel(model).topology().at("provenance").at("content_sha256")==sha256(content),"import provenance retains immutable content identity");
  auto meters=content;
  const auto units=meters.find("SI_UNIT(.MILLI.,.METRE.)");
  require(units!=std::string::npos,"STEP writer declares millimeter source units");
  meters.replace(units,std::string("SI_UNIT(.MILLI.,.METRE.)").size(),"SI_UNIT($,.METRE.)");
  auto metric_import=model;
  metric_import["features"][0]["content"]=meters; metric_import["features"][0]["sha256"]=sha256(meters);
  near(BuiltModel(metric_import).summary().at("volume_mm3"),6e12,10);
  const auto import_shape=[&](const TopoDS_Shape& shape) {
    STEPControl_Writer writer;
    writer.SetShapeProcessFlags(ShapeProcess::OperationsFlags{});
    require(writer.Transfer(shape,STEPControl_AsIs)==IFSelect_RetDone,"solid purity fixture transfers");
    const auto output=directory/"purity.step";
    const auto name=output.u8string(); const std::string filename(name.begin(),name.end());
    require(writer.Write(filename.c_str())==IFSelect_RetDone,"solid purity fixture writes");
    std::ifstream stream(output); std::ostringstream serialized;serialized<<stream.rdbuf();
    const auto payload=serialized.str();
    return document(Json::array({{{"id","imported"},{"type","import_step"},{"content",payload},{"sha256",sha256(payload)}}}),"imported");
  };
  BRep_Builder builder;
  TopoDS_Compound mixed; builder.MakeCompound(mixed);
  builder.Add(mixed,BRepPrimAPI_MakeBox(10,10,10).Shape());
  builder.Add(mixed,BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,20),gp_Dir(0,0,1)),0,5,0,5).Face());
  const auto mixed_import=import_shape(mixed);
  error("invalid_shape",[&]{BuiltModel invalid(mixed_import);});
  model["features"][0]["content"]=content+"\n";
  error("invalid_model",[&]{validate_model(model);});
  model["features"][0]["content"]="not a STEP file"; model["features"][0]["sha256"]=sha256("not a STEP file");
  error("kernel_failure",[&]{BuiltModel invalid(model);});
  const auto defs=model_definitions();
  require(defs.at("scalar").at("oneOf").size()==3,"expression schema discoverable");
  require(defs.at("feature").at("oneOf").size()==15,"modeling and assembly schemas discoverable");
  thread_tests();
}
}
int main() {
  configure_kernel_logging();
  try {tests(); std::cout<<"modeling: "<<checks<<" checks passed\n";return 0;}
  catch(const std::exception& e) {std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;}
}
