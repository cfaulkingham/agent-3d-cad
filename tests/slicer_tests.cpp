#include "agentcad/hash.hpp"
#include "agentcad/gcode.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/service.hpp"
#include <functional>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <signal.h>
#include <unistd.h>
#endif
using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
void fails(const std::string& code,const std::function<void()>& action){try{action();throw std::runtime_error("Expected "+code);}catch(const Error& e){require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what());}}
struct Temp{fs::path path=temporary_directory(fs::temp_directory_path());~Temp(){std::error_code ignored;fs::remove_all(path,ignored);}};
Json call(Service& service,const char* tool,const Json& arguments) {
  for(int i=0;i<1000;++i){try{return service.call(tool,arguments);}catch(const Error& e){if(e.code!="workspace_busy")throw;}std::this_thread::sleep_for(std::chrono::milliseconds(2));}
  throw std::runtime_error("Workspace stayed busy");
}
Json terminal(Service& service,const std::string& id){for(int i=0;i<6000;++i){const auto r=call(service,"cad_job",{{"action","get"},{"job_id",id}});if(r.at("state")!="queued"&&r.at("state")!="running"&&r.at("state")!="cancelling")return r;std::this_thread::sleep_for(std::chrono::milliseconds(2));}throw std::runtime_error("Job stayed live");}
std::set<std::string> published(const fs::path& workspace){std::set<std::string> result;for(const auto& f:fs::directory_iterator(workspace/"exports"))if(!f.path().filename().string().starts_with(".pending-"))result.insert(path_to_utf8(f.path().filename()));return result;}
Json options(const fs::path& root,const std::string& name="Normal",fs::path executable=path_from_utf8(CAD_SLICER_FIXTURE)) {
  directory(root/"profiles");
  const Json machine={{"type","machine"},{"name",name},{"gcode_flavor","marlin"},{"printer_technology","FFF"}};
  const Json process={{"type","process"},{"name","Fixture process"},{"compatible_printers",{name}},{"post_process",{""}}};
  const Json filament={{"type","filament"},{"name","Fixture filament"},{"compatible_printers",{name}}};
  Json profiles=Json::object();
  for(const auto& [role,value]:std::vector<std::pair<std::string,Json>>{{"machine",machine},{"process",process},{"filament",filament}}) {
    const auto raw=value.dump();const auto path=root/"profiles"/(role+".json");atomic_text(path,raw);profiles[role]={{"path",path_to_utf8(path)},{"expected_sha256",sha256(raw)}};
  }
  return {{"backend","orcaslicer"},{"version","2.4.2"},{"executable",{{"path",path_to_utf8(fs::absolute(executable))},{"expected_sha256",sha256_file(executable,512*1024*1024)}}},
    {"profiles",profiles},{"bed_type","High Temp Plate"},{"review",{{"firmware","marlin"},
      {"machine",{{"name","Fixture limits"},{"motion_bounds_mm",{{0,100},{0,100},{0,100}}}}},
      {"material",{{"name","Fixture targets"},{"nozzle_temperature_c",{190,230}},{"bed_temperature_c",{50,70}}}},
      {"initial",{{"units","mm"},{"xyz_mode","absolute"},{"extrusion_mode","absolute"},{"position_mm",{0,0,0}},{"extruder_mm",0}}}}}};
}
Json plan(Service& service,const Json& opts) {return call(service,"cad_slice",{{"document_id","part"},{"revision",1},{"action","plan"},{"feature_id","small"},{"options",opts}});}
Json run_args(const Json& planned){return {{"document_id","part"},{"revision",1},{"action","run"},{"plan_path",planned.at("path")},{"expected_sha256",planned.at("sha256")}};}
Json verify(const Json& result) {
  const auto path=path_from_utf8(text_field(result,"path"));const auto manifest=parse_json(read_text(path));
  std::set<std::string> found;
  for(const auto& item:manifest.at("artifacts")) {
    const auto name=text_field(item,"path");const auto relative=path_from_utf8(name);
    require(name.find('\\')==std::string::npos&&!relative.is_absolute()&&relative.lexically_normal()==relative,"Portable manifest paths use forward slashes on every platform");
    const auto file=path.parent_path()/relative;require(fs::file_size(file)==item.at("bytes")&&sha256_file(file,128*1024*1024)==text_field(item,"sha256"),"Actual size/hash of each package artifact");found.insert(text_field(item,"path"));
  }
  for(const auto& item:fs::recursive_directory_iterator(path.parent_path()))if(item.is_regular_file()&&item.path()!=path) {
    const auto relative=item.path().lexically_relative(path.parent_path()).generic_u8string();
    require(found.contains(std::string(reinterpret_cast<const char*>(relative.data()),relative.size())),"Every published file is in the manifest");
  }
  for(const auto* name:{"profiles/machine.json","reviewed-plan/profiles/machine.json","output/plate_1.gcode"})
    require(found.contains(name),"Nested artifact paths retain the portable package layout");
  return manifest;
}
bool alive(std::uint64_t pid) {
#ifdef _WIN32
  HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,static_cast<DWORD>(pid));if(!process)return false;const bool running=WaitForSingleObject(process,0)==WAIT_TIMEOUT;CloseHandle(process);return running;
