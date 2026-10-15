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
  Package result{fs::absolute(path).lexically_normal(),report,Json(),hash};
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
void collect(const fs::path& root,std::size_t incoming,const fs::path& protected_package={}) {
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
  auto remaining=entries.size();
  for(const auto& entry:entries) {
    if(remaining<retained_count&&total+incoming<=retained_bytes)break;
    // Filesystem identity also protects case aliases on Windows and macOS.
    if(!protected_package.empty()&&fs::equivalent(entry.path,protected_package))continue;
    fs::remove_all(entry.path);total-=entry.bytes;--remaining;
  }
}
Package publish_package(const fs::path& workspace,const Json& model,const Json& request,
                        const Json& source,const Json& error,const Json& limits,const Json& context,
                        const fs::path& protected_package={},bool cancellable=false) {
  const Json payload={{"model",model},{"request",request},{"source",source}};const auto bytes=payload.dump();
  if(bytes.size()>package_limit)throw Error("limit_exceeded","Failure payload exceeds 64 MiB");
  const auto root=workspace/"failures";plain_path(root);directory(root);WorkspaceLock lock(root);
  const auto stage=temporary_directory(root);
  try {
    const auto id="failure-"+sha256(path_to_utf8(stage)+std::to_string(now_ms())).substr(0,24);
    const auto limitation=replay_limitation(request);
    const Json report={{"format","agentcad-failure/1"},{"failure_id",id},{"created_at_unix_ms",now_ms()},
      {"native_build",AGENTCAD_CACHE_BUILD},{"kernel_version",kernel_version()},{"service_version",AGENTCAD_VERSION},
      {"error",error},{"context",context},{"budget",limits},
      {"model_sha256",sha256(model.dump())},{"request_sha256",sha256(request.dump())},
      {"replayable",limitation.empty()},{"replay_limitation",limitation},
      {"payload",{{"path","payload.json"},{"sha256",sha256(bytes)},{"bytes",bytes.size()}}}};
    const auto raw=report.dump();if(raw.size()>max_json_bytes)throw Error("limit_exceeded","Failure report exceeds 1 MiB");
    atomic_text(stage/"payload.json",bytes,package_limit);atomic_text(stage/"report.json",raw);
    // Failure capture must still work after a job deadline. Only a derived
    // minimizer artifact is subject to cancellation at this publication point.
    if(cancellable)check_job_cancelled();
    collect(root,bytes.size()+raw.size(),protected_package);const auto target=root/id;fs::rename(stage,target);
    return {target,report,payload,sha256(raw)};
  }catch(...){std::error_code ignored;fs::remove_all(stage,ignored);throw;}
}
struct ReplayScope {
  bool previous=replaying;
  ReplayScope(){replaying=true;}
  ~ReplayScope(){replaying=previous;}
};
struct Scratch {
  fs::path path;
  explicit Scratch(const fs::path& parent){plain_path(parent);directory(parent);path=temporary_directory(parent);}
  ~Scratch(){std::error_code ignored;fs::remove_all(path,ignored);}
};
// Rewrite only known feature-reference positions, never arbitrary strings in
// imported bytes, parameter names, part IDs or provenance metadata. Validation
// rejects references whose operand type cannot survive the proposed bypass.
void redirect_reference(Json& feature,const std::string& from,const std::string& to) {
  for(const auto* key:{"input","left","right","target","path"})
    if(feature.contains(key)&&feature.at(key)==from)feature[key]=to;
  for(const auto* key:{"inputs","sections"})if(feature.contains(key))
    for(auto& value:feature[key])if(value==from)value=to;
  if(feature.contains("parts"))for(auto& part:feature["parts"])if(part.at("input")==from)part["input"]=to;
  if(feature.contains("boundaries"))for(auto& boundary:feature["boundaries"])
    if(boundary.value("input",Json())==from)boundary["input"]=to;
  // Geometric selectors and constrained-curve edge references contain feature_id.
  const auto selectors=[&](auto&& self,Json& value)->void {
    if(value.is_array()){for(auto& item:value)self(self,item);}
    else if(value.is_object()) {
      if(value.value("feature_id",Json())==from)value["feature_id"]=to;
      for(auto& item:value.items())self(self,item.value());
    }
  };
  for(const auto* key:{"edges","faces","vertices","reference_face","constraints"})
    if(feature.contains(key))selectors(selectors,feature[key]);
}
Json minimize_package(const fs::path& workspace,const Package& package,const Json& args,const Json& limits) {
  const auto& original=package.payload.at("model");const auto& request=package.payload.at("request");
  const auto kind=text_field(request,"kind"),code=text_field(package.report.at("error"),"code");
  const auto failing=package.report.at("error").at("details").value("feature_id",Json());
  // Resource failures are timing-dependent, and non-model requests can carry
  // independent captured geometry. Neither is a reliable graph-reduction oracle.
  const std::set<std::string> kinds={"summary","topology","view","curve"};
  const std::set<std::string> codes={"kernel_failure","invalid_shape","invalid_model","selection_missing","selection_ambiguous","selection_count_mismatch"};
  if(!kinds.contains(kind)||!codes.contains(code)||!failing.is_string())
    throw Error("unsupported_minimization","Minimization requires a feature-qualified modeling error from summary, topology, view or curve evaluation");
  validate_model(original);
  if(std::none_of(original.at("features").begin(),original.at("features").end(),[&](const Json& f){return f.at("id")==failing;}))
    throw Error("unsupported_minimization","The failing feature is absent from the captured model");
  const auto started=std::chrono::steady_clock::now();const auto max_attempts=args.value("max_attempts",128);
  int attempts=0,evaluations=0,accepted=0;std::string stop="fixed_point";Json best=original,observed=nullptr;
  std::set<std::string> seen;
  ReplayScope replay_scope;
  const auto remaining=[&]{return limits.at("timeout_ms").get<std::int64_t>()-
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();};
  const auto available=[&]{check_job_cancelled();if(stop!="fixed_point")return false;if(remaining()<=0){stop="time_limit";return false;}
    if(attempts>=max_attempts){stop="attempt_limit";return false;}return true;};
  const auto trial=[&](const Json& candidate)->bool {
    if(!available())return false;
    if(!seen.insert(sha256(candidate.dump())).second)return false;
    ++attempts;
    try{validate_model(candidate);}catch(const Error&){return false;}
    if(remaining()<=0){stop="time_limit";return false;}
    Scratch scratch(workspace/".replays");auto budget=limits;budget["timeout_ms"]=std::max<std::int64_t>(1,remaining());++evaluations;
    try{evaluate_failure_replay(scratch.path,candidate,request,budget);observed=nullptr;}
    catch(const Error& e) {
      check_job_cancelled();if(e.code=="job_cancelled")throw;
      observed=e.json();
      if(e.code=="job_timeout"){stop="time_limit";return false;}
      return error_identity(observed)==error_identity(package.report.at("error"));
    }
    return false;
  };
  const bool reproduced=trial(best);Json retained_error=observed;
  if(reproduced) {
    const auto accept=[&](Json candidate)->bool {
      if(candidate==best||!trial(candidate))return false;
      best=std::move(candidate);retained_error=observed;++accepted;return true;
    };
    bool changed=true;
    while(changed&&available()&&stop=="fixed_point") {
      changed=false;
      // Provenance is not needed to execute a self-contained diagnostic model.
      // The complete original snapshot remains in the parent package.
      if(best.contains("components")){auto candidate=best;candidate.erase("components");if(accept(candidate)){changed=true;continue;}}
      // Large contiguous removals can eliminate dependent unused branches that
      // cannot be removed one feature at a time. Never remove the failing ID.
      for(std::size_t chunk=std::max<std::size_t>(1,best.at("features").size()/2);chunk>0&&!changed;chunk/=2) {
        for(std::size_t start=0;start<best.at("features").size()&&available();start+=chunk) {
          auto candidate=best;auto& features=candidate["features"];
          const auto end=std::min(start+chunk,features.size());bool protected_id=false;
          for(auto i=start;i<end;++i)protected_id|=features[i].at("id")==failing;
          if(protected_id)continue;
          features.erase(features.begin()+start,features.begin()+end);
          if(std::none_of(features.begin(),features.end(),[&](const Json& f){return f.at("id")==candidate.at("output");}))candidate["output"]=failing;
          if(accept(candidate)){changed=true;break;}
        }
      }
      if(changed)continue;
      // Diagnostic simplification may bypass a modifier or a Boolean operand.
      // It is accepted only after a fresh worker reproduces the same failure.
      for(std::size_t i=0;i<best.at("features").size()&&!changed&&available();++i) {
        const auto feature=best.at("features")[i];const auto id=text_field(feature,"id");if(feature.at("id")==failing)continue;
        for(const auto* key:{"input","left","right"})if(feature.contains(key)&&feature.at(key).is_string()) {
          const auto replacement=text_field(feature,key);auto candidate=best;candidate["features"].erase(candidate["features"].begin()+i);
          for(auto& f:candidate["features"])redirect_reference(f,id,replacement);
          if(candidate.at("output")==id)candidate["output"]=replacement;
          if(accept(candidate)){changed=true;break;}
        }
      }
      if(changed)continue;
      const auto parameters=best.at("parameters");
      for(const auto& item:parameters.items()) {
        if(!available())break;auto candidate=best;candidate["parameters"].erase(item.key());
        if(accept(candidate)){changed=true;break;}
      }
    }
  }
  Json removed_features=Json::array(),removed_parameters=Json::array();
  for(const auto& f:original.at("features"))if(std::none_of(best.at("features").begin(),best.at("features").end(),[&](const Json& b){return b.at("id")==f.at("id");}))removed_features.push_back(f.at("id"));
  for(const auto& p:original.at("parameters").items())if(!best.at("parameters").contains(p.key()))removed_parameters.push_back(p.key());
  const auto counts=[](const Json& model){return Json{{"features",model.at("features").size()},{"parameters",model.at("parameters").size()},{"model_bytes",model.dump().size()}};};
  Json result={{"action","minimize"},{"failure_id",package.report.at("failure_id")},{"package_sha256",package.sha256},
    {"original_build",package.report.at("native_build")},{"replay_build",AGENTCAD_CACHE_BUILD},{"same_build",package.report.at("native_build")==AGENTCAD_CACHE_BUILD},
    {"budget",limits},{"max_attempts",max_attempts},{"attempts",attempts},{"evaluations",evaluations},{"accepted_reductions",accepted},
    {"elapsed_ms",std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()},
    {"outcome",!reproduced?"not_reproduced":accepted?"reduced":"unchanged"},
    {"stop_reason",!reproduced&&stop=="fixed_point"?"baseline_not_reproduced":stop},
    {"original_error",package.report.at("error")},{"observed_error",retained_error},
    {"before",counts(original)},{"after",counts(best)},{"removed_feature_ids",removed_features},{"removed_parameters",removed_parameters},
    {"minimality","No global minimum is claimed; fixed_point exhausts only the supported reductions"}};
  if(reproduced&&accepted) {
    check_job_cancelled();
    const Json context={{"tool","cad_failure"},{"arguments",args},{"job",job_request_context()},
      {"parent_failure",location(package)},{"minimization",result}};
    result["minimized_package"]=location(publish_package(workspace,best,request,nullptr,retained_error,limits,context,package.path,true));
  }
  return result;
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
  auto details=error.details;
  try {
    auto context=FailureCallScope::context();Json source=nullptr;
    if(context.contains("arguments")) {
      const auto& args=context.at("arguments");const auto revision=args.value("expected_revision",args.value("revision",Json()));
      if(args.contains("document_id")&&!revision.is_null()) {
        try {source=Store(workspace).read(text_field(args,"document_id"),revision_number(revision));}
        catch(const std::exception& e){context["source_capture_error"]=e.what();}
      }
    }
    details["failure_package"]=location(publish_package(workspace,model,request,source,error.json(),limits,context));
  }catch(const std::exception& e) {
    details["failure_capture"]={{"state","unavailable"},{"reason",e.what()}};
  }
  return Error(error.code,error.what(),std::move(details));
}
void validate_failure_arguments(const Json& args) {
  const auto action=text_field(args,"action");
  if(action=="list") {
    fields(args,{"action"},{"limit"});if(args.contains("limit")&&(!args.at("limit").is_number_integer()||args.at("limit")<1||args.at("limit")>32))throw Error("invalid_argument","Failure list limit requires 1..32");return;
  }
  if(action!="get"&&action!="replay"&&action!="minimize")throw Error("invalid_argument","Failure action must be list, get, replay or minimize");
  fields(args,{"action"},{"failure_id","path","expected_sha256","budget","max_attempts"});
  if(args.contains("max_attempts")&&(action!="minimize"||!args.at("max_attempts").is_number_integer()||args.at("max_attempts")<1||args.at("max_attempts")>512))throw Error("invalid_argument","Only minimize accepts max_attempts, an integer in 1..512");
  if(args.contains("failure_id")) {
    identifier(text_field(args,"failure_id"));if(args.contains("path")||args.contains("expected_sha256"))throw Error("invalid_argument","Use failure_id or an explicit report path and hash");
  }else {
    const auto path=path_from_utf8(text_field(args,"path"));if(!path.is_absolute()||path.filename()!="report.json")throw Error("invalid_argument","Failure path must be an absolute report.json");digest(text_field(args,"expected_sha256"));
  }
  if(args.contains("budget")) {
    if(action!="replay"&&action!="minimize")throw Error("invalid_argument","Only replay and minimize accept a budget");
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
  if(action=="minimize")return minimize_package(workspace,package,args,limits);
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
  const auto count=Json{{"type","integer"},{"minimum",0}};
  const auto counts=object({{"features",count},{"parameters",count},{"model_bytes",count}},{"features","parameters","model_bytes"});
  const auto names=Json{{"type","array"},{"items",text},{"maxItems",256}};
  const auto minimized=object({{"action",{{"const","minimize"}}},{"failure_id",text},{"package_sha256",hash},
    {"original_build",text},{"replay_build",text},{"same_build",boolean},{"budget",budget},
    {"max_attempts",{{"type","integer"},{"minimum",1},{"maximum",512}}},{"attempts",count},{"evaluations",count},{"accepted_reductions",count},{"elapsed_ms",count},
    {"outcome",{{"enum",{"reduced","unchanged","not_reproduced"}}}},
    {"stop_reason",{{"enum",{"fixed_point","attempt_limit","time_limit","baseline_not_reproduced"}}}},
    {"original_error",error},{"observed_error",{{"oneOf",Json::array({error,Json{{"type","null"}}})}}},
    {"before",counts},{"after",counts},{"removed_feature_ids",names},{"removed_parameters",names},{"minimality",text},
    {"minimized_package",object(identity,{"failure_id","path","sha256"})}},
    {"action","failure_id","package_sha256","original_build","replay_build","same_build","budget","max_attempts","attempts","evaluations","accepted_reductions","elapsed_ms",
     "outcome","stop_reason","original_error","observed_error","before","after","removed_feature_ids","removed_parameters","minimality"});
  Json variants=Json::array({object({{"action",{{"const","list"}}},{"limit",{{"type","integer"},{"minimum",1},{"maximum",32}}}},{"action"})});
  for(const auto* action:{"get","replay","minimize"})for(bool by_id:{true,false}) {
    Json props={{"action",{{"const",action}}}};Json required={"action"};
    if(by_id){props["failure_id"]=identity.at("failure_id");required.push_back("failure_id");}
    else {props["path"]=text;props["expected_sha256"]=hash;required.push_back("path");required.push_back("expected_sha256");}
    if(std::string(action)!="get")props["budget"]=budget;
    if(std::string(action)=="minimize")props["max_attempts"]={{"type","integer"},{"minimum",1},{"maximum",512},{"default",128}};
    variants.push_back(object(props,required));
  }
  return {{"name","cad_failure"},{"description","Inspect local geometry failure packages, minimize feature-qualified modeling failures, or replay their checksummed captured inputs in an isolated temporary workspace. Replay never edits saved documents or reruns the original Service mutation. Reports retain native build, error, budget and originating request. Minimize saves a separate reduced replay package with a bounded search; it does not claim a global minimum. Use cad_job for bounded asynchronous replay or minimization. No uploads or arbitrary code execution."},
    {"inputSchema",{{"type","object"},{"oneOf",variants}}},{"outputSchema",{{"type","object"},{"oneOf",Json::array({list,object(get,{"failure_id","path","sha256","report"}),replay,minimized})}}},
    {"annotations",{{"readOnlyHint",false},{"destructiveHint",false},{"openWorldHint",false}}}};
}
}
