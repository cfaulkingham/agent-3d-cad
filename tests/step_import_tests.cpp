#include "mutation_test_support.hpp"
#include "agentcad/artifact_review.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/service.hpp"
#include "agentcad/model.hpp"
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <thread>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message) { ++checks;if(!value)throw std::runtime_error(message); }
void fails(const std::string& code,const std::function<void()>& action) {
  try { action(); } catch(const Error& e) { require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what());return; }
  throw std::runtime_error("Expected "+code);
}
struct Temp {
  fs::path root=temporary_directory(fs::canonical(fs::temp_directory_path()));
  ~Temp(){std::error_code ignored;fs::remove_all(root,ignored);}
};
Json box() {return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},
  {"features",Json::array({{{"id","body"},{"type","box"},{"size",{10,4,2}}}})},{"output","body"}};}
std::string source(const fs::path& path,std::size_t minimum) {
  BuiltModel(box()).export_file(path,"step");auto raw=read_text(path);
  // Legal STEP comments grow the actual source without making this a slow
  // geometry-complexity benchmark. Newlines/quotes also exercise JSON escaping.
  std::string comments;const std::string line="/* \"source padding\" "+std::string(4000,'x')+" */\n";
  while(raw.size()+comments.size()<=minimum)comments+=line;
  raw.insert(raw.find("DATA;"),comments);atomic_text(path,raw,unlimited_bytes);return raw;
}
void volume(const Json& summary) {require(summary.at("valid")==true&&std::abs(summary.at("volume_mm3").get<double>()-80)<1e-6,"Exact solid volume is preserved");}
Json job(Service& service,const Json& args) {
  for(int i=0;i<1000;++i) {
    try{return service.call("cad_job",args);}catch(const Error& e){if(e.code!="workspace_busy")throw;}
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  throw std::runtime_error("Job metadata remained busy");
}
void lifecycle() {
  Temp temp;const auto file=temp.root/"large.step";const auto raw=source(file,2*1024*1024),hash=sha256(raw);
  const auto workspace=temp.root/"workspace";Service service(workspace);
  Json args={{"document_id","imported"},{"path",path_to_utf8(file)},{"expected_sha256",hash},{"request_id","once"}};
  auto bad=args;bad["document_id"]="mismatch";bad["expected_sha256"]=std::string(64,'0');
  fails("artifact_mismatch",[&]{service.call("cad_import",bad);});
  fails("not_found",[&]{service.call("cad_read",{{"document_id","mismatch"}});});
  const auto imported=service.call("cad_import",args);volume(imported.at("summary"));const auto imported_source=test::receipt_source(service,imported);
  require(imported_source.at("features")[0].at("content")==raw,"Import preserves all source bytes above 1 MiB");
  require(fs::file_size(workspace/"documents/imported/revisions/1.json")>max_json_bytes,"Saved revision crosses old storage cap");
  fs::remove(file);fs::remove_all(workspace/".cache");Service reopened(workspace);
  require(reopened.call("cad_import",args)==imported,"Receipt replay survives source deletion and reopen");
  require(reopened.call("cad_read",{{"document_id","imported"}}).at("model")==imported_source,"Reopen preserves the large embedded model");
  volume(reopened.call("cad_query",{{"document_id","imported"},{"revision",1}}).at("summary"));
  const auto changed=reopened.call("cad_apply",{{"document_id","imported"},{"expected_revision",1},{"operations",Json::array({
    {{"op","add_feature"},{"feature",{{"id","moved"},{"type","transform"},{"input","imported"},{"translation",{5,0,0}}}}},
    {{"op","set_output"},{"feature_id","moved"}}})},{"request_id","edit"}});
  volume(changed.at("summary"));require(std::abs(changed.at("summary").at("bounds_mm").at("min")[0].get<double>()-5)<1e-5,"Large import supports subsequent geometry edits");
  auto broken=imported_source.at("features")[0];broken["content"]=std::string(2*1024*1024,'x');broken["sha256"]=sha256(broken.at("content").get<std::string>());
  fails("kernel_failure",[&]{reopened.call("cad_apply",{{"document_id","imported"},{"expected_revision",2},{"operations",Json::array({{{"op","replace_feature"},{"id","imported"},{"feature",broken}}})}});});
  require(reopened.call("cad_read",{{"document_id","imported"}}).at("revision")==2,"Invalid large STEP edit leaves HEAD unchanged");
  volume(reopened.call("cad_restore",{{"document_id","imported"},{"expected_revision",2},{"source_revision",1}}).at("summary"));
  const auto exported=reopened.call("cad_export",{{"document_id","imported"},{"revision",3},{"format","step"}});
  volume(reopened.call("cad_import",{{"document_id","roundtrip"},{"path",exported.at("path")}}).at("summary"));
  // Receipt-index recovery reads historical large revisions as well.
  fs::remove_all(workspace/"documents/imported/receipts");require(reopened.call("cad_import",args)==imported,"Missing receipt index recovers from large revision");
  reopened.call("cad_create",{{"document_id","consumer"},{"model",box()}});
  const auto captured=reopened.call("cad_apply",{{"document_id","consumer"},{"expected_revision",1},{"operations",Json::array({
    {{"op","set_component"},{"id","bought"},{"source_document_id","imported"},{"source_revision",1}},
    {{"op","set_output"},{"feature_id","bought"}}})}});
  const auto captured_source=test::receipt_source(reopened,captured);
  volume(captured.at("summary"));require(captured_source.dump().size()>4*1024*1024,"Pinned snapshot and materialized STEP both exceed metadata budget");
  Temp portable;Service independent(portable.root);volume(independent.call("cad_create",{{"document_id","copy"},{"model",captured_source}}).at("summary"));
  fails("limit_exceeded",[&]{validate_payload_size(Json{{"content",std::string(max_json_bytes,'x')}});});
  fails("limit_exceeded",[&]{parse_json(imported_source.dump());});
  auto invalid_model=imported_source;invalid_model["features"][0]["sha256"]=std::string(64,'0');
  fails("invalid_model",[&]{validate_model(invalid_model);});
}
void large_job_and_review() {
  Temp temp;const auto file=temp.root/"larger.step";auto raw=source(file,65*1024*1024);const auto hash=sha256(raw);const auto bytes=raw.size();raw.clear();raw.shrink_to_fit();
  Service service(temp.root/"workspace");
  const Json submit={{"action","submit"},{"tool","cad_import"},{"request_id","large"},
    {"arguments",{{"document_id","large"},{"path",path_to_utf8(file)},{"expected_sha256",hash}}},
    {"budget",{{"timeout_ms",120000},{"memory_mb",8192}}}};
  job(service,submit);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(110);
  bool succeeded=false;
  while(std::chrono::steady_clock::now()<deadline) {
    auto state=job(service,{{"action","get"},{"job_id","large"}});
    if(state.at("state")=="succeeded") {
      volume(state.at("result").at("summary"));const auto saved_source=test::receipt_source(service,state.at("result"));const auto& content=saved_source.at("features")[0].at("content").get_ref<const std::string&>();
      require(content.size()==bytes&&sha256(content)==hash,"Durable async receipt resolves complete STEP source above 64 MiB");succeeded=true;break;
    }
    require(state.at("state")!="failed"&&state.at("state")!="interrupted",state.dump());
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
  require(succeeded,"Large async import terminates");
  const auto review=service.call("cad_artifact",{{"action","review"},{"path",path_to_utf8(file)},{"format","step"},{"units","file"},{"expected_sha256",hash}});
  require(review.at("source").at("bytes")==bytes&&review.at("source").at("sha256")==hash,"Artifact capture accepts STEP above 64 MiB");
  fs::remove(file);Service reopened(temp.root/"workspace");
  const auto verified=reopened.call("cad_artifact",{{"action","verify"},{"review_path",review.at("path")},{"expected_sha256",review.at("sha256")}});
  require(verified.at("source")==review.at("source"),"Portable artifact verification reparses large captured source after original deletion");
  require(job(reopened,submit).at("job_id")=="large","Async replay survives reopen and source deletion");
  require(job(reopened,{{"action","get"},{"job_id","large"}}).at("state")=="succeeded","Large durable result remains readable after reopen");
}
}
int main(){try{set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));lifecycle();large_job_and_review();std::cout<<checks<<" STEP import checks passed\n";return 0;}
  catch(const std::exception& e){std::cerr<<"STEP import failure: "<<e.what()<<'\n';return 1;}}
