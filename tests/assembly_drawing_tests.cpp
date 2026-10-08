#include "agentcad/drawing.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include "geometry_equivalence.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <map>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message) {++checks;if(!value) throw std::runtime_error(message);}
void near(double a,double b) {require(std::abs(a-b)<1e-6,"Expected "+std::to_string(b)+", got "+std::to_string(a));}
void fails(const std::string& code,const std::function<void()>& action) {
  try {action();} catch(const Error& e) {require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what());return;}
  throw std::runtime_error("Expected "+code);
}
struct Temp {
  fs::path path;
  Temp():path(temporary_file(fs::temp_directory_path())) {fs::remove(path);directory(path);}
  ~Temp() {std::error_code ignored;fs::remove_all(path,ignored);}
};
Json model() {
  const Json seat={{"id","seat"},{"type","rigid"},{"parent","base"},{"child","spacer"},
    {"parent_frame",{{"origin",Json::array({10,5,Json{{"parameter","thickness"}}})},{"normal",{0,0,1}},{"x_direction",{1,0,0}}}},
    {"child_frame",{{"origin",{0,0,0}},{"normal",{0,0,1}},{"x_direction",{1,0,0}}}}};
  return {{"schema_version",1},{"units","mm"},{"parameters",{{"thickness",2},{"gap",12}}},
    {"features",Json::array({
      {{"id","plate"},{"type","box"},{"size",Json::array({20,10,Json{{"parameter","thickness"}}})}},
      {{"id","post"},{"type","cylinder"},{"radius",2},{"height",6}},
      {{"id","assembly"},{"type","assembly"},{"parts",Json::array({{{"id","base"},{"input","plate"}},{{"id","spacer"},{"input","post"}}})},
       {"mates",Json::array({seat})}}
    })},{"output","assembly"}};
}
Json recipe() {
  return {{"layout","grid"},{"views",Json::array({
    {{"id","assembled"},{"orientation","front"}},
    {{"id","exploded"},{"orientation","front"},{"explode",Json::array({{{"part_id","spacer"},{"translation",Json::array({0,0,Json{{"parameter","gap"}}})}}})}}
  })},{"dimensions",Json::array({{{"view","assembled"},{"kind","height"}},{{"view","exploded"},{"kind","height"}}})}};
}
Json projection_request(const Json& spec) {return {{"views",spec.at("views")},{"hidden_lines",spec.at("hidden_lines")}};}
Json identity() {return {{"document_id","assembly"},{"revision",1},{"kernel_version",kernel_version()}};}
Json worker_request(const Json& source,const Json& drawing) {
  return {{"kind","drawing"},{"drawing",normalize_drawing(drawing,source)},{"identity",identity()}};
}
std::string content(const Json& rendered,const std::string& filename) {
  for(const auto& file:rendered.at("files")) if(file.at("name")==filename) return file.at("content");
  throw std::runtime_error("Missing artifact "+filename);
}
void dimensions(const Json& result,double assembled,double exploded) {
  near(result.at("dimensions")[0].at("value_mm"),assembled);near(result.at("dimensions")[1].at("value_mm"),exploded);
}
void validation_tests() {
  const auto source=model(), drawing=recipe();
  const auto normalized=normalize_drawing(drawing,source);
  require(normalized.at("views")[1].at("explode")[0].at("translation")==Json::array({0,0,12}),"Explode mm parameters resolve");
  require(drawing==recipe() && source==model(),"Normalization preserves source and parameterized recipe");
  // Part names are model-internal identifiers: a Windows device name is valid.
  auto device=source;device["features"][2]["parts"][1]["id"]="aux";device["features"][2]["mates"][0]["child"]="aux";
  auto device_drawing=drawing;device_drawing["views"][1]["explode"][0]["part_id"]="aux";
  require(normalize_drawing(device_drawing,device).at("views")[1].at("explode")[0].at("part_id")=="aux","Explode accepts a part named like a device");
  auto invalid=drawing;invalid["views"][1]["explode"]=Json::array();
  fails("invalid_drawing",[&]{normalize_drawing(invalid,source);});
  invalid=drawing;invalid["views"][1]["explode"]=Json::object();
  fails("invalid_drawing",[&]{normalize_drawing(invalid,source);});
  invalid=drawing;invalid["views"][1]["explode"].push_back(invalid["views"][1]["explode"][0]);
  fails("invalid_drawing",[&]{normalize_drawing(invalid,source);});
  invalid=drawing;invalid["views"][1]["explode"][0]["part_id"]="post";
  fails("invalid_drawing",[&]{normalize_drawing(invalid,source);});
  invalid=drawing;invalid["views"][1]["explode"][0]["translation"]={0,0};
  fails("invalid_model",[&]{normalize_drawing(invalid,source);});
  invalid=drawing;invalid["views"][1]["explode"][0]["rotation"]={0,0,1};
  fails("invalid_argument",[&]{normalize_drawing(invalid,source);});
  invalid=drawing;invalid["views"][1]["explode"][0]["translation"][2]={{"expression",{{"op","add"},{"args",{1,2}},{"unit","deg"}}}};
  fails("invalid_model",[&]{normalize_drawing(invalid,source);});
  invalid=drawing;invalid["views"][1]["explode"][0]["translation"][2]={{"parameter","missing"}};
  fails("invalid_model",[&]{normalize_drawing(invalid,source);});
  invalid=drawing;for(int i=0;i<64;++i) invalid["views"][1]["explode"].push_back(invalid["views"][1]["explode"][0]);
  fails("invalid_drawing",[&]{normalize_drawing(invalid,source);});
  auto single=source;single["output"]="plate";
  fails("invalid_drawing",[&]{normalize_drawing(drawing,single);});
  invalid=drawing;invalid["layout"]="third_angle";
  fails("invalid_drawing",[&]{normalize_drawing(invalid,source);});
}
void geometry_tests() {
  const auto source=model(), normalized=normalize_drawing(recipe(),source);BuiltModel built(source);
  const auto initial_summary=built.summary(), initial_topology=built.topology();
  const auto projected=built.drawing(projection_request(normalized));
  const auto& a=projected.at("views")[0].at("bounds_mm");const auto& e=projected.at("views")[1].at("bounds_mm");
  near(a[3].get<double>()-a[1].get<double>(),8);near(e[3].get<double>()-e[1].get<double>(),20);
  const auto rendered=render_drawing(projected,normalized,identity());dimensions(rendered,8,20);
  require(rendered.at("view_layouts")[0].at("cell_mm")!=rendered.at("view_layouts")[1].at("cell_mm"),"Normal and exploded projections occupy separate grid cells");
  for(const auto& filename:{"drawing.svg","drawing.pdf","exploded.dxf"})
    require(content(rendered,filename).find("EXPLODED")!=std::string::npos,std::string(filename)+" visibly labels exploded geometry");
  require(content(rendered,"assembled.dxf").find("EXPLODED")==std::string::npos,"Assembled DXF is not labeled exploded");
  require(built.summary()==initial_summary && built.topology()==initial_topology,"Exploded projection does not mutate base geometry or topology");
  auto horizontal=normalized;horizontal["views"][1]["explode"][0]["translation"]={30,0,0};
  const auto wide=built.drawing(projection_request(horizontal)).at("views")[1].at("bounds_mm");
  near(wide[2].get<double>()-wide[0].get<double>(),42);
  auto section=recipe();section["views"][1]["orientation"]="section";section["views"][1]["section"]={{"axis","y"},{"offset",5}};
  const auto normalized_section=normalize_drawing(section,source);
  const auto section_render=render_drawing(built.drawing(projection_request(normalized_section)),normalized_section,identity());
  dimensions(section_render,8,20);
  require(content(section_render,"exploded.dxf").find("\n8\nHATCH\n")!=std::string::npos,"Exploded sections retain entities on the material hatching layer");
}
void cache_tests() {
  Temp temp;const auto source=model();auto drawing=recipe();Json cold,warm;
  const auto first=evaluate_model(temp.path,source,worker_request(source,drawing),&cold);
  require(!cold.at("geometry_hit") && !cold.at("projection_hit"),"Cold assembly drawing builds and projects");
  require(cold.at("projection_keys")[0]!=cold.at("projection_keys")[1],"Assembled and exploded views have different projection keys");
  const auto again=evaluate_model(temp.path,source,worker_request(source,drawing),&warm);
  require(warm.at("geometry_hit") && warm.at("projection_hit"),"Warm assembly drawing restores both caches");
  require(test::geometry_equivalent(first.at("summary"),again.at("summary")),"Warm assembly measurements and ownership match cold geometry");
  require(first.at("drawing")==again.at("drawing"),"Warm assembly artifacts and dimensions match cold bytes");
  drawing["views"][1]["explode"][0]["translation"][2]=15;
  dimensions(evaluate_model(temp.path,source,worker_request(source,drawing),&warm).at("drawing"),8,23);
  require(warm.at("geometry_hit") && !warm.at("projection_hit"),"Changed explode offset invalidates projections only");
  drawing["views"][1].erase("explode");
  dimensions(evaluate_model(temp.path,source,worker_request(source,drawing),&warm).at("drawing"),8,8);
  // Projections are cached per view, so a view without its explode offset is the
  // same projection as the assembled view and is reused rather than repeated.
  require(warm.at("projection_hit") && warm.at("projection_keys")[0]==warm.at("projection_keys")[1],
    "A view without an explode offset reuses the assembled view's projection");
  auto changed=source;changed["parameters"]["thickness"]=4;
  dimensions(evaluate_model(temp.path,changed,worker_request(changed,recipe()),&warm).at("drawing"),10,22);
  require(!warm.at("geometry_hit") && !warm.at("projection_hit"),"Part and mate edits invalidate assembly caches");
  fs::remove_all(temp.path/".cache");
  require(evaluate_model(temp.path,source,worker_request(source,recipe()),&warm)==first,"Cache deletion preserves exact regenerated artifacts");
}
void persistence_tests() {
  Temp temp;Service service(temp.path);const auto source=model();
  service.call("cad_create",{{"document_id","assembly"},{"model",source}});
  const auto original=service.call("cad_read",{{"document_id","assembly"}});
  const auto first=service.call("cad_drawing",{{"document_id","assembly"},{"revision",1},{"drawing",recipe()}});
  dimensions(first,8,20);
  const auto saved=parse_json(read_text(path_from_utf8(first.at("recipe_path"))));
  require(saved.at("drawing")==recipe(),"Saved recipe preserves parameterized explode offsets");
  require(saved.at("resolved_drawing").at("views")[1].at("explode")[0].at("translation")[2]==12,"Saved recipe retains resolved explode evidence");
  std::map<fs::path,std::string> files;
  for(const auto& artifact:first.at("artifacts")) {const auto path=path_from_utf8(artifact.at("path"));files[path]=read_text(path);}
  require(service.call("cad_read",{{"document_id","assembly"}})==original,"Drawing leaves committed assembly source unchanged");
  auto bad=recipe();bad["views"][1]["explode"][0]["part_id"]="missing";
  fails("invalid_drawing",[&]{service.call("cad_drawing",{{"document_id","assembly"},{"revision",1},{"drawing",bad}});});
  require(service.call("cad_read",{{"document_id","assembly"}})==original,"Failed exploded drawing preserves HEAD");
  service.call("cad_apply",{{"document_id","assembly"},{"expected_revision",1},{"operations",Json::array({
    {{"op","set_parameter"},{"name","thickness"},{"value",4}},{{"op","set_parameter"},{"name","gap"},{"value",18}}})}});
  Service reopened(temp.path);
  const auto regenerated=reopened.call("cad_drawing",{{"document_id","assembly"},{"revision",2},{"drawing",saved.at("drawing")}});
  dimensions(regenerated,10,28);
  for(const auto& [path,bytes]:files) require(read_text(path)==bytes,"Regeneration preserves historical drawing bytes");
  require(read_text(path_from_utf8(first.at("recipe_path")))==saved.dump(2)+"\n","Original parameterized recipe remains immutable");
  require(reopened.call("cad_read",{{"document_id","assembly"},{"revision",1}})==original,"Historical assembly source remains immutable");
}
void example_tests() {
  const auto source=parse_json(read_text(fs::path(CAD_SOURCE_DIR)/"examples/assembly.create.json")).at("model");
  const auto drawing=parse_json(read_text(fs::path(CAD_SOURCE_DIR)/"examples/assembly.drawing.json")).at("drawing");
  validate_model(source);BuiltModel built(source);const auto normalized=normalize_drawing(drawing,source);
  const auto rendered=render_drawing(built.drawing(projection_request(normalized)),normalized,identity());
  near(rendered.at("dimensions")[0].at("value_mm"),60);near(rendered.at("dimensions")[1].at("value_mm"),24);near(rendered.at("dimensions")[2].at("value_mm"),48);
  require(rendered.at("files").size()==6,"Example emits four DXFs and PDF/SVG sheets");
}
}
int main() {
  try {configure_kernel_logging();set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));validation_tests();geometry_tests();cache_tests();persistence_tests();example_tests();
    std::cout<<"assembly_drawing: "<<checks<<" checks passed\n";return 0;
  } catch(const std::exception& e) {std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;}
}
