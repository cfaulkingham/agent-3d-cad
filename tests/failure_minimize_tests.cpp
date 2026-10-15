#include "agentcad/failure.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>
using namespace agentcad;
namespace {
int checks=0;
void require(bool ok,const std::string& message){++checks;if(!ok)throw std::runtime_error(message);}
struct Temp {fs::path path=fs::canonical(temporary_directory(fs::temp_directory_path()));~Temp(){std::error_code e;fs::remove_all(path,e);}};
Error failed(const std::function<void()>& fn){try{fn();}catch(const Error& e){++checks;return e;}throw std::runtime_error("Expected failure");}
Json source(){return parse_json(R"({"schema_version":1,"units":"mm","parameters":{"width":10,"unused":42},"features":[
 {"id":"unused_base","type":"box","size":[2,3,4]},
 {"id":"unused_copy","type":"transform","input":"unused_base","translation":[20,0,0]},
 {"id":"base","type":"box","size":[{"parameter":"width"},20,30]},
 {"id":"moved","type":"transform","input":"base","translation":[0,0,5]}],"output":"moved"})");}
Json wait(Service& service,const std::string& id){for(int i=0;i<2000;++i){try{auto j=service.call("cad_job",{{"action","get"},{"job_id",id}});if(j.at("state")=="succeeded"||j.at("state")=="failed"||j.at("state")=="cancelled")return j;}catch(const Error& e){if(e.code!="workspace_busy")throw;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}throw std::runtime_error("Minimizer job deadline");}
}
int main(){try{
 configure_kernel_logging();set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));Temp t;Service service(t.path);
 service.call("cad_create",{{"document_id","part"},{"model",source()}});const auto before=service.call("cad_read",{{"document_id","part"}});
 const Json edits=Json::array({{{"op","add_feature"},{"feature",{{"id","bad_round"},{"type","fillet"},{"input","moved"},{"edges","all"},{"radius",100}}}},
   {{"op","add_feature"},{"feature",{{"id","tail"},{"type","transform"},{"input","bad_round"},{"translation",{10,0,0}}}}},{{"op","set_output"},{"feature_id","tail"}}});
 const auto error=failed([&]{service.call("cad_apply",{{"document_id","part"},{"expected_revision",1},{"operations",edits}});});
 const auto ref=error.details.at("failure_package");const auto id=ref.at("failure_id");const auto path=path_from_utf8(text_field(ref,"path"));
 const auto original_report=read_text(path),original_payload=read_text(path.parent_path()/"payload.json");
 const auto result=service.call("cad_failure",{{"action","minimize"},{"failure_id",id}});
 require(result.at("outcome")=="reduced"&&result.at("stop_reason")=="fixed_point","Real feature failure reaches a supported fixed point");
 require(result.at("before").at("features")==6&&result.at("after").at("features")==2,"Unused branch, output suffix and transform are removed");
 require(result.at("after").at("parameters")==1&&result.at("removed_parameters")==Json::array({"unused"}),"Only unused parameters are removed");
 require(result.at("attempts")<=result.at("max_attempts")&&result.at("evaluations")<=result.at("attempts"),"Validation and worker trials share the attempt cap");
 const auto reduced=result.at("minimized_package");const auto reduced_path=path_from_utf8(text_field(reduced,"path"));
 const auto reduced_payload=parse_payload_json(read_text(reduced_path.parent_path()/"payload.json"));const auto model=reduced_payload.at("model");validate_model(model);
 require(model.at("features")[0].at("id")=="base"&&model.at("features")[1].at("id")=="bad_round"&&model.at("features")[1].at("input")=="base"&&model.at("output")=="bad_round","Bypass rewrites dependency and output while preserving failure identity");
 require(reduced_payload.at("source").is_null(),"Derived package does not duplicate unrelated original source");
 require(service.call("cad_failure",{{"action","replay"},{"failure_id",reduced.at("failure_id")}}).at("outcome")=="reproduced","Reduced package reproduces through the public replay path");
 const auto minimal=service.call("cad_failure",{{"action","minimize"},{"failure_id",reduced.at("failure_id")}});
 require(minimal.at("outcome")=="unchanged"&&!minimal.contains("minimized_package"),"Already reduced model produces no redundant package");
 require(read_text(path)==original_report&&read_text(path.parent_path()/"payload.json")==original_payload,"Original package remains byte-for-byte unchanged");
 require(service.call("cad_read",{{"document_id","part"}})==before,"Minimization never edits HEAD or history");
 require(fs::is_empty(t.path/".replays"),"Every trial's cold workspace is cleaned up");
 const auto limited=service.call("cad_failure",{{"action","minimize"},{"failure_id",id},{"max_attempts",1}});
 require(limited.at("outcome")=="unchanged"&&limited.at("stop_reason")=="attempt_limit"&&limited.at("attempts")==1,"Baseline consumes an attempt and exhausted search makes no minimality claim");
 auto partial_model=model;partial_model["features"].insert(partial_model["features"].begin(),source().at("features")[0]);
 const auto partial_error=failed([&]{service.call("cad_create",{{"document_id","partial"},{"model",partial_model}});});
 const auto partial=service.call("cad_failure",{{"action","minimize"},{"failure_id",partial_error.details.at("failure_package").at("failure_id")},{"max_attempts",2}});
 require(partial.at("outcome")=="reduced"&&partial.at("stop_reason")=="attempt_limit"&&partial.at("attempts")==2,"Attempt limit returns a best verified partial reduction");
 require(service.call("cad_failure",{{"action","replay"},{"failure_id",partial.at("minimized_package").at("failure_id")}}).at("outcome")=="reproduced","Partial reduction remains independently replayable");
 const auto timed=service.call("cad_failure",{{"action","minimize"},{"failure_id",id},{"budget",{{"timeout_ms",1}}}});
 require(timed.at("stop_reason")=="time_limit"&&!timed.contains("minimized_package"),"Aggregate deadline never accepts a timeout as the modeling failure");
 Temp portable;const auto copy=portable.path/"copy";fs::copy(reduced_path.parent_path(),copy,fs::copy_options::recursive);Service fresh(portable.path);
 const Json external={{"action","minimize"},{"path",path_to_utf8(copy/"report.json")},{"expected_sha256",reduced.at("sha256")}};
 require(fresh.call("cad_failure",external).at("outcome")=="unchanged"&&fresh.call("cad_list",Json::object()).at("documents").empty(),"Relocated minimization needs no original workspace");
 auto report=parse_json(read_text(copy/"report.json"));report["error"]["message"]="different signature";atomic_text(copy/"report.json",report.dump());auto mismatch=external;mismatch["expected_sha256"]=sha256(report.dump());
 const auto rejected=fresh.call("cad_failure",mismatch);
 require(rejected.at("outcome")=="not_reproduced"&&rejected.at("stop_reason")=="baseline_not_reproduced"&&rejected.at("attempts")==1,"Different baseline failure stops before reducing");
 require(failed([&]{fresh.call("cad_failure",external);}).code=="artifact_mismatch","Minimization verifies package integrity before running");
 for(const auto& invalid:Json::array({Json{{"action","minimize"},{"failure_id",id},{"max_attempts",0}},Json{{"action","minimize"},{"failure_id",id},{"max_attempts",513}},Json{{"action","minimize"},{"failure_id",id},{"max_attempts",1.5}},Json{{"action","replay"},{"failure_id",id},{"max_attempts",1}}}))
   require(failed([&]{service.call("cad_failure",invalid);}).code=="invalid_argument","Strict minimizer admission");
 const auto timeout=capture_geometry_failure(t.path,source(),{{"kind","summary"}},current_job_budget(),Error("job_timeout","timeout",{{"feature_id","base"}}));
 require(failed([&]{service.call("cad_failure",{{"action","minimize"},{"failure_id",timeout.details.at("failure_package").at("failure_id")}});}).code=="unsupported_minimization","Timing-dependent errors are explicitly excluded");
 const Json arguments={{"action","minimize"},{"failure_id",id}};
 service.call("cad_job",{{"action","submit"},{"request_id","reduce"},{"tool","cad_failure"},{"arguments",arguments},{"budget",{{"timeout_ms",15000},{"memory_mb",1024}}}});
 const auto job=wait(service,"reduce");require(job.at("state")=="succeeded"&&job.at("result").at("outcome")=="reduced"&&job.at("result").at("budget")==job.at("budget"),"Durable minimization uses the enclosing budget");
 require(service.call("cad_job",{{"action","submit"},{"request_id","reduce"},{"tool","cad_failure"},{"arguments",arguments}}).at("result")==job.at("result"),"Durable retry returns the existing reduced artifact without another search");
 service.call("cad_job",{{"action","submit"},{"request_id","cancel_reduce"},{"tool","cad_failure"},{"arguments",arguments}});
 service.call("cad_job",{{"action","cancel"},{"job_id","cancel_reduce"}});
 require(wait(service,"cancel_reduce").at("state")=="cancelled","Cancellation stops queued minimization before publication");
 const auto packages_before=service.call("cad_failure",{{"action","list"}}).at("failures").size();
 service.call("cad_job",{{"action","submit"},{"request_id","cancel_active_reduce"},{"tool","cad_failure"},{"arguments",arguments}});
 bool trial_started=false;
 for(int i=0;i<5000;++i){if(!fs::is_empty(t.path/".replays")){trial_started=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 require(trial_started,"Cancellation fixture reached an active cold trial");
 service.call("cad_job",{{"action","cancel"},{"job_id","cancel_active_reduce"}});
 require(wait(service,"cancel_active_reduce").at("state")=="cancelled"&&fs::is_empty(t.path/".replays"),"Cancellation terminates an active trial and cleans its workspace");
 require(service.call("cad_failure",{{"action","list"}}).at("failures").size()==packages_before,"Cancelled search publishes no partial package or recursive capture");
 // Fill retention without expiring the original, then ensure publication protects it.
 while(service.call("cad_failure",{{"action","list"}}).at("failures").size()<32)
   capture_geometry_failure(t.path,source(),{{"kind","summary"}},current_job_budget(),Error("kernel_failure","fixture"));
 directory(t.path/"failures"/"path-normalization");
 auto alias=id.get<std::string>();for(auto& c:alias)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');
 if(!fs::exists(t.path/"failures"/alias))alias=id.get<std::string>();
 const auto retained=service.call("cad_failure",{{"action","minimize"},{"path",path_to_utf8(t.path/"failures"/"path-normalization"/".."/alias/"report.json")},{"expected_sha256",ref.at("sha256")}});
 require(retained.at("outcome")=="reduced"&&read_text(path)==original_report&&service.call("cad_failure",{{"action","list"}}).at("failures").size()==32,"Derived package retention protects its original while respecting count limit");
 require(service.call("cad_read",{{"document_id","part"}})==before,"All minimizer paths preserve source");
 std::cout<<"failure_minimize: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;}}
