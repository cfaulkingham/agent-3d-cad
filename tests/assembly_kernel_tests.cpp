#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp_Explorer.hxx>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <random>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message) { ++checks; if (!value) throw std::runtime_error(message); }
void near(double actual,double expected,double tolerance=1e-7) {
  require(std::abs(actual-expected)<tolerance,"Expected "+std::to_string(expected)+", got "+std::to_string(actual));
}
void error(const std::string& code,const std::function<void()>& action) {
  try { action(); } catch (const Error& e) { require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what()); return; }
  throw std::runtime_error("Expected "+code);
}
void equivalent(const Json& a,const Json& b) {
  if (a.is_number() && b.is_number()) { near(a.get<double>(),b.get<double>(),1e-7*std::max(1.0,std::abs(a.get<double>()))); return; }
  require(a.type()==b.type(),"Equivalent JSON type");
  if (a.is_object()) { require(a.size()==b.size(),"Equivalent object keys"); for (const auto& [key,value]:a.items()) equivalent(value,b.at(key)); }
  else if (a.is_array()) { require(a.size()==b.size(),"Equivalent array length"); for (std::size_t i=0;i<a.size();++i) equivalent(a[i],b[i]); }
  else require(a==b,"Equivalent JSON value");
}
Json frame(Json origin={0,0,0},Json normal={0,0,1},Json x={1,0,0}) { return {{"origin",origin},{"normal",normal},{"x_direction",x}}; }
Json part(const std::string& id,const std::string& input="block") { return {{"id",id},{"input",input}}; }
Json mate(const std::string& id,const std::string& parent,const std::string& child,Json origin={0,0,0}) {
  return {{"id",id},{"type","rigid"},{"parent",parent},{"child",child},{"parent_frame",frame(origin)},{"child_frame",frame()}};
}
Json model(Json parts,Json mates=Json::array()) {
  return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},
    {"features",Json::array({{{"id","block"},{"type","box"},{"size",{2,3,4}}},
      {{"id","group"},{"type","assembly"},{"parts",parts},{"mates",mates}}})},{"output","group"}};
}
const Json& resolved(const Json& summary,const std::string& id) {
  for (const auto& part:summary.at("assembly").at("parts")) if (part.at("id")==id) return part;
  throw std::runtime_error("Missing resolved part "+id);
}
Json view(Json explode=Json()) {
  Json item={{"id","front"},{"orientation","front"}};
  if (!explode.is_null()) item["explode"]=explode;
  return {{"views",Json::array({item})},{"hidden_lines",false}};
}
void verify_ownership(const BuiltModel& built,int copies) {
  const auto topology=built.topology(),mesh=built.mesh();
  std::map<std::string,std::string> face_parts,edge_parts;
  std::map<std::string,int> face_counts,edge_counts;
  for (const auto& face:topology.at("faces")) {
    const auto owner=face.at("part_id").get<std::string>(); face_parts[face.at("id")]=owner; ++face_counts[owner];
  }
  for (const auto& edge:topology.at("edges")) {
    const auto owner=edge.at("part_id").get<std::string>(); edge_parts[edge.at("id")]=owner; ++edge_counts[owner];
    require(!edge.contains("selector"),"Placed assembly edge does not propose an upstream edit in world coordinates");
  }
  require(face_counts.size()==static_cast<std::size_t>(copies),"Every independent part owns faces");
  for (const auto& [part,count]:face_counts) { require(count==6,"Box instance owns six faces"); require(edge_counts.at(part)==12,"Box instance owns twelve edges"); }
  for (const auto& id:mesh.at("triangle_faces")) require(face_parts.contains(id),"Every triangle resolves to owned face");
  for (const auto& edge:mesh.at("edges")) require(edge.at("part_id")==edge_parts.at(edge.at("id")),"Mesh edge ownership matches exact topology");
  for (const auto& item:topology.at("provenance").at("history")) require(item.contains("part_id"),"Assembly provenance identifies source copy");
}
}
int main() {
  try {
    configure_kernel_logging();
    const auto coincident=model(Json::array({part("one"),part("two")}));
    BuiltModel overlap(coincident);
    near(overlap.summary().at("volume_mm3"),48);
    require(overlap.summary().at("solid_count")==2,"Coincident copies remain independent solids");
    require(overlap.summary().at("face_count")==12,"Coincident copies preserve separate topology");
    verify_ownership(overlap,2);
    const auto overlap_snapshot=overlap.snapshot();
    require(overlap_snapshot.at("features").at("group").at("assembly")==true,"Assembly snapshot has reconstruction marker");
    require(!overlap_snapshot.at("features").at("group").contains("brep"),"Assembly snapshot reuses exact source B-reps");
    BuiltModel warm_overlap(coincident,overlap_snapshot);
    equivalent(overlap.summary(),warm_overlap.summary()); equivalent(overlap.topology(),warm_overlap.topology());
    verify_ownership(warm_overlap,2);
    auto broken=overlap_snapshot; broken["features"]["group"]["faces"]=6;
    error("cache_miss",[&]{ BuiltModel rejected(coincident,broken); });

    auto placed=part("rotated");
    placed["placement"]={{"rotation",{{"origin",{1,0,0}},{"axis",{0,0,1}},{"angle_deg",90}}},{"translation",{10,20,30}}};
    BuiltModel rotated(model(Json::array({placed})));
    const auto rotated_summary=rotated.summary();
    equivalent(rotated_summary.at("bounds_mm").at("min"),Json::array({8.0,19.0,30.0}));
    equivalent(rotated_summary.at("bounds_mm").at("max"),Json::array({11.0,21.0,34.0}));
    near(resolved(rotated_summary,"rotated").at("transform")[3],11);
    near(resolved(rotated_summary,"rotated").at("transform")[7],19);

    // Both part and mate arrays intentionally place descendants first. Local
    // offset X rotates with root Z rotation, and each child frame is inverted.
    auto root=part("root"); root["placement"]={{"translation",{10,20,30}},
      {"rotation",{{"origin",{0,0,0}},{"axis",{0,0,1}},{"angle_deg",90}}}};
    auto first=mate("first","root","child",{2,0,0}); first["offset"]={3,0,1}; first["child_frame"]=frame({1,0,0});
    auto second=mate("second","child","leaf",{0,0,4}); second["angle_deg"]=90;
    auto chained=model(Json::array({part("leaf"),part("child"),root}),Json::array({second,first}));
    BuiltModel chain(chained); const auto chain_summary=chain.summary();
    near(resolved(chain_summary,"child").at("transform")[3],10);
    near(resolved(chain_summary,"child").at("transform")[7],24);
    near(resolved(chain_summary,"child").at("transform")[11],31);
    near(resolved(chain_summary,"leaf").at("transform")[3],10);
    near(resolved(chain_summary,"leaf").at("transform")[7],24);
    near(resolved(chain_summary,"leaf").at("transform")[11],35);
    near(resolved(chain_summary,"leaf").at("transform")[0],-1);
    near(chain_summary.at("volume_mm3"),72);
    near(chain.summary("block").at("bounds_mm").at("min")[0],0);
    verify_ownership(chain,3);
    BuiltModel restored_chain(chained,chain.snapshot());
    equivalent(chain_summary,restored_chain.summary()); equivalent(chain.topology(),restored_chain.topology());
    auto edit=chained; edit["features"][0]["size"]={2,3,8};
    BuiltModel edited(edit); near(edited.summary().at("volume_mm3"),144);
    near(chain.summary().at("volume_mm3"),72);

    // Non-default parent and child axes ensure solving is full rigid-frame
    // composition, not translation plus a global Z-only rotation.
    auto oriented=mate("orient","base","mounted");
    oriented["parent_frame"]=frame({0,0,4},{1,0,0},{0,1,0});
    oriented["child_frame"]=frame({0,0,1},{0,0,1},{1,0,0});
    BuiltModel oriented_parts(model(Json::array({part("base"),part("mounted")}),Json::array({oriented})));
    const auto oriented_summary=oriented_parts.summary(); const auto& mounted=resolved(oriented_summary,"mounted");
    equivalent(mounted.at("bounds_mm").at("min"),Json::array({-1.0,0.0,4.0}));
    equivalent(mounted.at("bounds_mm").at("max"),Json::array({3.0,2.0,7.0}));
    auto inverse_frame=mate("inverse","base","mounted",{0,0,4});
    inverse_frame["child_frame"]=frame({0,0,0},{1,0,0},{0,1,0});
    BuiltModel inverse_parts(model(Json::array({part("base"),part("mounted")}),Json::array({inverse_frame})));
    const auto inverse_summary=inverse_parts.summary();
    equivalent(resolved(inverse_summary,"mounted").at("bounds_mm").at("min"),Json::array({0.0,0.0,4.0}));
    equivalent(resolved(inverse_summary,"mounted").at("bounds_mm").at("max"),Json::array({3.0,4.0,6.0}));

    const auto plain_projection=overlap.drawing(view());
    const auto explosion=Json::array({{{"part_id","two"},{"translation",{0,0,10}}}});
    const auto exploded=overlap.drawing(view(explosion));
    near(plain_projection.at("views")[0].at("bounds_mm")[3],4);
    near(exploded.at("views")[0].at("bounds_mm")[3],14);
    require(exploded.at("views")[0].at("explode")==explosion,"Projection retains explicit translations");
    equivalent(overlap.drawing(view()),plain_projection);
    near(overlap.summary().at("bounds_mm").at("max")[2],4);
    equivalent(warm_overlap.drawing(view(explosion)),exploded);
    // A rotated parent does not rotate an exploded world-X translation.
    const auto oriented_plain=oriented_parts.drawing(view());
    const auto oriented_exploded=oriented_parts.drawing(view(Json::array({{{"part_id","mounted"},{"translation",{10,0,0}}}})));
    near(oriented_plain.at("views")[0].at("bounds_mm")[2],3);
    near(oriented_exploded.at("views")[0].at("bounds_mm")[2],13);
    error("invalid_argument",[&]{overlap.drawing(view(Json::array()));});
    error("invalid_argument",[&]{overlap.drawing(view(Json::array({{{"part_id","missing"},{"translation",{1,0,0}}}})));});
    error("invalid_argument",[&]{overlap.drawing(view(Json::array({explosion[0],explosion[0]})));});
    auto single=coincident; single["output"]="block";
    BuiltModel nonassembly(single);
    error("invalid_argument",[&]{nonassembly.drawing(view(explosion));});

    const auto directory=std::filesystem::temp_directory_path()/("agentcad-assembly-kernel-"+std::to_string(std::random_device{}()));
    require(std::filesystem::create_directory(directory),"Isolated STEP workspace");
    struct Cleanup { std::filesystem::path path; ~Cleanup(){std::error_code e;std::filesystem::remove_all(path,e);} } cleanup{directory};
    const auto path=directory/"assembly.step"; chain.export_file(path,"step");
    STEPControl_Reader reader; const auto bytes=path.u8string(); const std::string filename(bytes.begin(),bytes.end());
    require(reader.ReadFile(filename.c_str())==IFSelect_RetDone && reader.TransferRoots()>0,"Assembly exact STEP reloads");
    const auto imported=reader.OneShape(); require(BRepCheck_Analyzer(imported).IsValid(),"Export preserves valid solids");
    int solids=0; for (TopExp_Explorer item(imported,TopAbs_SOLID);item.More();item.Next()) ++solids;
    require(solids==3,"STEP preserves separate assembly solids");
    GProp_GProps volume; BRepGProp::VolumeProperties(imported,volume); near(volume.Mass(),72,1e-5);
    std::cout<<checks<<" assembly kernel checks passed\n";
  } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
