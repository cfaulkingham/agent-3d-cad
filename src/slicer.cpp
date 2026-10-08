#include "agentcad/slicer.hpp"
#include "agentcad/gcode.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/jobs.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
#include <set>

namespace agentcad {
namespace {
constexpr std::size_t profile_limit=1024*1024,package_limit=128*1024*1024;
const std::array<const char*,3> roles={"machine","process","filament"};
Json object(Json properties,Json required){return {{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};}
void digest(const Json& value) {
  if(!value.is_string()||value.get_ref<const std::string&>().size()!=64||value.get_ref<const std::string&>().find_first_not_of("0123456789abcdef")!=std::string::npos)
    throw Error("invalid_argument","Expected SHA-256 must be 64 lowercase hex digits");
}
void artifact(const Json& value){fields(value,{"path","expected_sha256"});text_field(value,"path");digest(value.at("expected_sha256"));}
void options_valid(const Json& value) {
  fields(value,{"backend","version","executable","profiles","bed_type","review"});
  if(value.at("backend")!="orcaslicer"||value.at("version")!="2.4.2")throw Error("invalid_argument","Only the verified OrcaSlicer 2.4.2 CLI contract is supported");
  artifact(value.at("executable"));fields(value.at("profiles"),{"machine","process","filament"});
  for(const auto* role:roles)artifact(value.at("profiles").at(role));
  if(value.at("bed_type")!="High Temp Plate")throw Error("invalid_argument","This driver currently supports explicit High Temp Plate selection");
  validate_gcode_options(value.at("review"));
}
fs::path regular(const std::string& text) {
  const auto path=path_from_utf8(text);
  if(!path.is_absolute()||!fs::is_regular_file(path)||fs::is_symlink(fs::symlink_status(path)))throw Error("invalid_argument","Slicer artifacts require absolute regular non-symlink paths");
  return path;
}
void native_executable(const fs::path& path) {
  std::ifstream file(path,std::ios::binary);std::array<unsigned char,4> magic{};file.read(reinterpret_cast<char*>(magic.data()),magic.size());
  const bool elf=magic==std::array<unsigned char,4>{0x7f,'E','L','F'};
  const bool pe=magic[0]=='M'&&magic[1]=='Z';
  const bool mach=magic==std::array<unsigned char,4>{0xcf,0xfa,0xed,0xfe}||magic==std::array<unsigned char,4>{0xce,0xfa,0xed,0xfe}||
    magic==std::array<unsigned char,4>{0xfe,0xed,0xfa,0xcf}||magic==std::array<unsigned char,4>{0xfe,0xed,0xfa,0xce}||
    magic==std::array<unsigned char,4>{0xca,0xfe,0xba,0xbe}||magic==std::array<unsigned char,4>{0xbe,0xba,0xfe,0xca};
  if(!elf&&!pe&&!mach)throw Error("invalid_argument","Slicer executable must be a native binary, not a script or interpreter wrapper");
}
Json source_of(const Json& record,const std::string& feature,const std::string& build) {
  return {{"document_id",record.at("document_id")},{"revision",record.at("revision")},{"feature_id",feature},
    {"model_sha256",sha256(record.at("model").dump())},{"kernel_version",record.at("kernel_version")},{"native_build",build}};
}
std::string feature_of(const Json& record,const std::string& feature) {
  for(const auto& f:record.at("model").at("features"))if(f.at("id")==feature) {
    if(f.at("type")=="assembly")throw Error("invalid_argument","Slice one source solid at a time; use manufacturing packages for assembly inventory");
    return feature;
  }
  throw Error("invalid_argument","Missing slice feature");
}
Json ledger(const fs::path& root,const std::vector<std::string>& names) {
  Json entries=Json::array();std::uintmax_t total=0;
  for(const auto& name:names) {
    const auto path=root/path_from_utf8(name);const auto size=fs::file_size(path);total+=size;
    if(total>package_limit)throw Error("limit_exceeded","Slice package exceeds 128 MiB");
    entries.push_back({{"path",name},{"bytes",size},{"sha256",sha256_file(path,package_limit,check_job_cancelled)}});
  }
  return entries;
}
std::vector<std::string> argv(const fs::path& root) {
  if(path_to_utf8(root).find(';')!=std::string::npos)throw Error("invalid_argument","Orca settings separators require a workspace path without semicolons");
  return {"--debug","2","--logfile",path_to_utf8(root/"slicer.log"),"--datadir",path_to_utf8(root/"datadir"),
    "--curr-bed-type=High Temp Plate","--arrange","1","--orient","0","--export-settings",path_to_utf8(root/"effective-settings.json"),"--mstpp","300",
    "--load-settings",path_to_utf8(root/"profiles/machine.json")+";"+path_to_utf8(root/"profiles/process.json"),
    "--load-filaments",path_to_utf8(root/"profiles/filament.json"),"--outputdir",path_to_utf8(root/"output"),"--slice","0",path_to_utf8(root/"input.stl")};
}
Json execution_for(const Json& options) {
  return {{"backend","orcaslicer"},{"version","2.4.2"},{"executable",options.at("executable")},
    {"argv_template",argv(fs::path("/SLICE_PACKAGE"))},{"placement","arrange_xy_on_selected_bed_preserve_source_Z_orientation"},
    {"post_processing","prohibited"},{"hardware_contact",false}};
}
void geometry_matches(const Json& planned,const Json& fresh) {
  const auto close=[](const Json& a,const Json& b){if(!a.is_number()||!b.is_number())return false;const auto x=a.get<double>(),y=b.get<double>();return std::isfinite(x)&&std::isfinite(y)&&std::abs(x-y)<=1e-6+1e-9*std::max(std::abs(x),std::abs(y));};
  for(const auto* field:{"volume_mm3","area_mm2"})if(!close(planned.at(field),fresh.at(field)))throw Error("artifact_mismatch","Native source geometry differs from the reviewed plan");
  for(const auto* field:{"solid_count","face_count","edge_count"})if(planned.at(field)!=fresh.at(field))throw Error("artifact_mismatch","Native source topology counts differ from reviewed plan");
  for(int i=0;i<3;++i) {
    if(!close(planned.at("center_of_mass_mm").at(i),fresh.at("center_of_mass_mm").at(i)))throw Error("artifact_mismatch","Native center of mass differs from reviewed plan");
    for(const auto* side:{"min","max"})if(!close(planned.at("bounds_mm").at(side).at(i),fresh.at("bounds_mm").at(side).at(i)))throw Error("artifact_mismatch","Native bounds differ from reviewed plan");
  }
}
void profiles_valid(const fs::path& root) {
  Json profiles=Json::object();
  for(const auto* role:roles) {
    const auto value=parse_json(read_text(root/"profiles"/(std::string(role)+".json"),profile_limit));
    if(!value.is_object()||value.value("type",Json())!=role||!value.contains("name")||!value.at("name").is_string()||value.at("name").get_ref<const std::string&>().empty())throw Error("invalid_argument","Native profile must name its matching role");
    if(value.contains("inherits")&&!value.at("inherits").get<std::string>().empty())throw Error("invalid_argument","Supply self-contained resolved native profiles; implicit inheritance is unsupported");
    if(value.contains("post_process")) {
      const auto& scripts=value.at("post_process");
      if(!scripts.is_array())throw Error("invalid_argument","Native post-processing must be an empty string list");
      for(const auto& script:scripts)if(!script.is_string()||!script.get_ref<const std::string&>().empty())throw Error("invalid_argument","External profile post-processing is prohibited");
    }
    profiles[role]=value;
  }
  const auto& machine=profiles.at("machine");
  if(machine.value("gcode_flavor",Json())!="marlin"||machine.value("printer_technology",Json())!="FFF")throw Error("invalid_argument","Driver requires explicit Marlin FFF profile semantics");
  for(const auto* role:{"process","filament"}) {
    const auto& value=profiles.at(role);const auto compatible=value.value("compatible_printers",Json::array());
    if(!compatible.is_array()||std::find(compatible.begin(),compatible.end(),machine.at("name"))==compatible.end())
      throw Error("invalid_argument","Native profile requires an explicit compatible-printer list containing this machine; conditions are not executed");
  }
}
Json effective_identity(const fs::path& root) {
  const auto effective=parse_json(read_text(regular(path_to_utf8(root/"effective-settings.json")),4*profile_limit),4*profile_limit);
  Json names=Json::object();
  for(const auto* role:roles)names[role]=parse_json(read_text(root/"profiles"/(std::string(role)+".json"),profile_limit)).at("name");
  if(!effective.is_object()||effective.value("printer_settings_id",Json())!=names.at("machine")||
     effective.value("print_settings_id",Json())!=names.at("process")||
     effective.value("filament_settings_id",Json())!=Json::array({names.at("filament")})||
     effective.value("curr_bed_type",Json())!="High Temp Plate"||effective.value("gcode_flavor",Json())!="marlin"||
     effective.value("printer_technology",Json())!="FFF")
    throw Error("artifact_mismatch","Actual slicer settings do not identify the reviewed profiles, bed and firmware");
  if(effective.contains("post_process")) {
    const auto& scripts=effective.at("post_process");
    if(!scripts.is_array())throw Error("artifact_mismatch","Actual settings have malformed post-processing");
    for(const auto& script:scripts)if(!script.is_string()||!script.get_ref<const std::string&>().empty())throw Error("artifact_mismatch","Actual settings enabled prohibited post-processing");
  }
  names["bed_type"]="High Temp Plate";names["firmware"]="marlin";return names;
}
std::string token(){std::random_device random;return "slice_"+sha256(Json::array({random(),random(),random(),random()}).dump()).substr(0,32);}
struct Stage {
  fs::path path;Stage(const fs::path& parent){directory(parent);path=temporary_directory(parent);}~Stage(){std::error_code ignored;fs::remove_all(path,ignored);}
};
Json publish(const fs::path& workspace,const Json& source,const fs::path& staged,const std::string& suffix) {
  const auto id=text_field(source,"document_id");const auto dest=workspace/"exports"/(id+"-r"+std::to_string(source.at("revision").get<std::uint64_t>())+"-"+suffix+"-"+token());
  DocumentLock lock(workspace,id,LockWait::publication);check_job_cancelled();fs::rename(staged,dest);
  auto result=source;result["directory"]=path_to_utf8(dest);return result;
}
}
Json slicer_definitions() {
  Json definitions;
  const Json hash={{"type","string"},{"pattern","^[a-f0-9]{64}$"}},text={{"type","string"}};
  const auto file=object({{"path",text},{"expected_sha256",hash}},{"path","expected_sha256"});
  definitions["slice_options"]=object({{"backend",{{"const","orcaslicer"}}},{"version",{{"const","2.4.2"}}},{"executable",file},
    {"profiles",object({{"machine",file},{"process",file},{"filament",file}},{"machine","process","filament"})},
    {"bed_type",{{"const","High Temp Plate"}}},{"review",{{"$ref","#/$defs/gcode_options"}}}},
    {"backend","version","executable","profiles","bed_type","review"});
  return definitions;
}
void validate_slice_arguments(const Json& args) {
  const auto action=text_field(args,"action");
  if(action=="plan") {fields(args,{"document_id","revision","action","options"},{"feature_id"});options_valid(args.at("options"));}
  else if(action=="run") {fields(args,{"document_id","revision","action","plan_path","expected_sha256"});text_field(args,"plan_path");digest(args.at("expected_sha256"));}
  else throw Error("invalid_argument","Slice action must be plan or run");
}
Json plan_slice(const fs::path& workspace,const Json& record,const Json& options,const std::string& feature,const std::string& build) {
  options_valid(options);feature_of(record,feature);check_job_cancelled();
  Stage stage(workspace/"exports");directory(stage.path/"profiles");argv(stage.path);
  const auto exe=regular(text_field(options.at("executable"),"path"));native_executable(exe);
  if(sha256_file(exe,512*1024*1024,check_job_cancelled)!=text_field(options.at("executable"),"expected_sha256"))throw Error("artifact_mismatch","Installed slicer hash differs from the requested executable");
  for(const auto* role:roles) {
    const auto& input=options.at("profiles").at(role);const auto path=regular(text_field(input,"path"));
    const auto raw=read_text(path,profile_limit);if(sha256(raw)!=text_field(input,"expected_sha256"))throw Error("artifact_mismatch","Native profile bytes differ from expected SHA-256",{{"role",role}});
    atomic_text(stage.path/"profiles"/(std::string(role)+".json"),raw,profile_limit);
  }
  profiles_valid(stage.path);
  const auto source=source_of(record,feature,build);
  const auto evaluation=evaluate_model(workspace,record.at("model"),{{"kind","export"},{"feature_id",feature},{"format","stl"},{"path",path_to_utf8(stage.path/"input.stl")}});
  if(evaluation.at("summary").at("solid_count")!=1)throw Error("invalid_argument","Slice a single valid solid source; compound/assembly plates are unsupported");
  atomic_text(stage.path/"source.json",record.dump()+"\n");
  const auto artifacts=ledger(stage.path,{"source.json","input.stl","profiles/machine.json","profiles/process.json","profiles/filament.json"});
  const auto execution=execution_for(options);
  const auto plan=Json{{"schema_version",1},{"source",source},{"options",options},{"summary",evaluation.at("summary")},{"artifacts",artifacts},{"execution",execution}}.dump(2)+"\n";
  atomic_text(stage.path/"plan.json",plan);auto result=publish(workspace,source,stage.path,"slice-plan");
  result["action"]="plan";result["path"]=path_to_utf8(path_from_utf8(text_field(result,"directory"))/"plan.json");result["sha256"]=sha256(plan);
  result["execution"]=execution;result["physical_print_started"]=false;return result;
}
Json run_slice(const fs::path& workspace,const Json& record,const Json& args,const std::string& build) {
  const auto plan_path=regular(text_field(args,"plan_path"));const auto content=read_text(plan_path);
  if(sha256(content)!=text_field(args,"expected_sha256"))throw Error("artifact_mismatch","Reviewed slice plan hash changed");
  const auto plan=parse_json(content);fields(plan,{"schema_version","source","options","summary","artifacts","execution"});
  if(plan.at("schema_version")!=1)throw Error("invalid_argument","Unsupported slice plan version");
  const auto feature=feature_of(record,text_field(plan.at("source"),"feature_id"));const auto source=source_of(record,feature,build);
  if(plan.at("source")!=source)throw Error("artifact_mismatch","Slice plan does not match the committed revision and current native build");
  const auto& options=plan.at("options");options_valid(options);
  if(plan.at("execution")!=execution_for(options))throw Error("artifact_mismatch","Reviewed plan execution does not match the fixed native driver");
  Stage stage(workspace/"exports");directory(stage.path/"profiles");directory(stage.path/"output");directory(stage.path/"datadir");
  std::set<std::string> required={"source.json","input.stl","profiles/machine.json","profiles/process.json","profiles/filament.json"};
  if(!plan.at("artifacts").is_array()||plan.at("artifacts").size()!=required.size())throw Error("invalid_argument","Slice plan artifact ledger is incomplete");
  for(const auto& item:plan.at("artifacts")) {
    fields(item,{"path","bytes","sha256"});const auto name=text_field(item,"path");digest(item.at("sha256"));
    if(!required.erase(name)||!item.at("bytes").is_number_unsigned()||item.at("bytes")>package_limit)throw Error("invalid_argument","Invalid slice plan artifact identity");
    const auto file=regular(path_to_utf8(plan_path.parent_path()/path_from_utf8(name)));const auto raw=read_text(file,name=="input.stl"?gcode_bytes_limit:profile_limit);
    if(raw.size()!=item.at("bytes")||sha256(raw)!=text_field(item,"sha256"))throw Error("artifact_mismatch","Reviewed slice plan input changed",{{"path",name}});
    atomic_text(stage.path/path_from_utf8(name),raw,gcode_bytes_limit);
  }
  if(parse_json(read_text(stage.path/"source.json"))!=record)throw Error("artifact_mismatch","Plan's editable source differs from saved revision");
  for(const auto* role:roles)if(sha256_file(stage.path/"profiles"/(std::string(role)+".json"),profile_limit,check_job_cancelled)!=text_field(options.at("profiles").at(role),"expected_sha256"))throw Error("artifact_mismatch","Plan profile ledger contradicts its declared identity");
  profiles_valid(stage.path);const auto original=sha256_file(stage.path/"input.stl",gcode_bytes_limit,check_job_cancelled);
  directory(stage.path/"reviewed-plan");directory(stage.path/"reviewed-plan/profiles");
  for(const auto& item:plan.at("artifacts")) {
    const auto relative=path_from_utf8(text_field(item,"path"));fs::copy_file(stage.path/relative,stage.path/"reviewed-plan"/relative);
  }
  atomic_text(stage.path/"reviewed-plan/plan.json",content);
  const auto fresh=evaluate_model(workspace,record.at("model"),{{"kind","export"},{"feature_id",feature},{"format","stl"},{"path",path_to_utf8(stage.path/"input.stl")}});
  geometry_matches(plan.at("summary"),fresh.at("summary"));
  const auto input_hash=sha256_file(stage.path/"input.stl",gcode_bytes_limit,check_job_cancelled);
  const auto exe=regular(text_field(options.at("executable"),"path"));native_executable(exe);
  auto request=Json{{"executable",path_to_utf8(exe)},{"expected_sha256",options.at("executable").at("expected_sha256")},
    {"cwd",path_to_utf8(stage.path)},{"arguments",{"--help"}},{"log_prefix","probe"}};
  const auto probe=run_native_process(workspace,request);
  const auto help=read_text(stage.path/"probe.stdout.log",2*1024*1024)+read_text(stage.path/"probe.stderr.log",2*1024*1024);
  if(probe.at("exit_status")!=0||!help.starts_with("OrcaSlicer-2.4.2:"))throw Error("slicer_version_mismatch","Installed executable did not report the verified OrcaSlicer CLI version");
  request["arguments"]=argv(stage.path);request["log_prefix"]="slice";const auto process=run_native_process(workspace,request);
  if(process.at("exit_status")!=0) {
    const auto stderr_text=read_text(stage.path/"slice.stderr.log",2*1024*1024);
    throw Error("slicer_failed","OrcaSlicer rejected the job",{{"exit_status",process.at("exit_status")},{"stderr_tail",stderr_text.substr(stderr_text.size()>4096?stderr_text.size()-4096:0)}});
  }
  if(sha256_file(stage.path/"input.stl",gcode_bytes_limit,check_job_cancelled)!=input_hash)throw Error("artifact_mismatch","Native slicer input mesh changed during execution");
  for(const auto* role:roles)if(sha256_file(stage.path/"profiles"/(std::string(role)+".json"),profile_limit,check_job_cancelled)!=text_field(options.at("profiles").at(role),"expected_sha256"))throw Error("artifact_mismatch","Captured profile changed during execution");
  const auto gcode=regular(path_to_utf8(stage.path/"output/plate_1.gcode"));const auto raw=read_text(gcode,gcode_bytes_limit);
  const auto report=evaluate_model(workspace,record.at("model"),{{"kind","gcode_review"},{"path",path_to_utf8(gcode)},{"options",options.at("review")}}).at("gcode_report");
  // Require actual effective settings as provenance, never just caller intent.
  const auto effective=effective_identity(stage.path);
  atomic_text(stage.path/"review.json",Json{{"schema_version",1},{"source",source},{"options",options.at("review")},{"report",report}}.dump(2)+"\n");
  atomic_text(stage.path/"execution.json",Json{{"source",source},{"plan_sha256",sha256(content)},{"version_probe",probe},{"slice",process},
    {"reviewed_plan","reviewed-plan/plan.json"},{"planned_mesh_sha256",original},{"executed_mesh_sha256",input_hash},
    {"mesh_source","regenerated_from_same_committed_exact_source_at_run"},{"geometry_check",{{"absolute_tolerance",1e-6},{"relative_tolerance",1e-9},{"summary",fresh.at("summary")}}},
    {"effective_profile_identity",effective},{"argv_paths","private_execution_stage"},{"executable_identity","same_measured_SHA256_before_and_after_each_invocation"},{"physical_print_started",false}}.dump(2)+"\n");
  fs::remove_all(stage.path/"datadir");
  std::vector<std::string> names;std::size_t gcode_files=0;
  for(const auto& entry:fs::recursive_directory_iterator(stage.path)) {
    if(entry.is_symlink()||(!entry.is_regular_file()&&!entry.is_directory())||names.size()>4096)throw Error("limit_exceeded","Invalid slicer package entry");
    if(entry.is_regular_file()) {
      names.push_back(path_to_utf8(entry.path().lexically_relative(stage.path)));
      if(entry.path().extension()==".gcode")++gcode_files;
    }
  }
  if(gcode_files!=1)throw Error("slicer_failed","Single-solid slicing must produce exactly one plain G-code artifact");
  std::sort(names.begin(),names.end());const auto artifacts=ledger(stage.path,names);
  atomic_text(stage.path/"manifest.json",Json{{"schema_version",1},{"source",source},{"plan_sha256",sha256(content)},{"artifacts",artifacts},
    {"process_review",report.at("status")},{"physical_print_started",false},{"printer_approval","not_evaluated"}}.dump(2)+"\n");
  auto result=publish(workspace,source,stage.path,"sliced");
  const auto destination=path_from_utf8(text_field(result,"directory"));result["action"]="run";result["path"]=path_to_utf8(destination/"manifest.json");
  result["gcode_path"]=path_to_utf8(destination/"output/plate_1.gcode");result["gcode_sha256"]=sha256(raw);result["gcode_bytes"]=raw.size();
  result["plan_sha256"]=sha256(content);result["report"]=report;result["physical_print_started"]=false;return result;
}
}
