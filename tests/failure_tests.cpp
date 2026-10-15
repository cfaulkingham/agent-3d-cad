#include "agentcad/failure.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/mcp.hpp"
#include "agentcad/service.hpp"
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>
using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
struct Temp {fs::path path=fs::canonical(temporary_directory(fs::temp_directory_path()));~Temp(){std::error_code ignored;fs::remove_all(path,ignored);}};
Error failed(const std::function<void()>& fn){try{fn();}catch(const Error& e){++checks;return e;}throw std::runtime_error("Expected failure");}
Json box(){return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",Json::array({{{"id","base"},{"type","box"},{"size",{10,20,30}}}})},{"output","base"}};}
Json done(Service& service,const std::string& id){for(int i=0;i<1000;++i){try{auto job=service.call("cad_job",{{"action","get"},{"job_id",id}});const auto state=text_field(job,"state");if(state=="succeeded"||state=="failed"||state=="cancelled")return job;}catch(const Error& e){if(e.code!="workspace_busy")throw;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}throw std::runtime_error("Replay job did not finish");}
}
int main(){try{
  configure_kernel_logging();set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));Temp temp;Service service(temp.path);
  service.call("cad_create",{{"document_id","part"},{"model",box()}});const auto before=service.call("cad_read",{{"document_id","part"}});
  const Json operations=Json::array({{{"op","add_feature"},{"feature",{{"id","bad_round"},{"type","fillet"},{"input","base"},{"edges","all"},{"radius",100}}}},{{"op","set_output"},{"feature_id","bad_round"}}});
  const Json edit={{"document_id","part"},{"expected_revision",1},{"operations",operations}};
  const auto error=failed([&]{service.call("cad_apply",edit);});
  require(error.details.contains("failure_package"),"Real failed geometry produces a replay package");
  const auto ref=error.details.at("failure_package");const auto id=text_field(ref,"failure_id");
  const auto get=service.call("cad_failure",{{"action","get"},{"failure_id",id}});const auto report=get.at("report");
  require(report.at("error").at("code")==error.code&&report.at("context").at("tool")=="cad_apply"&&report.at("context").at("arguments")==edit,"Report retains original error and exact triggering edit");
  require(report.at("budget")==current_job_budget()&&report.at("replayable")==true,"Report preserves original worker limits and replay support");
  require(get.at("sha256")==ref.at("sha256"),"Failure error links to exact report bytes");
  require(parse_payload_json(read_text(path_from_utf8(text_field(ref,"path")).parent_path()/"payload.json")).at("source")==before,"Package preserves the exact pre-edit source as well as the failing candidate");
  require(service.call("cad_read",{{"document_id","part"}})==before,"Failed source edit leaves HEAD unchanged");
  const auto replay=service.call("cad_failure",{{"action","replay"},{"failure_id",id}});
  require(replay.at("outcome")=="reproduced"&&replay.at("same_build")==true,"Captured candidate reproduces the actual geometry failure on the same build");
  require(service.call("cad_read",{{"document_id","part"}})==before,"Replay never reapplies the failed edit");
  require(service.call("cad_failure",{{"action","list"}}).at("failures").size()==1,"Replay failures do not recursively capture more packages");
  require(fs::is_empty(temp.path/".replays"),"Replay scratch files are removed");
  service.call("cad_job",{{"action","submit"},{"request_id","replay_failure"},{"tool","cad_failure"},{"arguments",{{"action","replay"},{"failure_id",id}}},{"budget",{{"timeout_ms",5000},{"memory_mb",1024}}}});
  const auto job=done(service,"replay_failure");
  require(job.at("state")=="succeeded"&&job.at("result").at("outcome")=="reproduced"&&job.at("result").at("budget")==job.at("budget"),"Durable replay returns its diagnostic outcome under the job's budget");
  require(service.call("cad_job",{{"action","submit"},{"request_id","replay_failure"},{"tool","cad_failure"},{"arguments",{{"action","replay"},{"failure_id",id}}}}).at("result")==job.at("result"),"Replay jobs preserve identical durable results");
  Temp portable;const auto copy=portable.path/"copy";fs::copy(path_from_utf8(text_field(ref,"path")).parent_path(),copy,fs::copy_options::recursive);
  Service fresh(portable.path);const Json external={{"action","replay"},{"path",path_to_utf8(copy/"report.json")},{"expected_sha256",ref.at("sha256")}};
  require(fresh.call("cad_failure",external).at("outcome")=="reproduced"&&fresh.call("cad_list",Json::object()).at("documents").empty(),"Portable replay requires no original document, service memory or source paths");
  auto bad_hash=external;bad_hash["expected_sha256"]=std::string(64,'0');require(failed([&]{fresh.call("cad_failure",bad_hash);}).code=="artifact_mismatch","Tampered report identity is rejected before execution");
  const auto payload_raw=read_text(copy/"payload.json");atomic_text(copy/"payload.json",payload_raw+" ");
  require(failed([&]{fresh.call("cad_failure",external);}).code=="artifact_mismatch","Modified payload bytes are rejected before execution");atomic_text(copy/"payload.json",payload_raw);
  auto different=report;different["native_build"]="earlier-native-build";different["error"]["message"]="another failure";atomic_text(copy/"report.json",different.dump());auto different_args=external;different_args["expected_sha256"]=sha256(different.dump());
  const auto observed=fresh.call("cad_failure",different_args);require(observed.at("outcome")=="different_failure"&&!observed.at("same_build").get<bool>(),"Replay distinguishes a different error and changed native build");
  const auto output=temp.path/"untouched.step";atomic_text(output,"original caller bytes");
  const auto synthetic=capture_geometry_failure(temp.path,box(),{{"kind","export"},{"format","step"},{"path",path_to_utf8(output)}},current_job_budget(),Error("kernel_failure","previous build failed"));
  const auto fixed=service.call("cad_failure",{{"action","replay"},{"failure_id",synthetic.details.at("failure_package").at("failure_id")}});
  require(fixed.at("outcome")=="succeeded"&&read_text(output)=="original caller bytes","Successful replay redirects every output into temporary scratch");
  const auto unavailable=capture_geometry_failure(temp.path,box(),{{"kind","gcode_review"},{"path",path_to_utf8(output)}},current_job_budget(),Error("parse_failure","external bytes unavailable"));
  const auto unsupported_id=unavailable.details.at("failure_package").at("failure_id");
  require(service.call("cad_failure",{{"action","get"},{"failure_id",unsupported_id}}).at("report").at("replayable")==false,"Uncaptured external file reads are explicitly not replayable");
  require(failed([&]{service.call("cad_failure",{{"action","replay"},{"failure_id",unsupported_id}});}).code=="unsupported_replay","Replay never reopens uncaptured external inputs");
  for(const auto& args:Json::array({Json{{"action","get"},{"failure_id","../escape"}},Json{{"action","get"},{"failure_id",id},{"budget",Json::object()}},Json{{"action","replay"},{"failure_id",id},{"budget",{{"timeout_ms",0}}}}}))require(failed([&]{service.call("cad_failure",args);}).code=="invalid_argument","Malformed replay arguments fail admission");
  require(failed([&]{dispatch_job(temp.path,{{"action","submit"},{"request_id","bad_replay"},{"tool","cad_failure"},{"arguments",Json::array()}});}).code=="invalid_argument","Direct job admission rejects non-object replay arguments with a domain error");
#ifndef _WIN32
  const auto alias=portable.path/"alias";fs::create_directory_symlink(copy,alias);
  require(failed([&]{fresh.call("cad_failure",{{"action","get"},{"path",path_to_utf8(alias/"report.json")},{"expected_sha256",sha256(different.dump())}});}).code=="storage_error","A symlinked package path is rejected before reading");
#endif
  Temp blocked;Service blocked_service(blocked.path);atomic_text(blocked.path/"failures","not a directory");
  const auto original=Error("kernel_failure","keep this error",{{"feature_id","original"}});
  const auto uncaptured=capture_geometry_failure(blocked.path,box(),{{"kind","summary"}},current_job_budget(),original);
  require(uncaptured.code==original.code&&std::string(uncaptured.what())==original.what()&&uncaptured.details.at("feature_id")=="original"&&uncaptured.details.contains("failure_capture"),"Diagnostic storage failure never masks the original modeling error");
  for(int i=0;i<34;++i)require(capture_geometry_failure(temp.path,box(),{{"kind","summary"}},current_job_budget(),original).details.contains("failure_package"),"Retention capture succeeds");
  require(service.call("cad_failure",{{"action","list"}}).at("failures").size()==32,"Automatic retention bounds generated packages to 32");
  require(service.call("cad_failure",{{"action","list"},{"limit",1}}).at("truncated")==true,"Bounded discovery reports omitted packages");
  require(service.call("cad_read",{{"document_id","part"}})==before,"All diagnostics preserve saved source and revision");
  std::cout<<"failure: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;}}
