#include "mutation_test_support.hpp"
#include "agentcad/manufacturing.hpp"
#include "agentcad/bom.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include "geometry_equivalence.hpp"
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <thread>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
struct Temp {fs::path root;Temp(){root=temporary_file(fs::temp_directory_path());fs::remove(root);directory(root);}~Temp(){std::error_code ignored;fs::remove_all(root,ignored);}};
void fails(const std::string& code,const std::function<void()>& action) {
  try{action();require(false,"Expected "+code);}catch(const Error& error){require(error.code==code,"Expected "+code+", got "+error.code+": "+error.what());}
}
Json purchase(){return {{"supplier","=Test supplier"},{"part_number","R-2"},{"source_url","https://example.invalid/parts/R-2"},{"artifact_sha256",std::string(64,'a')}};}
Json model(){
  auto result=parse_json(read_text(fs::path(CAD_SOURCE_DIR)/"examples/nested-assembly.create.json")).at("model");
  result["features"][2]["bom"][1]["purchase"]=purchase();return result;
}
std::set<std::string> exports(const fs::path& root) {
  std::set<std::string> names;
  for(const auto& entry:fs::directory_iterator(root/"exports"))names.insert(path_to_utf8(entry.path().filename()));return names;
}
Json job(Service& service,const Json& args) {
  for(int attempt=0;attempt<1000;++attempt) {
    try{return service.call("cad_job",args);}catch(const Error& error){if(error.code!="workspace_busy")throw;}
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  throw std::runtime_error("Job admission remained busy");
}
Json terminal(Service& service,const std::string& id) {
  for(int attempt=0;attempt<4000;++attempt) {
    auto result=job(service,{{"action","get"},{"job_id",id}});const auto state=result.at("state");
    if(state!="queued"&&state!="running"&&state!="cancelling")return result;
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
  }
  throw std::runtime_error("Manufacturing job did not finish");
}
void step(const fs::path& path,const Json& expected) {
  STEPControl_Reader reader;reader.SetShapeProcessFlags(ShapeProcess::OperationsFlags{});
  require(reader.ReadFile(path_to_utf8(path).c_str())==IFSelect_RetDone&&reader.TransferRoots()==reader.NbRootsForTransfer(),"Independent STEP reader transfers every root");
  const auto shape=reader.OneShape();require(BRepCheck_Analyzer(shape).IsValid(),"Independent package STEP is valid");
  NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> solids;TopExp::MapShapes(shape,TopAbs_SOLID,solids);
  require(solids.Extent()==expected.at("solid_count"),"Package STEP retains all exact solids");
  GProp_GProps volume;BRepGProp::VolumeProperties(shape,volume);
  require(std::abs(volume.Mass()-expected.at("volume_mm3").get<double>())<1e-6,"Package STEP matches source-coordinate volume");
}
void stl(const fs::path& path,const Json& summary) {
  const auto bytes=read_text(path,64*1024*1024);
  const auto u32=[&](std::size_t offset){std::uint32_t value=0;for(int i=3;i>=0;--i)value=(value<<8)|static_cast<unsigned char>(bytes.at(offset+i));return value;};
  const auto f32=[&](std::size_t offset){const auto bits=u32(offset);float value;std::memcpy(&value,&bits,4);return static_cast<double>(value);};
  require(bytes.size()>=84,"Binary package STL has a complete header");const auto count=u32(80);
  require(count>0&&bytes.size()==84+50*static_cast<std::size_t>(count),"Independent STL parser verifies every triangle record");
  using Point=std::array<long long,3>;std::map<std::array<Point,2>,int> edges;
  std::array<double,3> lo={1e100,1e100,1e100},hi={-1e100,-1e100,-1e100};
  for(std::size_t triangle=0;triangle<count;++triangle) {
    std::array<Point,3> points;
    for(int vertex=0;vertex<3;++vertex)for(int axis=0;axis<3;++axis) {
      const auto value=f32(84+50*triangle+12+12*vertex+4*axis);require(std::isfinite(value),"STL positions remain finite");
      lo[axis]=std::min(lo[axis],value);hi[axis]=std::max(hi[axis],value);points[vertex][axis]=std::llround(value*100000);
    }
    for(int edge=0;edge<3;++edge){std::array<Point,2> pair={points[edge],points[(edge+1)%3]};if(pair[1]<pair[0])std::swap(pair[0],pair[1]);++edges[pair];}
  }
  for(const auto& [edge,count]:edges)require(count==2,"Package part mesh is watertight at 0.00001 mm quantization");
  for(int axis=0;axis<3;++axis)require(std::abs(lo[axis]-summary.at("bounds_mm").at("min")[axis].get<double>())<0.0001&&
    std::abs(hi[axis]-summary.at("bounds_mm").at("max")[axis].get<double>())<0.0001,"Part STL stays in original source coordinates");
}
void package() {
  Temp temporary;Service service(temporary.root);const auto source=model();
  service.call("cad_create",{{"document_id","source"},{"model",source}});const auto original=service.call("cad_read",{{"document_id","source"}});
  const Json recipe={{"formats",{"svg","pdf","dxf"}},{"views",Json::array({{{"id","front"},{"orientation","front"}}})},
    {"dimensions",Json::array({{{"view","front"},{"kind","width"}}})}};
  const Json options={{"part_drawing",recipe},{"parts",Json::array({{{"feature_id","block"},{"process","cnc"},{"material","Aluminum"},{"notes",{"Stock and tolerances require approval"}}},
    {{"feature_id","rod"},{"process","purchased"}}})}};
  const auto result=service.call("cad_manufacture",{{"document_id","source"},{"revision",1},{"options",options}});
  const auto root=path_from_utf8(text_field(result,"directory"));const auto manifest=parse_json(read_text(root/"manifest.json",64*1024*1024),64*1024*1024);
  require(manifest.at("parts").size()==2&&result.at("part_count")==2,"Repeated nested instances export only two unique leaf sources");
  require(manifest.at("occurrences").size()==5,"Every physical occurrence retains its saved assembly transform");
  require(manifest.at("source").at("model_sha256")==sha256(source.dump())&&result.at("model_sha256")==sha256(source.dump()),"Portable manifest binds exact editable source");
  require(parse_json(read_text(root/"source.json")).at("model")==source,"Saved package includes complete editable intent");
  require(manifest.at("process_review").at("status")=="not_evaluated","Artifact export cannot claim manufacturing validation");
  std::uintmax_t total=fs::file_size(root/"manifest.json");
  std::map<std::string,std::string> hashes;
  for(const auto& artifact:manifest.at("artifacts")) {
    const auto relative=path_from_utf8(text_field(artifact,"path"));require(!relative.is_absolute()&&path_to_utf8(relative).find("..") ==std::string::npos,"Manifest artifact paths are portable and contained");
    const auto content=read_text(root/relative,64*1024*1024);total+=content.size();
    require(content.size()==artifact.at("bytes")&&sha256(content)==text_field(artifact,"sha256"),"Every portable artifact verifies by bytes and SHA-256");
    hashes[path_to_utf8(relative)]=sha256(content);
  }
  require(total==result.at("bytes")&&hashes.size()==result.at("artifact_count"),"Package aggregate size includes its manifest");
  for(const auto& part:manifest.at("parts")) {
    const auto id=text_field(part,"feature_id");const auto path=root/path_from_utf8(text_field(part,"directory"));
    const auto expected=BuiltModel(source).summary(id);
    require(test::geometry_equivalent(part.at("summary"),expected),"Part measurements describe unposed source geometry");
    step(path/"part.step",expected);stl(path/"part.stl",expected);
    const auto drawing=parse_json(read_text(root/path_from_utf8(text_field(part,"drawing_recipe"))));
    require(drawing.at("feature_id")==id&&drawing.at("dimensions")[0].at("value_mm")== (id=="block"?10:6),"Per-part drawing dimensions bind the actual source feature");
    if(id=="rod")require(part.at("purchase")==purchase()&&part.at("quantity")==2,"Purchasing identity survives source roll-up and package export");
    else require(part.at("process")=="cnc"&&part.at("material")=="Aluminum"&&part.at("quantity")==3,"Explicit process/material assumptions and leaf quantities are preserved");
  }
  require(read_text(root/"bom.csv").find("\"'=Test supplier\"")!=std::string::npos,"Purchasing CSV neutralizes formula-like supplier text");
  step(root/"assembly/assembly.step",BuiltModel(source).summary());
  require(service.call("cad_read",{{"document_id","source"}})==original,"Manufacturing preserves HEAD and saved intent");
  Temp relocated;const auto copy=relocated.root/"copied-package";fs::copy(root,copy,fs::copy_options::recursive);
  for(const auto& [name,hash]:hashes)require(sha256(read_text(copy/path_from_utf8(name),64*1024*1024))==hash,"Relocated package remains independently verifiable");
  Service portable(relocated.root/"workspace");const auto restored=portable.call("cad_create",{{"document_id","restored"},{"model",parse_json(read_text(copy/"source.json")).at("model")}});
  require(test::geometry_equivalent(restored.at("summary"),BuiltModel(source).summary()),"Package source rebuilds in a different workspace without its library");
  const auto before=exports(temporary.root);
  auto bad=options;bad["parts"][0]["drawing"]={{"views",Json::array({{{"id","top"},{"orientation","top"}}})},
    {"dimensions",Json::array({{{"view","top"},{"kind","radius"},{"center",{100,100}},{"radius",2}}})}};
  fails("drawing_reference_not_found",[&]{service.call("cad_manufacture",{{"document_id","source"},{"revision",1},{"options",bad}});});
  require(exports(temporary.root)==before,"Failed part drawing leaves no partial published package or stage");
  require(service.call("cad_read",{{"document_id","source"}})==original,"Failed package preserves source history");
  for(const auto& [name,hash]:hashes)require(sha256(read_text(root/path_from_utf8(name),64*1024*1024))==hash,"Failed later generation leaves historical artifacts intact");
  service.call("cad_apply",{{"document_id","source"},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","angle"},{"value",30}}})}});
  const Json submission={{"action","submit"},{"request_id","historical_package"},{"tool","cad_manufacture"},
    {"arguments",{{"document_id","source"},{"revision",1},{"feature_id","rod"},{"options",{{"drawings",false},{"formats",{"step"}}}}}}};
  job(service,submission);const auto finished=terminal(service,"historical_package");
  require(finished.at("state")=="succeeded"&&finished.at("result").at("revision")==1&&finished.at("result").at("part_count")==1,"Async manufacturing supports historical scoped single parts");
  require(job(service,submission).at("result")==finished.at("result"),"Completed package job replay returns its original generation");
  require(service.call("cad_read",{{"document_id","source"}}).at("revision")==2,"Historical package never changes current HEAD");
}
void validation() {
  for(const auto& options:Json::array({Json{{"formats",{"step","step"}}},Json{{"drawings",false},{"part_drawing",Json::object()}},
      Json{{"parts",Json::array({{{"feature_id","x"},{"process","magic"}}})}},Json{{"parts",Json::array({{{"feature_id","x"}},{{"feature_id","x"}}})}},Json{{"command","rm"}}}))
    fails("invalid_argument",[&]{validate_manufacturing_options(options);});
  auto invalid=model();invalid["features"][2]["bom"][1]["purchase"]["source_url"]="file:///tmp/part";fails("invalid_model",[&]{validate_model(invalid);});
  invalid=model();invalid["features"][2]["bom"][1]["purchase"]["artifact_sha256"]="broken";fails("invalid_model",[&]{validate_model(invalid);});
  Temp temporary;Service service(temporary.root);service.call("cad_create",{{"document_id","source"},{"model",model()}});
  const auto before=exports(temporary.root);
  fails("invalid_argument",[&]{service.call("cad_manufacture",{{"document_id","source"},{"revision",1},{"options",{{"parts",Json::array({{{"feature_id","module"}}})}}}});});
  require(exports(temporary.root)==before,"Invalid non-leaf assumptions create no package");
}
void pinned() {
  Temp temporary;Service service(temporary.root);
  service.call("cad_create",{{"document_id","library"},{"model",model()}});
  const auto seed=parse_json(read_text(fs::path(CAD_SOURCE_DIR)/"examples/component-assembly.create.json"));service.call("cad_create",seed);
  auto capture=parse_json(read_text(fs::path(CAD_SOURCE_DIR)/"examples/component-assembly.edit.json"));capture["operations"][0]["source_document_id"]="library";
  const auto imported=service.call("cad_apply",capture);
  const auto result=service.call("cad_manufacture",{{"document_id",seed.at("document_id")},{"revision",2},{"options",{{"drawings",false}}}});
  const auto root=path_from_utf8(text_field(result,"directory"));const auto saved=parse_json(read_text(root/"source.json"));
  require(saved.at("model")==test::receipt_source(service,imported)&&saved.at("model").at("components")[0].at("source").at("revision")==1,"Package captures exact historical component source pins");
  Temp independent;Service portable(independent.root);
  const auto restored=portable.call("cad_create",{{"document_id","portable"},{"model",saved.at("model")}});
  require(test::geometry_equivalent(restored.at("summary"),imported.at("summary")),"Pinned package rebuilds without the component library");
  const auto manifest=parse_json(read_text(root/"manifest.json"));
  require(manifest.at("parts").size()==3,"Pinned package exports base and two unique component leaves");
  bool found=false;for(const auto& part:manifest.at("parts"))if(part.contains("purchase")){found=true;require(part.at("purchase")==purchase(),"Component remapping preserves supplier identity");}
  require(found,"Pinned package retains nested purchasing metadata");
}
void cancelled() {
  Temp temporary;Service service(temporary.root);Json source={{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",Json::array()},{"output","assembly"}};
  Json parts=Json::array();
  for(int i=0;i<40;++i){const auto id="box_"+std::to_string(i);source["features"].push_back({{"id",id},{"type","box"},{"size",{5+i,8,2}}});parts.push_back({{"id",id},{"input",id},{"placement",{{"translation",{i*50,0,0}}}}});}
  source["features"].push_back({{"id","assembly"},{"type","assembly"},{"parts",parts}});
  service.call("cad_create",{{"document_id","cancel_source"},{"model",source}});const auto before=exports(temporary.root);
  job(service,{{"action","submit"},{"request_id","cancel_package"},{"tool","cad_manufacture"},{"arguments",{{"document_id","cancel_source"},{"revision",1}}}});
  bool written=false;
  for(int attempt=0;attempt<2000&&!written;++attempt) {
    for(const auto& entry:fs::directory_iterator(temporary.root/"exports"))if(fs::exists(entry.path()/"parts/part-1/part.step"))written=true;
    if(!written)std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  require(written&&job(service,{{"action","get"},{"job_id","cancel_package"}}).at("state")=="running","Cancellation reaches a live job after native artifact generation begins");
  job(service,{{"action","cancel"},{"job_id","cancel_package"}});
  require(terminal(service,"cancel_package").at("state")=="cancelled","Manufacturing worker cancellation becomes terminal");
  require(exports(temporary.root)==before,"Cancelled manufacturing removes its private generation completely");
  require(service.call("cad_read",{{"document_id","cancel_source"}}).at("model")==source,"Cancellation preserves the complete source document");
}
}
int main(){try{configure_kernel_logging();set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));package();validation();pinned();cancelled();std::cout<<checks<<" manufacturing checks passed\n";return 0;}
catch(const Error& error){std::cerr<<error.code<<": "<<error.what()<<" "<<error.details.dump()<<'\n';return 1;}
catch(const std::exception& error){std::cerr<<"FAILED: "<<error.what()<<'\n';return 1;}}
