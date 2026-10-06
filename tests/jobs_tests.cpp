#include "agentcad/jobs.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/service.hpp"
#include "agentcad/mcp.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif
using namespace agentcad;
namespace {
int checks = 0;
void require(bool truth, const std::string& message) { ++checks; if (!truth) throw std::runtime_error(message); }
struct Temp {
  fs::path path;
  Temp() { path = temporary_file(fs::temp_directory_path()); fs::remove(path); directory(path); }
  ~Temp() { std::error_code ignored; fs::remove_all(path, ignored); }
};
Json box() { return {{"schema_version",1},{"units","mm"},{"parameters",{{"size",10}}},
  {"features",Json::array({{{"id","base"},{"type","box"},{"size",Json::array({Json{{"parameter","size"}},10,10})}}})},{"output","base"}}; }
Json heavy_operations() {
  Json result = Json::array();
  std::string input = "base";
  for (int i = 0; i < 3; ++i) {
    const auto id = "array" + std::to_string(i);
    const Json feature = {{"id",id},{"type","pattern"},{"input",input},{"count",64},
      {"step",Json::array({(i==0 ? 20 : 0),(i==1 ? 20 : 0),(i==2 ? 20 : 0)})}};
    result.push_back({{"op","add_feature"},{"feature",feature}});
    input = id;
  }
  result.push_back({{"op","set_output"},{"feature_id",input}}); return result;
}
Json dispatch(const fs::path& root, const Json& args) {
  for (int attempt = 0;; ++attempt) {
    try { return dispatch_job(root,args); }
    catch (const Error& e) { if (e.code != "workspace_busy" || attempt > 1000) throw; }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}
Json finished(const fs::path& root, const std::string& id) {
  for (int i = 0; i < 3000; ++i) {
    auto job = dispatch(root,{{"action","get"},{"job_id",id}});
    const auto state = job.at("state").get<std::string>();
    if (state=="succeeded" || state=="failed" || state=="cancelled" || state=="interrupted") return job;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  throw std::runtime_error("Job did not finish");
}
Json submit_heavy(const fs::path& root, const std::string& id, int timeout = 10000, int memory = 2048) {
  return dispatch(root,{{"action","submit"},{"request_id",id},{"tool","cad_apply"},
    {"arguments",{{"document_id","part"},{"expected_revision",1},{"operations",heavy_operations()}}},
    {"budget",{{"timeout_ms",timeout},{"memory_mb",memory}}}});
}
std::uint64_t building_pid(const fs::path& root) {
  for (int i = 0; i < 1000; ++i) {
    for (const auto& entry : fs::directory_iterator(root / ".workers")) {
      if (fs::exists(entry.path()/"building.json") && fs::exists(entry.path()/"process.json"))
        return parse_json(read_text(entry.path()/"process.json")).at("pid");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  throw std::runtime_error("Geometry worker never entered build phase");
}
void kill_worker(std::uint64_t pid) {
#ifdef _WIN32
  HANDLE handle = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
  if (!handle) throw std::runtime_error("Cannot open worker");
  const bool killed = TerminateProcess(handle, 99); CloseHandle(handle); require(killed,"terminate worker");
#else
  require(::kill(static_cast<pid_t>(pid), SIGKILL)==0,"terminate worker");
#endif
}
}
int run_tests(int argc, const char* const* argv) {
  try {
    if (argc > 1) set_worker_executable(path_from_utf8(argv[1]));
    require(sha256("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA256 empty standard vector");
    require(sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 abc standard vector");
    Temp temp; Service service(temp.path);
    const Json create = {{"document_id","part"},{"model",box()},{"request_id","createOnce"}};
    const auto created = service.call("cad_create",create);
    require(created.at("revision")==1,"worker creates revision");
    require(std::abs(created.at("summary").at("volume_mm3").get<double>()-1000)<1e-6,"real worker volume");
    require(service.call("cad_create",create)==created,"durable synchronous duplicate replay");
    auto changed = create; changed["model"]["parameters"]["size"]=20;
    try { service.call("cad_create",changed); require(false,"changed request must conflict"); }
    catch (const Error& e) { require(e.code=="request_conflict","request payload mismatch rejected"); }
    {
      DocumentLock first(temp.path,"part"); DocumentLock independent(temp.path,"other");
      try { DocumentLock conflict(temp.path,"part"); require(false,"same document writers excluded"); }
      catch (const Error& e) { require(e.code=="workspace_busy","same document exclusion"); }
      require(service.call("cad_read",{{"document_id","part"}}).at("revision")==1,"reads proceed under document locks");
    }
    Json query = {{"action","submit"},{"request_id","queryJob"},{"tool","cad_query"},{"arguments",{{"document_id","part"},{"revision",1}}}};
    auto submitted = dispatch(temp.path,query);
    require(submitted.at("state")=="queued","job submits without waiting for geometry");
    const auto queried=finished(temp.path,"queryJob");
    require(queried.at("state")=="succeeded","job query succeeds");
    require(dispatch(temp.path,query).at("result")==queried.at("result"),"job submission deduplicated");
    McpSession session(service);
    const Json initialization = {{"protocolVersion","2025-11-25"},{"capabilities",Json::object()},
      {"clientInfo",{{"name","jobs-test"},{"version","1"}}}};
    session.handle({{"jsonrpc","2.0"},{"id",1},{"method","initialize"},{"params",initialization}});
    session.handle({{"jsonrpc","2.0"},{"method","notifications/initialized"}});
    const auto started = session.handle({{"jsonrpc","2.0"},{"id",2},{"method","tools/call"},{"params",{{"name","cad_job"},
      {"arguments",{{"action","submit"},{"request_id","cancelJob"},{"tool","cad_apply"},
        {"arguments",{{"document_id","part"},{"expected_revision",1},{"operations",heavy_operations()}}}}}}}});
    require(started && started->at("result").at("structuredContent").at("state")=="queued","MCP job submit returns before build");
    building_pid(temp.path);
    const auto ping = session.handle({{"jsonrpc","2.0"},{"id",3},{"method","ping"}});
    require(ping && ping->at("id")==3 && ping->contains("result"),"MCP ping stays responsive while native geometry worker builds");
    const auto read = session.handle({{"jsonrpc","2.0"},{"id",4},{"method","tools/call"},
      {"params",{{"name","cad_read"},{"arguments",{{"document_id","part"}}}}}});
    require(read && read->at("result").at("structuredContent").at("revision")==1,"MCP reads committed HEAD during active build");
    for(int attempt=0;;++attempt) {
      const auto cancel=session.handle({{"jsonrpc","2.0"},{"id",5},{"method","tools/call"},
        {"params",{{"name","cad_job"},{"arguments",{{"action","cancel"},{"job_id","cancelJob"}}}}}});
      if(cancel->at("result").at("isError")==false) break;
      if(attempt>=100 || cancel->at("result").at("structuredContent").at("error").at("code")!="workspace_busy")
        throw std::runtime_error("MCP cancellation failed");
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    require(finished(temp.path,"cancelJob").at("state")=="cancelled","cancel terminates active geometry");
    require(service.call("cad_read",{{"document_id","part"}}).at("revision")==1,"cancel preserves HEAD");
    submit_heavy(temp.path,"killJob"); kill_worker(building_pid(temp.path));
    require(finished(temp.path,"killJob").at("state")=="failed","killed worker produces durable failure");
    require(service.call("cad_read",{{"document_id","part"}}).at("revision")==1,"worker crash preserves HEAD");
    submit_heavy(temp.path,"timeoutJob",200);
    const auto timed=finished(temp.path,"timeoutJob");
    require(timed.at("state")=="failed" && timed.at("error").at("code")=="job_timeout","wall deadline enforced");
    require(service.call("cad_read",{{"document_id","part"}}).at("revision")==1,"timeout preserves HEAD");
    submit_heavy(temp.path,"memoryJob",10000,128);
    const auto limited=finished(temp.path,"memoryJob");
    require(limited.at("state")=="failed","worker memory budget produces failure");
    require(limited.at("error").at("code")!="job_timeout","memory enforcement fires before deadline");
#ifdef __APPLE__
    require(limited.at("error").at("code")=="memory_limit","Darwin physical-footprint watchdog enforced the memory bound");
#else
    require(limited.at("error").at("code")=="memory_limit" || limited.at("error").at("code")=="worker_failed" ||
      limited.at("error").at("code")=="kernel_failure","OS memory containment rejects allocation or terminates worker");
#endif
    require(service.call("cad_read",{{"document_id","part"}}).at("revision")==1,"memory failure preserves HEAD");
    const Json edit = {{"document_id","part"},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","size"},{"value",11}}})},{"request_id","crashAfterCommit"}};
    const auto edited=service.call("cad_apply",edit);
    // Model the exact crash window: revision and receipt are committed, but the
    // coordinator's durable state still says running and its process lock is gone.
    directory(temp.path/"jobs"/"crashAfterCommit");
    Json stale={{"job_id","crashAfterCommit"},{"request_id","crashAfterCommit"},{"tool","cad_apply"},{"arguments",edit},
      {"fingerprint",request_fingerprint("cad_apply",edit)},{"budget",{{"timeout_ms",30000},{"memory_mb",2048}}},
      {"state","running"},{"progress",0.1},{"submitted_at_unix_ms",1},{"deadline_unix_ms",1}};
    atomic_text(temp.path/"jobs"/"crashAfterCommit"/"state.json",stale.dump());
    const auto recovered=dispatch(temp.path,{{"action","get"},{"job_id","crashAfterCommit"}});
    require(recovered.at("state")=="succeeded" && recovered.at("result")==edited,"committed receipt recovers lost job result");
    require(service.call("cad_apply",edit)==edited,"retry after lost result does not commit twice");
    require(service.call("cad_read",{{"document_id","part"}}).at("revision")==2,"recovery preserves exactly one committed mutation");
    require(dispatch(temp.path,{{"action","list"}}).at("jobs").size()==6,"durable job list");
    {
      Temp outside;
      std::error_code error;
      fs::create_directory_symlink(outside.path,temp.path/"jobs"/"symlinkJob",error);
      if(!error) {
        try { dispatch(temp.path,{{"action","get"},{"job_id","symlinkJob"}}); require(false,"job symlink must reject"); }
        catch(const Error& e) { require(e.code=="storage_error","managed job directory symlink rejected"); }
        fs::remove(temp.path/"jobs"/"symlinkJob");
      }
    }
    {
      Temp queue; Service queue_service(queue.path);
      queue_service.call("cad_create",{{"document_id","part"},{"model",box()}});
      std::vector<std::unique_ptr<WorkspaceLock>> slots;
      for (int i=0;i<4;++i) {
        auto path=queue.path/".workers"/(".slot-"+std::to_string(i)); directory(path);
        slots.push_back(std::make_unique<WorkspaceLock>(path));
      }
      for(int i=0;i<8;++i) {
        auto item=query; item["request_id"]="queued"+std::to_string(i); dispatch(queue.path,item);
      }
      auto rejected=query; rejected["request_id"]="queueOverflow";
      try { dispatch(queue.path,rejected); require(false,"queue overflow must reject"); }
      catch(const Error& e) { require(e.code=="queue_full","bounded queue admission"); }
      for(int i=0;i<8;++i) dispatch(queue.path,{{"action","cancel"},{"job_id","queued"+std::to_string(i)}});
      slots.clear();
      for(int i=0;i<8;++i) require(finished(queue.path,"queued"+std::to_string(i)).at("state")=="cancelled","cancel queued work");
    }
    {
      const auto path=temp.path/"jobs"/"beforeCommit"; directory(path);
      Json input={{"document_id","retryPart"},{"model",box()},{"request_id","beforeCommit"}};
      Json interrupted=stale; interrupted["job_id"]="beforeCommit"; interrupted["request_id"]="beforeCommit";
      interrupted["tool"]="cad_create"; interrupted["arguments"]=input;
      interrupted["fingerprint"]=request_fingerprint("cad_create",input);
      atomic_text(path/"state.json",interrupted.dump());
      require(dispatch(temp.path,{{"action","get"},{"job_id","beforeCommit"}}).at("state")=="interrupted","precommit crash recovers to explicit interrupted state");
      dispatch(temp.path,{{"action","submit"},{"request_id","beforeCommit"},{"tool","cad_create"},{"arguments",input}});
      require(finished(temp.path,"beforeCommit").at("state")=="succeeded","interrupted request retries with same identity");
      require(service.call("cad_read",{{"document_id","retryPart"}}).at("revision")==1,"retry commits once");
    }

    std::cout << checks << " job/storage checks passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << "FAILED: " << e.what() << '\n'; return 1; }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  std::vector<std::string> text;
  for (int index = 0; index < argc; ++index) text.push_back(path_to_utf8(fs::path(argv[index])));
  std::vector<const char*> arguments;
  for (const auto& argument : text) arguments.push_back(argument.c_str());
  return run_tests(argc, arguments.data());
}
#else
int main(int argc, char** argv) { return run_tests(argc, argv); }
#endif
