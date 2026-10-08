#include "agentcad/bom.hpp"
#include "agentcad/cache.hpp"
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
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <thread>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
Error fails(const std::string& code,const std::function<void()>& action){try{action();}catch(const Error& e){require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what());return e;}throw std::runtime_error("Expected "+code);}
struct Temp{fs::path root;Temp(){root=temporary_file(fs::temp_directory_path());fs::remove(root);directory(root);}~Temp(){std::error_code ignored;fs::remove_all(root,ignored);}};
Json box(){return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},
  {"features",Json::array({{{"id","body"},{"type","box"},{"size",{10,4,2}}}})},{"output","body"}};}
Json purchase(){return {{"supplier","=Fixture supplier"},{"part_number","BLOCK-10"},{"source_url","https://example.invalid/parts/BLOCK-10"}};}
Json job(Service& service,const Json& args){
  for(int attempt=0;attempt<1000;++attempt){try{return service.call("cad_job",args);}catch(const Error& e){if(e.code!="workspace_busy")throw;}std::this_thread::sleep_for(std::chrono::milliseconds(2));}
  throw std::runtime_error("Job metadata remained busy");
}
Json terminal(Service& service,const std::string& id){
  for(int attempt=0;attempt<2000;++attempt){auto value=job(service,{{"action","get"},{"job_id",id}});const auto state=value.at("state");
    if(state!="queued"&&state!="running"&&state!="cancelling")return value;std::this_thread::sleep_for(std::chrono::milliseconds(3));}
  throw std::runtime_error("Purchased-part job did not terminate");
}
void readback(const fs::path& path,double expected){
  STEPControl_Reader reader;reader.SetShapeProcessFlags(ShapeProcess::OperationsFlags{});
  require(reader.ReadFile(path_to_utf8(path).c_str())==IFSelect_RetDone&&reader.TransferRoots()==reader.NbRootsForTransfer(),"Independent STEP reader transfers every root");
  require(BRepCheck_Analyzer(reader.OneShape()).IsValid(),"Independent STEP source remains valid");
  GProp_GProps volume;BRepGProp::VolumeProperties(reader.OneShape(),volume);
  require(std::abs(volume.Mass()-expected)<1e-6,"Independent exact source/export volume matches");
}
void workflow(){
  Temp temporary;Service service(temporary.root);const auto file=temporary.root/"fixture.step";BuiltModel(box()).export_file(file,"step");
  const auto bytes=read_text(file),digest=sha256(bytes);auto identity=purchase();identity["artifact_sha256"]=digest;
  Json args={{"document_id","library"},{"path",path_to_utf8(file)},{"expected_sha256",digest},{"purchase",purchase()},{"request_id","import_once"}};
  const auto imported=service.call("cad_import",args);const auto model=imported.at("model");
  require(model.at("features")[0].at("content")==bytes&&model.at("features")[0].at("sha256")==digest,"Import preserves exact source bytes/hash");
  require(model.at("features")[0].at("purchase")==identity,"Native importer binds caller supplier identity to measured artifact hash");
  require(std::abs(imported.at("summary").at("volume_mm3").get<double>()-80)<1e-6,"Purchased import preserves exact geometry within integration tolerance");
  readback(file,80);
  auto invalid=args;invalid.erase("request_id");invalid["document_id"]="mismatch";invalid["expected_sha256"]=std::string(64,'a');
  const auto mismatch=fails("artifact_mismatch",[&]{service.call("cad_import",invalid);});
  require(mismatch.details.at("actual_sha256")==digest,"Hash mismatch returns measured identity");
  fails("not_found",[&]{service.call("cad_read",{{"document_id","mismatch"}});});
  invalid.erase("expected_sha256");invalid["purchase"]["artifact_sha256"]=std::string(64,'a');
  fails("artifact_mismatch",[&]{service.call("cad_import",invalid);});
  for(const auto& bad:Json::array({Json{{"source_url","file:///tmp/source"}},Json{{"supplier",""}},Json{{"artifact_sha256","broken"}},Json{{"unexpected",true}}})){
    invalid=args;invalid["document_id"]="bad_metadata";invalid["purchase"].update(bad);
    fails("invalid_argument",[&]{service.call("cad_import",invalid);});
  }
  invalid=args;invalid["document_id"]="bad_digest";invalid["expected_sha256"]="broken";
  fails("invalid_argument",[&]{service.call("cad_import",invalid);});
  fs::remove(file);
  require(service.call("cad_import",args)==imported,"Receipt replay preserves imported identity after original file removal");
  require(service.call("cad_read",{{"document_id","library"},{"revision",1}}).at("model")==model,"Saved import remains self-contained");
  auto damaged=model;damaged["features"][0]["purchase"].erase("artifact_sha256");
  fails("invalid_model",[&]{validate_model(damaged);});
  damaged=model;damaged["features"][0]["purchase"]["artifact_sha256"]=std::string(64,'b');
  const auto bad_model=fails("invalid_model",[&]{validate_model(damaged);});
  require(bad_model.details.at("feature_id")=="imported","Bad purchasing identity is a feature-level error");

  auto nested=model;
  nested["features"].push_back({{"id","positioned"},{"type","transform"},{"input","imported"},{"translation",{3,0,0}}});
  nested["features"].push_back({{"id","copy"},{"type","instance"},{"input","positioned"}});
  nested["features"].push_back({{"id","machined"},{"type","hole"},{"input","imported"},{"origin",{5,2,0}},{"axis",{0,0,1}},{"radius",.5},{"depth",2}});
  nested["features"].push_back({{"id","child"},{"type","assembly"},{"parts",Json::array({{{"id","a"},{"input","imported"}},{{"id","b"},{"input","copy"},{"placement",{{"translation",{20,0,0}}}}}})}});
  nested["features"].push_back({{"id","machine"},{"type","assembly"},{"parts",Json::array({{{"id","left"},{"input","child"}},{{"id","right"},{"input","child"},{"placement",{{"translation",{0,20,0}}}}},{{"id","modified"},{"input","machined"},{"placement",{{"translation",{40,0,0}}}}}})}});
  nested["output"]="machine";validate_model(nested);
  require(imported_step_source(nested,"copy")->at("id")=="imported","Rigid copy chain resolves exact imported source");
  require(imported_step_source(nested,"machined")==nullptr,"Changed geometry does not inherit an unchanged purchased-part identity");
  const auto inventory=build_bom(nested);
  for(const auto& item:inventory.at("items")){
    if(item.at("input")=="machined")require(!item.contains("purchase"),"Machined part is not silently sold as the original purchase");
    else require(item.at("purchase")==identity&&item.at("quantity")==2,"Nested source/copy BOM derives identity and repeated quantity");
  }
  for(const auto& node:inventory.at("structure"))if(node.at("kind")=="part"&&node.at("input")!="machined")
    require(node.at("bom").at("purchase")==identity,"Hierarchy exposes imported purchasing provenance without duplicated intent");
  require(bom_csv(inventory).find("\"'=Fixture supplier\"")!=std::string::npos,"Derived purchasing CSV retains formula neutralization");
  auto conflicting=nested;conflicting["features"][4]["bom"]=Json::array({{{"input","copy"},{"purchase",purchase()}}});
  fails("invalid_model",[&]{validate_model(conflicting);});
  conflicting["features"][4]["bom"][0]["purchase"]=identity;validate_model(conflicting);++checks;
  auto updated=nested;updated["features"][0]["purchase"]["supplier"]="New declared supplier";
  require(feature_cache_keys(updated)==feature_cache_keys(nested),"Purchasing-only edits preserve exact geometry dependency keys");
  const BuiltModel cold(nested),warm(updated,cold.snapshot());
  require(test::geometry_equivalent(cold.summary(),warm.summary()),"Cached exact geometry survives purchasing-only edits");
  require(build_bom(updated).at("items")[0].at("purchase").at("supplier")=="New declared supplier","BOM uses current intent after geometry cache reuse");
  service.call("cad_create",{{"document_id","nested"},{"model",nested}});
  const auto before=service.call("cad_read",{{"document_id","nested"}});
  fails("invalid_model",[&]{service.call("cad_apply",{{"document_id","nested"},{"expected_revision",1},{"operations",Json::array({
    {{"op","set_bom_item"},{"assembly_id","child"},{"item",{{"input","copy"},{"purchase",purchase()}}}}})}});});
  require(service.call("cad_read",{{"document_id","nested"}})==before,"Conflicting provenance edit preserves HEAD and history");

  const auto package=service.call("cad_manufacture",{{"document_id","nested"},{"revision",1},{"options",{{"drawings",false}}}});
  const auto directory=path_from_utf8(package.at("directory"));const auto manifest=parse_json(read_text(directory/"manifest.json"));
  for(const auto& part:manifest.at("parts")){
    if(part.at("input")=="machined"){require(!part.contains("purchase")&&!part.contains("source_artifact"),"Modified package part does not imply an unchanged purchased source");continue;}
    const auto original=directory/path_from_utf8(part.at("source_artifact").at("path"));
    require(read_text(original)==bytes&&sha256(read_text(original))==text_field(part.at("source_artifact"),"sha256"),"Package preserves original supplier STEP bytes/hash beside its independent exports");
    require(part.at("purchase")==identity&&part.at("source_artifact").at("source_feature_id")=="imported","Package links supplier identity, actual hash and source feature");
    readback(original,80);readback(directory/path_from_utf8(part.at("directory"))/"part.step",80);
  }
  for(const auto& artifact:manifest.at("artifacts")){
    const auto raw=read_text(directory/path_from_utf8(artifact.at("path")),64*1024*1024);
    require(raw.size()==artifact.at("bytes")&&sha256(raw)==text_field(artifact,"sha256"),"Independent package manifest hashes/bytes match all artifacts");
  }
  const auto direct=service.call("cad_manufacture",{{"document_id","library"},{"revision",1},{"options",{{"drawings",false}}}});
  const auto direct_manifest=parse_json(read_text(path_from_utf8(direct.at("directory"))/"manifest.json"));
  require(direct_manifest.at("parts")[0].at("purchase")==identity&&direct_manifest.at("parts")[0].contains("source_artifact"),"Standalone imported part also exports its purchasing identity and original artifact");

  service.call("cad_create",{{"document_id","consumer"},{"model",box()}});
  const Json consumer_assembly={{"id","assembly"},{"type","assembly"},{"parts",Json::array({
    {{"id","one"},{"input","bought"}},{{"id","two"},{"input","bought"},{"placement",{{"translation",{20,0,0}}}}}})}};
  auto captured=service.call("cad_apply",{{"document_id","consumer"},{"expected_revision",1},{"operations",Json::array({
    {{"op","set_component"},{"id","bought"},{"source_document_id","library"},{"source_revision",1}},
    {{"op","add_feature"},{"feature",consumer_assembly}},
    {{"op","set_output"},{"feature_id","assembly"}}})}});
  require(build_bom(captured.at("model")).at("items")[0].at("purchase")==identity,"Pinned component capture retains imported provenance through ID remapping");
  auto new_source=model.at("features")[0];new_source["purchase"]["supplier"]="New declared supplier";
  service.call("cad_apply",{{"document_id","library"},{"expected_revision",1},{"operations",Json::array({{{"op","replace_feature"},{"id","imported"},{"feature",new_source}}})}});
  require(build_bom(service.call("cad_read",{{"document_id","consumer"}}).at("model")).at("items")[0].at("purchase")==identity,"Source supplier updates do not silently change a pinned consuming revision");
  Temp portable;Service independent(portable.root);
  const auto copied=independent.call("cad_create",{{"document_id","portable"},{"model",captured.at("model")}});
  require(test::geometry_equivalent(copied.at("summary"),captured.at("summary")),"Purchased component rebuilds without source file or library");
  require(build_bom(copied.at("model")).at("items")[0].at("purchase")==identity,"Portable captured source preserves its recorded supplier and artifact identity");
  require(service.call("cad_read",{{"document_id","library"},{"revision",1}}).at("model")==model,"Historical purchasing identity remains immutable after explicit source edits");

  atomic_text(file,bytes);args["document_id"]="async_import";args.erase("request_id");
  const Json submit={{"action","submit"},{"tool","cad_import"},{"arguments",args},{"request_id","purchased_async"}};
  const auto queued=job(service,submit),done=terminal(service,text_field(queued,"job_id"));
  require(done.at("state")=="succeeded"&&done.at("result").at("model").at("features")[0].at("purchase")==identity,"Native job importer binds the same supplier/hash provenance");
  fs::remove(file);require(job(service,submit).at("job_id")==queued.at("job_id"),"Job receipt replays the purchased import without rereading deleted source");
  atomic_text(file,bytes);auto bad_submit=submit;bad_submit["request_id"]="purchased_failed";bad_submit["arguments"]["document_id"]="async_mismatch";bad_submit["arguments"]["expected_sha256"]=std::string(64,'a');
  const auto rejected=terminal(service,text_field(job(service,bad_submit),"job_id"));
  require(rejected.at("state")=="failed"&&rejected.at("error").at("code")=="artifact_mismatch","Async checksum failure is terminal structured evidence");
  fails("not_found",[&]{service.call("cad_read",{{"document_id","async_mismatch"}});});
  require(service.call("cad_read",{{"document_id","nested"}})==before,"All imports/packages/jobs preserve unrelated assembly HEAD and history");
}
}
int main(){try{set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));workflow();std::cout<<checks<<" purchased-part checks passed\n";return 0;}
  catch(const std::exception& e){std::cerr<<"Purchased-part failure: "<<e.what()<<'\n';return 1;}}
