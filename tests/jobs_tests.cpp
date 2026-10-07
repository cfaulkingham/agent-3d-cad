#include "agentcad/jobs.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/service.hpp"
#include "agentcad/mcp.hpp"
#include "agentcad/runtime.hpp"
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
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
std::set<fs::path> cache_entries(const fs::path& root) {
  std::set<fs::path> result;
  for(const auto& entry:fs::directory_iterator(root/".cache"))
    if(entry.path().extension()==".json") result.insert(entry.path());
  return result;
}
// A valid model that is heavy inside the per-feature replication budget: a
// 64 x 64 pin grid (4,096 solids, the documented maximum) cut through one plate
// by a single Boolean. It builds for several seconds and well past 128 MiB, so
// cancellation, kill, deadline and memory limits all act during the build.
Json heavy_operations() {
  const Json features = Json::array({
    {{"id","plate"},{"type","box"},{"size",{1300,1300,5}},{"origin",{-10,-10,0}}},
    {{"id","pin"},{"type","cylinder"},{"radius",4},{"height",20},{"origin",{0,0,-5}}},
    {{"id","row"},{"type","pattern"},{"input","pin"},{"count",64},{"step",{20,0,0}}},
    {{"id","grid"},{"type","pattern"},{"input","row"},{"count",64},{"step",{0,20,0}}},
    {{"id","perforated"},{"type","cut"},{"left","plate"},{"right","grid"}}});
  Json result = Json::array();
  for (const auto& feature : features) result.push_back({{"op","add_feature"},{"feature",feature}});
  result.push_back({{"op","set_output"},{"feature_id","perforated"}}); return result;
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
std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
constexpr std::int64_t day_ms = 24LL * 60 * 60 * 1000;
void expect_error(const std::string& code, const std::string& message, const std::function<void()>& function) {
  try { function(); } catch (const Error& e) { require(e.code == code, message + " (got " + e.code + ": " + e.what() + ")"); return; }
  require(false, message + " (no error)");
}
void wait_until(const std::function<bool()>& condition, int seconds, const std::string& message) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  while (!condition()) {
    if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Timed out: " + message);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}
Json stored_state(const fs::path& job) { return parse_json(read_text(job / "state.json")); }
Json find_job(const Json& listed, const std::string& id) {
  for (const auto& job : listed.at("jobs")) if (job.at("job_id") == id) return job;
  throw std::runtime_error("Job missing from list: " + id);
}
// Current on-disk job layout (docs/PROTOCOL.md): small state.json, immutable
// request.json and, after success, result.json with the job identity.
Json state_record(const std::string& id, const std::string& tool, const Json& input, const std::string& state,
                  std::int64_t submitted, std::int64_t updated) {
  Json record = {{"schema_version",2},{"job_id",id},{"request_id",id},{"tool",tool},
    {"fingerprint",request_fingerprint(tool,input)},{"budget",{{"timeout_ms",30000},{"memory_mb",2048}}},
    {"state",state},{"progress",0.0},{"submitted_at_unix_ms",submitted},{"updated_at_unix_ms",updated},
    {"deadline_unix_ms",submitted + 30000},{"retried",false}};
  if (input.contains("document_id")) record["document_id"] = input.at("document_id");
  if (state == "failed") record["error"] = Error("worker_failed","Earlier failure").json();
  if (state == "cancelled") record["error"] = Error("job_cancelled","Earlier cancellation").json();
  return record;
}
void write_job(const fs::path& root, const Json& state, const Json& input) {
  const auto path = root / "jobs" / state.at("job_id").get<std::string>();
  directory(root / "jobs"); directory(path);
  atomic_text(path / "request.json", Json{{"schema_version",2},{"job_id",state.at("job_id")},{"tool",state.at("tool")},
    {"fingerprint",state.at("fingerprint")},{"arguments",input}}.dump());
  atomic_text(path / "state.json", state.dump());
}
bool lock_free(const fs::path& path) {
  try { WorkspaceLock probe(path); return true; } catch (const Error&) { return false; }
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
    const auto initial_cache=cache_entries(temp.path);
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
    require(cache_entries(temp.path)==initial_cache,"Cancelled worker publishes no cache");
    submit_heavy(temp.path,"killJob"); kill_worker(building_pid(temp.path));
    require(finished(temp.path,"killJob").at("state")=="failed","killed worker produces durable failure");
    require(service.call("cad_read",{{"document_id","part"}}).at("revision")==1,"worker crash preserves HEAD");
    require(cache_entries(temp.path)==initial_cache,"Killed worker publishes no cache");
    submit_heavy(temp.path,"timeoutJob",200);
    const auto timed=finished(temp.path,"timeoutJob");
    require(timed.at("state")=="failed" && timed.at("error").at("code")=="job_timeout","wall deadline enforced");
    require(service.call("cad_read",{{"document_id","part"}}).at("revision")==1,"timeout preserves HEAD");
    require(cache_entries(temp.path)==initial_cache,"Timed-out worker publishes no cache");
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
    require(cache_entries(temp.path)==initial_cache,"Memory-limited worker publishes no cache");
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
    const Json query_input={{"document_id","part"},{"revision",1}};
    {
      // Unknown identities are explicit misses, not worker failures.
      for (const auto* action : {"get","cancel"})
        expect_error("not_found",std::string(action)+" of an unknown job is not_found",
          [&]{ dispatch(temp.path,{{"action",action},{"job_id","neverSubmitted"}}); });
    }
    {
      // A result lives beside a small state record that admission can scan cheaply.
      const auto job=temp.path/"jobs"/"queryJob";
      const auto state=stored_state(job);
      require(state.at("schema_version")==2 && !state.contains("result") && !state.contains("arguments"),"state.json embeds neither result nor arguments");
      require(fs::file_size(job/"state.json")<16*1024,"state.json stays small");
      require(fs::exists(job/"request.json") && fs::exists(job/"result.json"),"request and result are separate files");
      require(state.at("result_bytes")==fs::file_size(job/"result.json"),"state records the result size");
      require(state.at("result_sha256")==sha256(read_text(job/"result.json",64*1024*1024)),"state records the result digest");
      const auto envelope=parse_json(read_text(job/"result.json",64*1024*1024),64*1024*1024);
      require(envelope.at("job_id")=="queryJob" && envelope.at("result")==queried.at("result"),"result file carries the job identity and result");
      for (const auto& item : dispatch(temp.path,{{"action","list"}}).at("jobs"))
        require(!item.contains("result"),"list never returns results");
    }
    {
      // Records written by the previous format embed the result in state.json.
      const auto now=now_ms();
      auto legacy=[&](const std::string& id, const Json& result) {
        Json record={{"job_id",id},{"request_id",id},{"tool","cad_query"},{"arguments",query_input},
          {"fingerprint",request_fingerprint("cad_query",query_input)},{"budget",{{"timeout_ms",30000},{"memory_mb",2048}}},
          {"state","succeeded"},{"progress",1.0},{"submitted_at_unix_ms",now-2000},{"updated_at_unix_ms",now-1000},
          {"deadline_unix_ms",now+28000},{"retried",false},{"result",result}};
        directory(temp.path/"jobs"/id); atomic_text(temp.path/"jobs"/id/"state.json",record.dump(),64*1024*1024);
      };
      legacy("legacyDone",queried.at("result"));
      const Json large={{"blob",std::string(2*1024*1024,'x')}};
      legacy("legacyLarge",large);
      const auto read=dispatch(temp.path,{{"action","get"},{"job_id","legacyDone"}});
      require(read.at("state")=="succeeded" && read.at("result")==queried.at("result"),"legacy embedded result is readable");
      require(dispatch(temp.path,{{"action","get"},{"job_id","legacyLarge"}}).at("result")==large,"legacy record above the state bound is readable");
      require(find_job(dispatch(temp.path,{{"action","list"}}),"legacyLarge").at("state")=="succeeded","legacy record listed");
      for (const auto* id : {"legacyDone","legacyLarge"}) {
        const auto migrated=stored_state(temp.path/"jobs"/id);
        require(migrated.at("schema_version")==2 && !migrated.contains("result") && fs::exists(temp.path/"jobs"/id/"result.json"),
          "terminal legacy record migrates to a separate result");
      }
      require(dispatch(temp.path,{{"action","get"},{"job_id","legacyLarge"}}).at("result")==large,"migrated legacy result remains readable");
      require(dispatch(temp.path,{{"action","submit"},{"request_id","legacyDone"},{"tool","cad_query"},{"arguments",query_input}}).at("result")==queried.at("result"),
        "duplicate submit returns the legacy result");
    }
    {
      // Corrupt, foreign and identity-free records never break list or admission.
      directory(temp.path/"jobs"/"corruptJob"); atomic_text(temp.path/"jobs"/"corruptJob"/"state.json","{not json");
      directory(temp.path/"jobs"/"emptyJob"); atomic_text(temp.path/"jobs"/"emptyJob"/"state.json","{}");
      Json anonymous=stale; anonymous["job_id"]="noDocument"; anonymous["request_id"]="noDocument"; anonymous["arguments"]=Json::object();
      directory(temp.path/"jobs"/"noDocument"); atomic_text(temp.path/"jobs"/"noDocument"/"state.json",anonymous.dump());
      Temp outside; std::error_code link_error;
      fs::create_directory_symlink(outside.path,temp.path/"jobs"/"linkedJob",link_error);
      const auto listed=dispatch(temp.path,{{"action","list"}});
      for (const auto* id : {"corruptJob","emptyJob"}) {
        const auto item=find_job(listed,id);
        require(item.at("state")=="failed" && item.at("error").at("code")=="job_record_corrupt","corrupt record is listed as job_record_corrupt");
      }
      require(find_job(listed,"noDocument").at("state")=="interrupted","recovery tolerates a record without document identity");
      if (!link_error) require(find_job(listed,"linkedJob").at("error").at("code")=="job_record_corrupt","symlinked job directory is reported, not fatal");
      const auto corrupt=dispatch(temp.path,{{"action","get"},{"job_id","corruptJob"}});
      require(corrupt.at("state")=="failed" && corrupt.at("error").at("code")=="job_record_corrupt","get reports a corrupt record");
      auto fresh=query; fresh["request_id"]="afterCorrupt";
      dispatch(temp.path,fresh);
      require(finished(temp.path,"afterCorrupt").at("state")=="succeeded","submit admits beside corrupt records");
      if (!link_error) fs::remove(temp.path/"jobs"/"linkedJob");
    }
    {
      // Arguments are validated at submission; nothing is recorded for them.
      const Json invalid[]={Json{{"revision",1}},Json{{"document_id","../escape"},{"revision",1}},
        Json{{"document_id","part"},{"revision",1},{"unexpected",true}},Json{{"document_id","part"},{"revision",0}},Json("text")};
      for (const auto& input : invalid) {
        expect_error("invalid_argument","malformed job arguments fail at submit",[&]{
          dispatch(temp.path,{{"action","submit"},{"request_id","badArguments"},{"tool","cad_query"},{"arguments",input}}); });
        require(!fs::exists(temp.path/"jobs"/"badArguments"),"rejected submission records no job");
      }
    }
    {
      // A coordinator can stop after its durable result but before its state flips.
      const auto old=now_ms()-60000;
      write_job(temp.path,state_record("resultOnly","cad_query",query_input,"running",old,old),query_input);
      atomic_text(temp.path/"jobs"/"resultOnly"/"result.json",Json{{"schema_version",2},{"job_id","resultOnly"},
        {"fingerprint",request_fingerprint("cad_query",query_input)},{"result",queried.at("result")}}.dump());
      const auto recovered=dispatch(temp.path,{{"action","get"},{"job_id","resultOnly"}});
      require(recovered.at("state")=="succeeded" && recovered.at("result")==queried.at("result"),"durable result recovers as success");
      write_job(temp.path,state_record("foreignResult","cad_query",query_input,"running",old,old),query_input);
      atomic_text(temp.path/"jobs"/"foreignResult"/"result.json",Json{{"schema_version",2},{"job_id","foreignResult"},
        {"fingerprint","0000"},{"result",queried.at("result")}}.dump());
      require(dispatch(temp.path,{{"action","get"},{"job_id","foreignResult"}}).at("state")=="interrupted","mismatched result identity is not trusted");
    }
    {
      // The coordinator's final state save is blocked; its durable result still wins.
      Temp blocked; Service blocked_service(blocked.path);
      blocked_service.call("cad_create",{{"document_id","part"},{"model",box()}});
      const auto expected=blocked_service.call("cad_query",query_input);
      std::vector<std::unique_ptr<WorkspaceLock>> slots;
      for (int i=0;i<4;++i) {
        auto path=blocked.path/".workers"/(".slot-"+std::to_string(i)); directory(path);
        slots.push_back(std::make_unique<WorkspaceLock>(path));
      }
      auto item=query; item["request_id"]="saveBlocked"; dispatch(blocked.path,item);
      const auto job=blocked.path/"jobs"/"saveBlocked";
      wait_until([&]{ return stored_state(job).at("state")=="running"; },20,"coordinator enters running");
      {
        WorkspaceLock admission(blocked.path/"jobs");
        slots.clear();
        wait_until([&]{ return fs::exists(job/"result.json"); },30,"result published before state");
        wait_until([&]{ return lock_free(job); },30,"coordinator gives up on its blocked state save");
        require(stored_state(job).at("state")=="running","blocked final save left the earlier state");
      }
      const auto recovered=dispatch(blocked.path,{{"action","get"},{"job_id","saveBlocked"}});
      require(recovered.at("state")=="succeeded" && recovered.at("result")==expected,"durable result recovers after a failed final state save");
    }
    {
      // Any coordinator exception is persisted as an explicit failure.
      const auto now=now_ms();
      const auto path=temp.path/"jobs"/"missingRequest"; directory(path);
      atomic_text(path/"state.json",state_record("missingRequest","cad_query",query_input,"queued",now,now).dump());
      require(job_worker_main(temp.path,"missingRequest")==1,"coordinator reports its failure");
      const auto failed=dispatch(temp.path,{{"action","get"},{"job_id","missingRequest"}});
      require(failed.at("state")=="failed" && failed.at("error").at("code")=="job_record_corrupt","coordinator failure is durable");
    }
    {
      // Retention: only old or excess terminal records are collected, never live ones.
      Temp gc; Service gc_service(gc.path);
      gc_service.call("cad_create",{{"document_id","part"},{"model",box()}});
      const auto now=now_ms(), expired=now-8*day_ms;
      write_job(gc.path,state_record("expiredJob","cad_query",query_input,"failed",expired,expired),query_input);
      write_job(gc.path,state_record("recentJob","cad_query",query_input,"failed",now-3600000,now-3600000),query_input);
      write_job(gc.path,state_record("expiredLocked","cad_query",query_input,"failed",expired,expired),query_input);
      write_job(gc.path,state_record("expiredActive","cad_query",query_input,"running",expired,expired),query_input);
      Json legacy={{"job_id","expiredLegacy"},{"request_id","expiredLegacy"},{"tool","cad_query"},{"arguments",query_input},
        {"fingerprint",request_fingerprint("cad_query",query_input)},{"budget",{{"timeout_ms",30000},{"memory_mb",2048}}},
        {"state","succeeded"},{"progress",1.0},{"submitted_at_unix_ms",expired},{"updated_at_unix_ms",expired},
        {"deadline_unix_ms",expired+30000},{"retried",false},{"result",Json::object()}};
      directory(gc.path/"jobs"/"expiredLegacy"); atomic_text(gc.path/"jobs"/"expiredLegacy"/"state.json",legacy.dump());
      directory(gc.path/"jobs"/"expiredCorrupt"); atomic_text(gc.path/"jobs"/"expiredCorrupt"/"state.json","{");
      fs::last_write_time(gc.path/"jobs"/"expiredCorrupt"/"state.json",fs::file_time_type::clock::now()-std::chrono::hours(24*8));
      {
        WorkspaceLock held(gc.path/"jobs"/"expiredLocked"), live(gc.path/"jobs"/"expiredActive");
        auto trigger=query; trigger["request_id"]="gcTrigger"; dispatch(gc.path,trigger);
        for (const auto* id : {"expiredJob","expiredLegacy","expiredCorrupt"})
          require(!fs::exists(gc.path/"jobs"/id),std::string("expired terminal record collected: ")+id);
        for (const auto* id : {"recentJob","expiredLocked","expiredActive","gcTrigger"})
          require(fs::exists(gc.path/"jobs"/id/"state.json"),std::string("recent, locked or live record retained: ")+id);
        expect_error("not_found","collected job is no longer addressable",[&]{ dispatch(gc.path,{{"action","get"},{"job_id","expiredJob"}}); });
        for (int i=0;i<260;++i) {
          const auto at=now-(1000+i)*1000LL;
          write_job(gc.path,state_record("bulk"+std::to_string(i),"cad_query",query_input,"cancelled",at,at),query_input);
        }
        auto count=query; count["request_id"]="countTrigger"; dispatch(gc.path,count);
        require(fs::exists(gc.path/"jobs"/"bulk0") && !fs::exists(gc.path/"jobs"/"bulk259"),"count bound collects the oldest terminal records");
        require(!fs::exists(gc.path/"jobs"/"recentJob"),"older terminal record beyond the count bound is collected");
        require(fs::exists(gc.path/"jobs"/"expiredLocked") && fs::exists(gc.path/"jobs"/"expiredActive"),"locked and live records survive the count bound");
        std::size_t retained=0;
        for (const auto& entry : fs::directory_iterator(gc.path/"jobs")) {
          const auto name=path_to_utf8(entry.path().filename());
          require(name.rfind(".trash-",0)!=0,"collection leaves no trash directories");
          if (entry.is_directory()) ++retained;
        }
        require(retained<=256+4,"terminal records are bounded");
      }
      require(finished(gc.path,"gcTrigger").at("state")=="succeeded" && finished(gc.path,"countTrigger").at("state")=="succeeded","collection does not disturb new jobs");
    }
    {
      // Linux reports an upgraded running image as "<path> (deleted)".
      const auto upgraded=resolve_proc_self_exe("/opt/cad/bin/agent-3d-cad (deleted)");
      require(upgraded.path==fs::path("/opt/cad/bin/agent-3d-cad") && upgraded.image==fs::path("/proc/self/exe"),
        "a replaced binary resolves resources by path and spawns the running image");
      const auto current=resolve_proc_self_exe("/opt/cad/bin/agent-3d-cad-linux-x64");
      require(current.path==fs::path("/opt/cad/bin/agent-3d-cad-linux-x64") && current.image==current.path,
        "a renamed binary spawns itself");
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
