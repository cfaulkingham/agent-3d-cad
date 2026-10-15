#include "agentcad/failure.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include <algorithm>
#include <chrono>
#include <set>
#include <tuple>
#include <vector>

namespace agentcad {
namespace {
constexpr std::size_t package_limit=64*1024*1024, retained_bytes=256*1024*1024, retained_count=32;
thread_local const FailureCallScope* active_call=nullptr;
thread_local bool replaying=false;
Json object(Json properties,Json required) {return {{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};}
std::int64_t now_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
void digest(const std::string& value){if(value.size()!=64||value.find_first_not_of("0123456789abcdef")!=std::string::npos)throw Error("invalid_argument","Expected a lowercase SHA-256");}
void plain_path(const fs::path& path) {
  auto current=fs::absolute(path).lexically_normal();
  while(!current.empty()) {
    if(fs::is_symlink(fs::symlink_status(current)))throw Error("storage_error","Failure package paths cannot contain symlinks");
    const auto parent=current.parent_path();if(parent==current)break;current=parent;
  }
}
std::string replay_limitation(const Json& request) {
  static const std::set<std::string> supported={"summary","topology","view","curve","component_manifest","component_mesh",
    "inspect_step","capture_sketch","projection","drawing","export","manufacturing","render","render_animation",
    "external_render","fabrication","measure","robot","recognize_mesh"};
  if(!request.is_object()||!request.contains("kind")||!request.at("kind").is_string()||!supported.contains(text_field(request,"kind")))
    return "This worker request is not self-contained; automatic replay is unavailable";
  return "";
}
Json error_identity(Json error) {
  // Paths and worker PIDs can differ in the isolated replay. Require the same
  // code, message and failing feature, rather than claiming a stack-trace match.
  return {{"code",error.at("code")},{"message",error.at("message")},
    {"feature_id",error.at("details").value("feature_id",Json())}};
}
struct Package {fs::path path;Json report,payload;std::string sha256;};
Package read_package(const fs::path& path,const std::string& expected="",bool payload=true) {
  plain_path(path);const auto raw=read_text(path/"report.json");
  const auto hash=sha256(raw);if(!expected.empty()&&hash!=expected)throw Error("artifact_mismatch","Failure report does not match expected_sha256");
  const auto report=parse_json(raw);
  fields(report,{"format","failure_id","created_at_unix_ms","native_build","kernel_version","service_version","error","context","budget","model_sha256","request_sha256","replayable","replay_limitation","payload"});
  if(report.at("format")!="agentcad-failure/1")throw Error("invalid_argument","Unsupported failure package format");
  identifier(text_field(report,"failure_id"));
  for(const auto* field:{"native_build","kernel_version","service_version","replay_limitation"})text_field(report,field);
  for(const auto* field:{"model_sha256","request_sha256"})digest(text_field(report,field));
  if(!report.at("created_at_unix_ms").is_number_integer()||report.at("created_at_unix_ms")<0||!report.at("context").is_object()||!report.at("replayable").is_boolean())throw Error("invalid_argument","Malformed failure report metadata");
  fields(report.at("error"),{"code","message","details"});text_field(report.at("error"),"code");text_field(report.at("error"),"message");
  if(!report.at("error").at("details").is_object())throw Error("invalid_argument","Malformed failure error details");
  validate_job_budget(report.at("budget"));
  fields(report.at("payload"),{"path","sha256","bytes"});
  if(report.at("payload").at("path")!="payload.json"||!report.at("payload").at("bytes").is_number_unsigned()||report.at("payload").at("bytes")>package_limit)
    throw Error("invalid_argument","Invalid failure payload ledger");
  digest(text_field(report.at("payload"),"sha256"));
  Package result{path,report,Json(),hash};
  if(payload) {
    const auto content=read_text(path/"payload.json",package_limit);
    if(content.size()!=report.at("payload").at("bytes")||report.at("payload").at("sha256")!=sha256(content))throw Error("artifact_mismatch","Failure payload does not match its ledger");
    result.payload=parse_payload_json(content,package_limit);fields(result.payload,{"model","request","source"});
    if(report.at("model_sha256")!=sha256(result.payload.at("model").dump())||report.at("request_sha256")!=sha256(result.payload.at("request").dump()))throw Error("artifact_mismatch","Failure source identity does not match its report");
  }
  return result;
}
Json location(const Package& package){return {{"failure_id",package.report.at("failure_id")},{"path",path_to_utf8(package.path/"report.json")},{"sha256",package.sha256}};}
// Called only under the private failure-store lock. Only verified, generated
// two-file packages are candidates for removal; unrelated files are untouched.
void collect(const fs::path& root,std::size_t incoming) {
  struct Entry {fs::path path;std::int64_t time;std::size_t bytes;};std::vector<Entry> entries;std::size_t total=0;
  for(const auto& item:fs::directory_iterator(root)) {
    if(item.is_symlink()||!item.is_directory()||!path_to_utf8(item.path().filename()).starts_with("failure-"))continue;
    try {
      const auto p=read_package(item.path(),"",false);
      if(item.path().filename()!=path_from_utf8(text_field(p.report,"failure_id")))continue;
      std::size_t count=0;bool clean=true;
      for(const auto& file:fs::directory_iterator(item.path())){++count;clean&=!file.is_symlink()&&file.is_regular_file()&&(file.path().filename()=="report.json"||file.path().filename()=="payload.json");}
      if(!clean||count!=2)continue;
      const auto size=fs::file_size(item.path()/"report.json")+fs::file_size(item.path()/"payload.json");
      entries.push_back({item.path(),p.report.at("created_at_unix_ms").get<std::int64_t>(),size});total+=size;
    }catch(const std::exception&){}
  }
  std::sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return std::tie(a.time,a.path)<std::tie(b.time,b.path);});
  for(std::size_t i=0;i<entries.size()&&(entries.size()-i>=retained_count||total+incoming>retained_bytes);++i){fs::remove_all(entries[i].path);total-=entries[i].bytes;}
}
}
FailureCallScope::FailureCallScope(const std::string& tool,const Json& args):tool_(tool),args_(args),previous_(active_call){active_call=this;}
FailureCallScope::~FailureCallScope(){active_call=previous_;}
Json FailureCallScope::context() {
  if(!active_call)return Json::object();
  auto args=active_call->args_;
  if(args.contains("model")){args["model_sha256"]=sha256(args.at("model").dump());args.erase("model");}
  return {{"tool",active_call->tool_},{"arguments",args},{"job",job_request_context()}};
}
Error capture_geometry_failure(const fs::path& workspace,const Json& model,const Json& request,const Json& limits,const Error& error) {
  if(replaying||error.code=="job_cancelled"||error.code=="queue_full"||error.code=="workspace_busy")return error;
  auto details=error.details;fs::path stage;
  try {
    auto context=FailureCallScope::context();Json source=nullptr;
    if(context.contains("arguments")) {
      const auto& args=context.at("arguments");const auto revision=args.value("expected_revision",args.value("revision",Json()));
      if(args.contains("document_id")&&!revision.is_null()) {
        try {source=Store(workspace).read(text_field(args,"document_id"),revision_number(revision));}
        catch(const std::exception& e){context["source_capture_error"]=e.what();}
      }
    }
    const Json payload={{"model",model},{"request",request},{"source",source}};const auto bytes=payload.dump();
    if(bytes.size()>package_limit)throw Error("limit_exceeded","Failure payload exceeds 64 MiB");
    const auto root=workspace/"failures";plain_path(root);directory(root);WorkspaceLock lock(root);
    stage=temporary_directory(root);
    const auto id="failure-"+sha256(path_to_utf8(stage)+std::to_string(now_ms())).substr(0,24);
    const auto limitation=replay_limitation(request);
    const Json report={{"format","agentcad-failure/1"},{"failure_id",id},{"created_at_unix_ms",now_ms()},
      {"native_build",AGENTCAD_CACHE_BUILD},{"kernel_version",kernel_version()},{"service_version",AGENTCAD_VERSION},
      {"error",error.json()},{"context",context},{"budget",limits},
      {"model_sha256",sha256(model.dump())},{"request_sha256",sha256(request.dump())},
      {"replayable",limitation.empty()},{"replay_limitation",limitation},
      {"payload",{{"path","payload.json"},{"sha256",sha256(bytes)},{"bytes",bytes.size()}}}};
    const auto raw=report.dump();if(raw.size()>max_json_bytes)throw Error("limit_exceeded","Failure report exceeds 1 MiB");
    atomic_text(stage/"payload.json",bytes,package_limit);atomic_text(stage/"report.json",raw);
    collect(root,bytes.size()+raw.size());const auto target=root/id;fs::rename(stage,target);stage.clear();
    details["failure_package"]={{"failure_id",id},{"path",path_to_utf8(target/"report.json")},{"sha256",sha256(raw)}};
  }catch(const std::exception& e) {
    if(!stage.empty()){std::error_code ignored;fs::remove_all(stage,ignored);}
    details["failure_capture"]={{"state","unavailable"},{"reason",e.what()}};
  }
  return Error(error.code,error.what(),std::move(details));
}
void validate_failure_arguments(const Json& args) {
  const auto action=text_field(args,"action");
  if(action=="list") {
    fields(args,{"action"},{"limit"});if(args.contains("limit")&&(!args.at("limit").is_number_integer()||args.at("limit")<1||args.at("limit")>32))throw Error("invalid_argument","Failure list limit requires 1..32");return;
  }
  if(action!="get"&&action!="replay")throw Error("invalid_argument","Failure action must be list, get or replay");
  fields(args,{"action"},{"failure_id","path","expected_sha256","budget"});
  if(args.contains("failure_id")) {
    identifier(text_field(args,"failure_id"));if(args.contains("path")||args.contains("expected_sha256"))throw Error("invalid_argument","Use failure_id or an explicit report path and hash");
  }else {
    const auto path=path_from_utf8(text_field(args,"path"));if(!path.is_absolute()||path.filename()!="report.json")throw Error("invalid_argument","Failure path must be an absolute report.json");digest(text_field(args,"expected_sha256"));
  }
  if(args.contains("budget")) {
    if(action!="replay")throw Error("invalid_argument","Only replay accepts a budget");
    validate_job_budget(args.at("budget"));
  }
}
Json failure_call(const fs::path& workspace,const Json& args) {
  validate_failure_arguments(args);const auto action=text_field(args,"action");const auto root=workspace/"failures";
  if(action=="list") {
    Json entries=Json::array(),unreadable=Json::array();const auto limit=args.value("limit",32);
    plain_path(root);if(!fs::exists(root))return {{"failures",entries},{"unreadable",unreadable},{"limit",limit},{"truncated",false}};
    WorkspaceLock lock(root);
    for(const auto& item:fs::directory_iterator(root)) {
      const auto id=path_to_utf8(item.path().filename());if(!id.starts_with("failure-"))continue;
      try {const auto p=read_package(item.path(),"",false);auto entry=location(p);entry["created_at_unix_ms"]=p.report.at("created_at_unix_ms");entry["error"]=p.report.at("error");entry["replayable"]=p.report.at("replayable");entries.push_back(entry);}
      catch(const std::exception& e){if(unreadable.size()<32)unreadable.push_back({{"failure_id",id},{"message",e.what()}});}
    }
    std::sort(entries.begin(),entries.end(),[](const Json& a,const Json& b){return a.at("created_at_unix_ms")>b.at("created_at_unix_ms");});
    const bool truncated=entries.size()>static_cast<std::size_t>(limit);while(entries.size()>static_cast<std::size_t>(limit))entries.erase(entries.end()-1);
    return {{"failures",entries},{"unreadable",unreadable},{"limit",limit},{"truncated",truncated}};
  }
  Package package;
  if(args.contains("failure_id")) {plain_path(root);if(!fs::exists(root))throw Error("not_found","No saved failure packages");WorkspaceLock lock(root);package=read_package(root/text_field(args,"failure_id"));}
  else package=read_package(path_from_utf8(text_field(args,"path")).parent_path(),text_field(args,"expected_sha256"));
  if(action=="get"){auto result=location(package);result["report"]=package.report;return result;}
  if(package.report.at("kernel_version")!=kernel_version())throw Error("kernel_mismatch","Failure package requires a different kernel version");
  const auto limitation=replay_limitation(package.payload.at("request"));if(!limitation.empty())throw Error("unsupported_replay",limitation);
  auto limits=package.report.at("budget");if(args.contains("budget"))limits.update(args.at("budget"));limits=validate_job_budget(limits);
  // The durable job's own budget controls the entire replay when queued.
  if(!job_request_context().empty())limits=current_job_budget();
  Json result={{"failure_id",package.report.at("failure_id")},{"package_sha256",package.sha256},
    {"original_build",package.report.at("native_build")},{"replay_build",AGENTCAD_CACHE_BUILD},
    {"same_build",package.report.at("native_build")==AGENTCAD_CACHE_BUILD},{"budget",limits},
    {"original_error",package.report.at("error")},{"observed_error",nullptr},{"outcome","succeeded"}};
  const auto parent=workspace/".replays";plain_path(parent);directory(parent);const auto isolated=temporary_directory(parent);
  const auto previous=replaying;replaying=true;
  try {
    auto request=package.payload.at("request");
    if(request.contains("path")){directory(isolated/"output");request["path"]=path_to_utf8(isolated/"output"/"artifact");const auto kind=text_field(request,"kind");if(kind=="manufacturing"||kind=="robot"||(kind=="export"&&request.value("format",std::string())=="3mf"))directory(isolated/"output"/"artifact");}
    try {
      const auto evaluated=evaluate_failure_replay(isolated,package.payload.at("model"),request,limits);
      if(evaluated.contains("summary"))result["summary"]=evaluated.at("summary");
    }catch(const Error& e) {
      if(e.code=="job_cancelled")throw;
      if(!job_request_context().empty())check_job_cancelled();
      result["observed_error"]=e.json();result["outcome"]=error_identity(e.json())==error_identity(package.report.at("error"))?"reproduced":"different_failure";
    }
  }catch(...){replaying=previous;std::error_code ignored;fs::remove_all(isolated,ignored);throw;}
  replaying=previous;std::error_code ignored;fs::remove_all(isolated,ignored);return result;
}
Json failure_tool_definition() {
  const Json text={{"type","string"}},hash={{"type","string"},{"pattern","^[a-f0-9]{64}$"}},boolean={{"type","boolean"}},integer={{"type","integer"}};
  const auto error=object({{"code",text},{"message",text},{"details",{{"type","object"}}}},{"code","message","details"});
  const auto budget=object({{"timeout_ms",{{"type","integer"},{"minimum",1},{"maximum",maximum_job_timeout_ms}}},{"memory_mb",{{"type","integer"},{"minimum",128},{"maximum",2147483647}}}},Json::array());
  Json identity={{"failure_id",{{"type","string"},{"pattern","^[A-Za-z][A-Za-z0-9_-]{0,63}$"}}},{"path",text},{"sha256",hash}};
  auto report=object({{"format",{{"const","agentcad-failure/1"}}},{"failure_id",text},{"created_at_unix_ms",integer},{"native_build",text},{"kernel_version",text},{"service_version",text},
    {"error",error},{"context",{{"type","object"}}},{"budget",budget},{"model_sha256",hash},{"request_sha256",hash},{"replayable",boolean},{"replay_limitation",text},
    {"payload",object({{"path",{{"const","payload.json"}}},{"sha256",hash},{"bytes",{{"type","integer"},{"minimum",1},{"maximum",package_limit}}}},{"path","sha256","bytes"})}},
    {"format","failure_id","created_at_unix_ms","native_build","kernel_version","service_version","error","context","budget","model_sha256","request_sha256","replayable","replay_limitation","payload"});
  auto entry=identity;entry.update({{"created_at_unix_ms",integer},{"error",error},{"replayable",boolean}});
  auto get=identity;get["report"]=report;
  const auto list=object({{"failures",{{"type","array"},{"items",object(entry,{"failure_id","path","sha256","created_at_unix_ms","error","replayable"})},{"maxItems",32}}},
    {"unreadable",{{"type","array"},{"items",object({{"failure_id",text},{"message",text}},{"failure_id","message"})},{"maxItems",32}}},
    {"limit",{{"type","integer"},{"minimum",1},{"maximum",32}}},{"truncated",boolean}},{"failures","unreadable","limit","truncated"});
  const auto replay=object({{"failure_id",text},{"package_sha256",hash},{"original_build",text},{"replay_build",text},{"same_build",boolean},{"budget",budget},
    {"original_error",error},{"observed_error",{{"oneOf",Json::array({error,Json{{"type","null"}}})}}},{"outcome",{{"enum",{"reproduced","different_failure","succeeded"}}}},{"summary",{{"type","object"}}}},
    {"failure_id","package_sha256","original_build","replay_build","same_build","budget","original_error","observed_error","outcome"});
  Json variants=Json::array({object({{"action",{{"const","list"}}},{"limit",{{"type","integer"},{"minimum",1},{"maximum",32}}}},{"action"})});
  for(const auto* action:{"get","replay"})for(bool by_id:{true,false}) {
    Json props={{"action",{{"const",action}}}};Json required={"action"};
    if(by_id){props["failure_id"]=identity.at("failure_id");required.push_back("failure_id");}
    else {props["path"]=text;props["expected_sha256"]=hash;required.push_back("path");required.push_back("expected_sha256");}
    if(std::string(action)=="replay")props["budget"]=budget;
    variants.push_back(object(props,required));
  }
  return {{"name","cad_failure"},{"description","Inspect local geometry failure packages or replay their checksummed captured inputs in an isolated temporary workspace. Replay never edits saved documents or reruns the original Service mutation. Reports retain native build, error, budget and originating request. Use cad_job for bounded asynchronous replay. No uploads or arbitrary code execution."},
    {"inputSchema",{{"type","object"},{"oneOf",variants}}},{"outputSchema",{{"type","object"},{"oneOf",Json::array({list,object(get,{"failure_id","path","sha256","report"}),replay})}}},
    {"annotations",{{"readOnlyHint",false},{"destructiveHint",false},{"openWorldHint",false}}}};
}
}