#else
  if(::kill(static_cast<pid_t>(pid),0)!=0)return false;
#ifdef __linux__
  std::ifstream state("/proc/"+std::to_string(pid)+"/stat");std::string line;std::getline(state,line);
  const auto end=line.rfind(')');if(end!=std::string::npos&&end+2<line.size()&&line[end+2]=='Z')return false;
#endif
  return true;
#endif
}
void terminate(std::uint64_t pid) {
#ifdef _WIN32
  HANDLE process=OpenProcess(PROCESS_TERMINATE,FALSE,static_cast<DWORD>(pid));require(process!=nullptr,"Open the observed process");
  const bool killed=TerminateProcess(process,99);CloseHandle(process);require(killed,"Terminate the observed native process");
#else
  require(::kill(static_cast<pid_t>(pid),SIGKILL)==0,"Terminate the observed native process");
#endif
}
Json active_process(const fs::path& workspace) {
  for(int i=0;i<1000;++i) {
    for(const auto& f:fs::directory_iterator(workspace/".workers"))if(f.is_directory()&&fs::is_regular_file(f.path()/"external-process.json")&&fs::is_regular_file(f.path()/"process.json")) {
      const auto request=parse_json(read_text(f.path()/"input.json"));if(request.value("log_prefix",Json())!="slice")continue;
      const auto supervisor=parse_json(read_text(f.path()/"process.json")).at("pid");if(!alive(supervisor.get<std::uint64_t>()))continue;
      return {{"supervisor",supervisor},{"coordinator",request.at("parent_pid")},{"slicer",parse_json(read_text(f.path()/"external-process.json")).at("pid")}};
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  throw std::runtime_error("No actual native process supervisor observed");
}
fs::path descendant_file(const fs::path& workspace) {
  for(int i=0;i<3000;++i){for(const auto& f:fs::directory_iterator(workspace/"exports"))if(f.is_directory()&&f.path().filename().string().starts_with(".pending-")) {
    const auto path=f.path()/"descendant.pid";if(fs::is_regular_file(path)&&fs::file_size(path)>0)return path;
  }std::this_thread::sleep_for(std::chrono::milliseconds(2));}throw std::runtime_error("No live fixture descendant observed");
}
void gone(std::uint64_t pid){for(int i=0;i<500&&alive(pid);++i)std::this_thread::sleep_for(std::chrono::milliseconds(5));require(!alive(pid),"Native descendants stop after completion/cancel/failure");}
void test() {
  Temp t;const auto workspace=t.path/path_from_utf8("workspace spaces \xc3\xa9");Service service(workspace);
  const Json model={{"schema_version",1},{"units","mm"},{"parameters",Json::object()},
    {"features",Json::array({{{"id","small"},{"type","box"},{"size",{10,10,2}}},{{"id","large"},{"type","box"},{"size",{20,20,20}}},{{"id","curve"},{"type","cylinder"},{"radius",4},{"height",2}}})},{"output","large"}};
  call(service,"cad_create",{{"document_id","part"},{"model",model}});const auto before=call(service,"cad_read",{{"document_id","part"}});
  auto opts=options(t.path);const auto planned=plan(service,opts);const auto plan_file=path_from_utf8(text_field(planned,"path"));
  require(sha256_file(plan_file,max_json_bytes)==text_field(planned,"sha256"),"Raw dry-run plan identity");
  const auto saved=parse_json(read_text(plan_file));require(saved.at("summary").at("bounds_mm").at("max")[2]==2,"Plan scopes the requested solid, not document output");
  require(planned.at("physical_print_started")==false&&planned.at("execution").at("hardware_contact")==false,"Plan never contacts hardware");
  for(const auto* role:{"machine","process","filament"})fs::remove(path_from_utf8(text_field(opts.at("profiles").at(role),"path")));
  const auto result=call(service,"cad_slice",run_args(planned));const auto manifest=verify(result);
  require(result.at("report").at("status")=="pass"&&manifest.at("printer_approval")=="not_evaluated","Native process review preserves measured scope without print approval");
  require(parse_json(read_text(path_from_utf8(text_field(result,"directory"))/"execution.json")).at("slice").at("exit_status")==0,"Actual native child completed");
  const auto executed=parse_json(read_text(path_from_utf8(text_field(result,"directory"))/"execution.json"));
  const auto retained=path_from_utf8(text_field(result,"directory"))/"reviewed-plan";
  require(read_text(retained/"plan.json")==read_text(plan_file),"Complete reviewed plan remains independently portable");
  require(executed.at("executed_mesh_sha256")==sha256_file(path_from_utf8(text_field(result,"directory"))/"input.stl",gcode_bytes_limit),"Execution identifies the actual regenerated native mesh");
  require(call(service,"cad_read",{{"document_id","part"}})==before,"Plan and execution preserve HEAD/history");
  const Json request={{"action","submit"},{"request_id","slice_replay"},{"tool","cad_slice"},{"arguments",run_args(planned)}};
  call(service,"cad_job",request);const auto done=terminal(service,"slice_replay");require(done.at("state")=="succeeded","Slicing runs as a durable async job");
  fs::remove(plan_file);require(call(service,"cad_job",request).at("result")==done.at("result"),"Job replay survives removed caller plan");
  opts=options(t.path);fs::remove_all(workspace/".cache");
  const auto curved=call(service,"cad_slice",{{"document_id","part"},{"revision",1},{"action","plan"},{"feature_id","curve"},{"options",opts}});
  const auto curved_result=call(service,"cad_slice",run_args(curved));verify(curved_result);
  require(curved_result.at("feature_id")=="curve","Cold plan and warm run preserve exact curved source scope");
  auto reviewed=parse_json(read_text(path_from_utf8(text_field(curved,"path"))));const auto original_plan=reviewed.dump();
  auto forged=run_args(curved);reviewed["execution"]["argv_template"].push_back("--unreviewed");atomic_text(path_from_utf8(text_field(curved,"path")),reviewed.dump());forged["expected_sha256"]=sha256(reviewed.dump());
  fails("artifact_mismatch",[&]{call(service,"cad_slice",forged);});
  reviewed=parse_json(original_plan);reviewed["summary"]["volume_mm3"]=reviewed.at("summary").at("volume_mm3").get<double>()+1;atomic_text(path_from_utf8(text_field(curved,"path")),reviewed.dump());forged["expected_sha256"]=sha256(reviewed.dump());
  fails("artifact_mismatch",[&]{call(service,"cad_slice",forged);});
  opts=options(t.path,"BadBounds");auto checked=plan(service,opts);auto bounds=call(service,"cad_slice",run_args(checked));require(bounds.at("report").at("status")=="fail","Failed G-code bounds remain failed in completed package");verify(bounds);
  auto bad=run_args(checked);bad["expected_sha256"]=std::string(64,'a');fails("artifact_mismatch",[&]{call(service,"cad_slice",bad);});
  const auto artifacts=published(workspace);auto changed=read_text(path_from_utf8(text_field(checked,"directory"))/"profiles/machine.json");atomic_text(path_from_utf8(text_field(checked,"directory"))/"profiles/machine.json",changed+" ");
  fails("artifact_mismatch",[&]{call(service,"cad_slice",run_args(checked));});require(published(workspace)==artifacts,"Changed reviewed profile publishes nothing");
  opts=options(t.path);bad={{"document_id","part"},{"revision",1},{"action","plan"},{"options",opts},{"shell","bad"}};fails("invalid_argument",[&]{call(service,"cad_slice",bad);});
  const auto process=path_from_utf8(text_field(opts.at("profiles").at("process"),"path"));auto profile=parse_json(read_text(process));profile["post_process"]={"echo bad"};atomic_text(process,profile.dump());opts["profiles"]["process"]["expected_sha256"]=sha256(profile.dump());
  fails("invalid_argument",[&]{plan(service,opts);});
  for(const auto* mode:{"Failure","NoOutput","WrongSettings"}){checked=plan(service,options(t.path,mode));const auto old=published(workspace);fails(mode==std::string("Failure")?"slicer_failed":mode==std::string("WrongSettings")?"artifact_mismatch":"invalid_argument",[&]{call(service,"cad_slice",run_args(checked));});require(published(workspace)==old,"Failed native slicer publishes no package");}
  const auto wrong=t.path/("wrong-version"+path_from_utf8(CAD_SLICER_FIXTURE).extension().string());fs::copy_file(path_from_utf8(CAD_SLICER_FIXTURE),wrong);checked=plan(service,options(t.path,"Normal",wrong));fails("slicer_version_mismatch",[&]{call(service,"cad_slice",run_args(checked));});
  checked=plan(service,options(t.path,"Lingering"));const auto lingering=call(service,"cad_slice",run_args(checked));gone(std::stoull(read_text(path_from_utf8(text_field(lingering,"directory"))/"descendant.pid")));verify(lingering);
  checked=plan(service,options(t.path,"Slow"));const auto old=published(workspace);
  call(service,"cad_job",{{"action","submit"},{"request_id","slice_cancel"},{"tool","cad_slice"},{"arguments",run_args(checked)}});
  const auto file=descendant_file(workspace);const auto pid=std::stoull(read_text(file));require(alive(pid),"Cancellation observes an actually live native descendant");
  call(service,"cad_job",{{"action","cancel"},{"job_id","slice_cancel"}});require(terminal(service,"slice_cancel").at("state")=="cancelled","Cancel stops the installed-slicer process job");gone(pid);require(published(workspace)==old,"Cancellation publishes no package");
  call(service,"cad_job",{{"action","submit"},{"request_id","slice_deadline"},{"tool","cad_slice"},{"arguments",run_args(checked)},{"budget",{{"timeout_ms",1500}}}});
  const auto timed=terminal(service,"slice_deadline");require(timed.at("state")=="failed"&&timed.at("error").at("code")=="job_timeout","Installed-slicer deadline is enforced");
  checked=plan(service,options(t.path,"Logs"));fails("limit_exceeded",[&]{call(service,"cad_slice",run_args(checked));});
  checked=plan(service,options(t.path,"Memory"));const auto memory_old=published(workspace);
  call(service,"cad_job",{{"action","submit"},{"request_id","slice_memory"},{"tool","cad_slice"},{"arguments",run_args(checked)},{"budget",{{"memory_mb",128},{"timeout_ms",10000}}}});
  const auto memory=terminal(service,"slice_memory");
  require(memory.at("state")=="failed"&&(memory.at("error").at("code")=="memory_limit"||memory.at("error").at("code")=="worker_failed"||memory.at("error").at("code")=="slicer_failed"),"Actual allocation over budget fails under native platform containment");require(published(workspace)==memory_old,"Memory exhaustion publishes nothing");
  checked=plan(service,options(t.path,"Slow"));
  for(const auto* which:{"supervisor","coordinator"}) {
    const std::string id=std::string("slice_kill_")+which;const auto old_files=published(workspace);
    call(service,"cad_job",{{"action","submit"},{"request_id",id},{"tool","cad_slice"},{"arguments",run_args(checked)}});
    const auto descendant=std::stoull(read_text(descendant_file(workspace)));const auto native=active_process(workspace);
    require(alive(descendant)&&alive(native.at("slicer").get<std::uint64_t>()),"Crash test observes executing slicer and descendant");
    terminate(native.at(which).get<std::uint64_t>());
    const auto crashed=terminal(service,id);require(crashed.at("state")== (std::string(which)=="supervisor"?"failed":"interrupted"),"Native supervisor/coordinator death recovers explicitly");
    gone(descendant);gone(native.at("slicer").get<std::uint64_t>());require(published(workspace)==old_files,"Hard death does not publish a slicing package");
  }
  require(call(service,"cad_read",{{"document_id","part"}})==before,"All failures leave saved intent unchanged");
  for(const auto n:{0,1,55,56,63,64,65,127,65535,65536,65537}) {
    const auto text=std::string(n,'a');atomic_text(t.path/"hash.bin",text);require(sha256_file(t.path/"hash.bin",65537)==sha256(text),"Streaming SHA matches padded and multi-chunk data");
  }
  require(sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","Independent SHA-256 known answer");
  fails("limit_exceeded",[&]{sha256_file(t.path/"hash.bin",65536);});
}
}
int main(){try{set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));test();std::cout<<checks<<" slicer checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
