#include "agentcad/gcode.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/service.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <set>
#include <thread>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
void fails(const std::string& code,const std::function<void()>& action) {
  try{action();throw std::runtime_error("Expected "+code);}catch(const Error& e){require(e.code==code,"Expected "+code+", got "+e.code);}
}
struct Temp {fs::path root;Temp(){root=temporary_directory(fs::temp_directory_path());}~Temp(){std::error_code ignored;fs::remove_all(root,ignored);}};
Json options() {
  return {{"firmware","marlin"},{"machine",{{"name","Analytic fixture"},{"motion_bounds_mm",{{-20,20},{-20,20},{0,100}}},{"home_position_mm",{0,0,0}}}},
    {"material",{{"name","Analytic PLA range"},{"nozzle_temperature_c",{190,230}},{"bed_temperature_c",{50,70}}}},
    {"initial",{{"units","mm"},{"xyz_mode","absolute"},{"extrusion_mode","absolute"},{"position_mm",{0,0,0}},{"extruder_mm",0}}}};
}
const std::string header="G21\nG90\nM83\nM104 S210\nM140 S60\n";
Json inspect(const std::string& s,Json profile=options()){return inspect_gcode(header+s,profile);}
Json check(const Json& result,const std::string& id){for(const auto& c:result.at("checks"))if(c.at("id")==id)return c;throw std::runtime_error("Missing "+id);}
double stat(const Json& result,const char* key){return result.at("statistics").at(key).get<double>();}
void analytic() {
  auto r=inspect("G1 X10 Y5 Z1 E1\n");require(r.at("status")=="pass","Finite explicit linear move passes supported static checks");
  require(stat(r,"extruded_mm")==1&&r.at("statistics").at("commanded_bounds_mm")[0][1]==10,"Independent endpoint and commanded E amount");
  r=inspect("G91\nG1X15E1\nG1X10E1\n");require(check(r,"commanded_bounds").at("status")=="fail","Relative moves accumulate past the envelope");
  require(r.at("statistics").at("commanded_bounds_mm")[0][1]==25,"Relative bounds include summed endpoints");
  r=inspect("G20\nG1 X1 E.1\n");require(check(r,"commanded_bounds").at("status")=="fail","Inch coordinate converted to mm exceeds fixture");
  require(std::abs(stat(r,"extruded_mm")-2.54)<1e-10,"Inch extrusion converted independently");
  auto o=options();o["initial"]["position_mm"]={5,0,0};r=inspect("G92 X0\nG1 X16 E1\n",o);
  require(check(r,"commanded_bounds").at("status")=="fail"&&r.at("statistics").at("commanded_bounds_mm")[0][1]==21,"G92 keeps physical commanded-frame offset");
  r=inspect("M82\nG1X1E5\nG92E0\nG1X2E1\n");require(stat(r,"extruded_mm")==6,"Absolute extrusion handles G92 resets");
  r=inspect("G1X1E2\nG90\nG1X2E1\n");require(stat(r,"extruded_mm")==2&&stat(r,"retracted_mm")==1,"Marlin G90 clears M83 override");
  r=inspect("G1X0E10\n");require(check(r,"extrusion").at("status")=="fail","E-only or zero-length spatial motion is not deposition");
  o=options();o["initial"]["position_mm"]={10,0,1};o["machine"]["motion_bounds_mm"][1]={-5,5};
  r=inspect("G3X-10Y0I-10J0E1\n",o);
  require(check(r,"commanded_bounds").at("status")=="fail","CCW semicircle exceeds Y envelope although endpoints fit");
  require(std::abs(r.at("statistics").at("commanded_bounds_mm")[1][1].get<double>()-10)<1e-9,"Exact quarter extremum reaches +radius");
  r=inspect("G2X-10Y0I-10J0E1\n",o);require(std::abs(r.at("statistics").at("commanded_bounds_mm")[1][0].get<double>()+10)<1e-9,"Clockwise semicircle reaches -radius");
  o=options();o["initial"]["position_mm"]={10,0,1};r=inspect("G2I-10E1\n",o);
  require(r.at("status")=="pass"&&stat(r,"arc_commands")==1,"Center-only full circle is inspected as a complete sweep");
  require(r.at("statistics").at("commanded_bounds_mm")[0][0]==-10&&r.at("statistics").at("commanded_bounds_mm")[1][1]==10,"Full circle includes all extrema");
  o["machine"]["motion_bounds_mm"][0]={-15,15};o["machine"]["motion_bounds_mm"][1]={-15,15};
  require(inspect("G3X0Y10R10E1\n",o).at("status")=="pass","Positive R selects the minor arc");
  require(check(inspect("G3X0Y10R-10E1\n",o),"commanded_bounds").at("status")=="fail","Negative R selects major arc reaching 20 mm");
  o=options();o["initial"]["position_mm"]={0,0,10};o["machine"]["motion_bounds_mm"]={{-5,5},{-20,20},{-20,20}};
  r=inspect("G18\nG3X0Z-10K-10E1\n",o);require(check(r,"commanded_bounds").at("status")=="fail","XZ plane sweep includes X extrema");
  o=options();o["initial"]["position_mm"]={0,10,0};o["machine"]["motion_bounds_mm"][2]={-5,5};
  r=inspect("G19\nG3Y-10Z0J-10E1\n",o);require(check(r,"commanded_bounds").at("status")=="fail","YZ plane sweep includes Z extrema");
  o=options();o["initial"]["position_mm"]={10,0,1};r=inspect("G3X-10Y0Z3I-10E1\n",o);
  require(r.at("status")=="pass"&&r.at("statistics").at("commanded_bounds_mm")[2][1]==3,"Helical orthogonal travel includes endpoint extrema");
  for(const auto* arc:{"G2X5Y0E1\n","G2X5Y0R1E1\n","G2X5Y0I-1R10E1\n","G2X5Y0I-1E1\n"})
    require(check(inspect(arc),"syntax").at("status")=="fail","Malformed or geometrically inconsistent arc fails explicitly");
  r=inspect("G2I10P2E1\n");require(check(r,"firmware_commands").at("status")=="unknown","Unsupported extra arc turns remain unknown");
  r=inspect("G90.1\nG1X1E1\n");require(r.at("status")=="unknown","Unsupported absolute-center extension is not assumed Marlin");
  o=options();o["initial"].erase("position_mm");r=inspect("G91\nG1X1E1\n",o);require(check(r,"commanded_bounds").at("status")=="unknown","Unknown relative start never passes swept bounds");
  r=inspect("G2I10E1\n",o);require(check(r,"extrusion").at("status")=="unknown"&&stat(r,"deposition_commands")==0,"Unknown arc start cannot imply measured deposition");
  r=inspect("G28 W\nG1X1E1\n");require(check(r,"commanded_bounds").at("status")=="pass"&&r.at("status")=="unknown","Caller home permits path inspection while physical homing is unknown");
  o=options();o["machine"].erase("home_position_mm");r=inspect("G28\nG91\nG1X1E1\n",o);require(check(r,"position_tracking").at("status")=="unknown","Homing without declared location cannot imply zero");
  o=options();o["initial"].erase("extruder_mm");r=inspect("M82\nG1X1E1\nG1X2E2\n",o);
  require(check(r,"extrusion").at("status")=="unknown"&&stat(r,"extruded_mm")==1,"Unknown initial absolute E is not counted as deposited length");
  r=inspect("M200D1.75\nG1X1E100\n");require(check(r,"extrusion").at("status")=="unknown"&&stat(r,"extruded_mm")==0,"Volumetric units cannot masquerade as filament length");
  r=inspect("M149F\nM104S210\nG1X1E1\n");require(check(r,"temperature_targets").at("status")=="unknown","Temperature unit changes invalidate Celsius assumptions");
  require(r.at("statistics").at("temperature_targets").back().at("units")=="unknown"&&!r.at("statistics").at("temperature_targets").back().contains("target_c"),"Unsupported temperature units never label a programmed value Celsius");
  r=inspect("M104S250\nG1X1E1\n");require(check(r,"temperature_targets").at("status")=="fail","Nozzle target exceeds material range");
  r=inspect("M140S-1\nG1X1E1\n");require(check(r,"temperature_targets").at("status")=="fail","Negative heater target fails");
  require(inspect("G1X1E1\nM104S0\nM140S0\n").at("status")=="pass","Power-off commands do not violate positive target range");
  require(check(inspect_gcode("G1X1E1\nM104S0\nM140S0\n",options()),"temperature_targets").at("status")=="fail","Power-off alone is not a heating profile");
  r=inspect("M117 Hello world X999\nG1X1E1\n");require(r.at("status")=="unknown"&&check(r,"commanded_bounds").at("status")=="pass","Unknown message text does not inject motion words");
  r=inspect("G1X1A2E1\n");require(check(r,"firmware_commands").at("status")=="unknown","Additional axes are explicit unsupported coverage");
  for(const auto* bad:{"G1XnanE1\n","G1X1X2E1\n","G1X1E1F0\n","G1X1E1 (unclosed\n"})
    require(check(inspect(bad),"syntax").at("status")=="fail","Malformed numeric motion or comments cannot silently pass");
  std::string numbered="N42 G1 X1 E1";unsigned int sum=0;for(const unsigned char c:numbered)sum^=c;
  r=inspect(numbered+"*"+std::to_string(sum)+"\n");require(r.at("status")=="pass"&&stat(r,"checksum_lines")==1,"Independent XOR checksum is accepted");
  require(check(inspect(numbered+"*0\n"),"syntax").at("status")=="fail","Checksum mismatch is a failed finding");
  r=inspect("g1x1e1 ; café 日本語\n");require(r.at("status")=="pass","Compact case-insensitive words and UTF-8 comments are supported");
  r=inspect(std::string(100,'\n'));require(r.at("status")=="fail","Comment-only or blank artifact fails required movement/extrusion");
  fails("limit_exceeded",[&]{inspect(std::string(4097,'a'));});
  fails("invalid_argument",[&]{inspect(std::string("G1X1\0E1",8));});
  int calls=0;fails("job_cancelled",[&]{inspect_gcode(header+"G1X1E1\n",options(),[&]{if(++calls==3)throw Error("job_cancelled","Cancelled");});});
  o=options();o["machine"]["motion_bounds_mm"][0]={2,1};fails("invalid_argument",[&]{validate_gcode_options(o);});
  o=options();o["initial"]["script"]="bad";fails("invalid_argument",[&]{validate_gcode_options(o);});
  o=options();o["firmware"]="guess";fails("invalid_argument",[&]{validate_gcode_options(o);});
}
Json job(Service& service,const Json& args) {
  for(int i=0;i<1000;++i){try{return service.call("cad_job",args);}catch(const Error& e){if(e.code!="workspace_busy")throw;}std::this_thread::sleep_for(std::chrono::milliseconds(2));}
  throw std::runtime_error("Job admission remained busy");
}
Json terminal(Service& service,const std::string& id) {
  for(int i=0;i<4000;++i){auto r=job(service,{{"action","get"},{"job_id",id}});if(r.at("state")!="queued"&&r.at("state")!="running"&&r.at("state")!="cancelling")return r;std::this_thread::sleep_for(std::chrono::milliseconds(3));}
  throw std::runtime_error("G-code job remained live");
}
std::set<std::string> exports(const fs::path& workspace) {
  std::set<std::string> r;for(const auto& f:fs::directory_iterator(workspace/"exports"))r.insert(path_to_utf8(f.path().filename()));return r;
}
void service() {
  Temp t;Service service(t.root/"workspace");
  const Json model={{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",Json::array({{{"id","part"},{"type","box"},{"size",{10,10,2}}}})},{"output","part"}};
  service.call("cad_create",{{"document_id","part"},{"model",model}});
  const auto before=service.call("cad_read",{{"document_id","part"}});const auto file=t.root/"real file.gcode";
  const auto raw=header+"G1X1Y2Z.2E1\nM117 operator message\n";atomic_text(file,raw);
  Json args={{"document_id","part"},{"revision",1},{"path",path_to_utf8(file)},{"expected_sha256",sha256(raw)},{"options",options()}};
  const auto result=service.call("cad_gcode_review",args);
  require(result.at("report").at("status")=="unknown","Successful native review preserves unsupported findings");
  require(read_text(path_from_utf8(text_field(result,"artifact_path")))==raw,"Preserve exact original G-code bytes");
  const auto content=read_text(path_from_utf8(text_field(result,"path")));
  require(content.size()==result.at("report_bytes")&&sha256(content)==result.at("report_sha256").get<std::string>(),"Native raw report size and checksum match");
  const auto folder=path_from_utf8(text_field(result,"path")).parent_path();const auto manifest=parse_json(read_text(folder/"manifest.json"));
  for(const auto& a:manifest.at("artifacts")) {const auto b=read_text(folder/path_from_utf8(text_field(a,"path")));require(sha256(b)==a.at("sha256").get<std::string>()&&b.size()==a.at("bytes"),"Every package artifact independently verifies");}
  require(manifest.at("physical_print_started")==false&&parse_json(content).at("source_association")=="caller_declared_not_geometry_verified","No print or geometric slicing provenance is inferred");
  require(service.call("cad_read",{{"document_id","part"}})==before,"Static review preserves source HEAD and editable intent");
  const auto existing=exports(t.root/"workspace");auto bad=args;bad["expected_sha256"]=std::string(64,'a');
  fails("artifact_mismatch",[&]{service.call("cad_gcode_review",bad);});require(exports(t.root/"workspace")==existing,"Checksum failure publishes no artifact directory");
  bad=args;bad["feature_id"]="missing";fails("invalid_argument",[&]{service.call("cad_gcode_review",bad);});
  bad=args;bad["path"]="relative.gcode";fails("invalid_argument",[&]{service.call("cad_gcode_review",bad);});
  bad=args;bad["shell"]="echo bad";fails("invalid_argument",[&]{service.call("cad_gcode_review",bad);});
  const Json request={{"action","submit"},{"request_id","review_job"},{"tool","cad_gcode_review"},{"arguments",args}};
  job(service,request);auto done=terminal(service,"review_job");require(done.at("state")=="succeeded","Static review is admitted and runs asynchronously");
  fs::remove(file);require(job(service,request).at("result")==done.at("result"),"Durable replay returns original result after source removal");
  Temp relocated;fs::copy(folder,relocated.root/"package",fs::copy_options::recursive);
  require(read_text(relocated.root/"package/original.gcode")==raw,"Review package relocates independently of caller paths");
  const auto large=t.root/"large.gcode";std::string repeated=header;for(int i=0;i<300000;++i)repeated+="G1X1Y1Z1E1\n";atomic_text(large,repeated,gcode_bytes_limit);
  auto slow=args;slow["path"]=path_to_utf8(large);slow["expected_sha256"]=sha256(repeated);
  const auto before_cancel=exports(t.root/"workspace");
  job(service,{{"action","submit"},{"request_id","cancel_review"},{"tool","cad_gcode_review"},{"arguments",slow}});
  bool started=false;for(int i=0;i<3000&&!started;++i) {
    for(const auto& f:fs::directory_iterator(t.root/"workspace/.workers"))if(fs::is_directory(f.path())&&fs::exists(f.path()/"building.json"))started=true;
    if(!started)std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  require(started,"Cancellation fixture observes an actual native inspection worker");
  job(service,{{"action","cancel"},{"job_id","cancel_review"}});
  require(terminal(service,"cancel_review").at("state")=="cancelled","Cancellation stops the running native inspection");
  require(exports(t.root/"workspace")==before_cancel&&service.call("cad_read",{{"document_id","part"}})==before,"Cancelled inspection preserves earlier artifacts and HEAD");
  job(service,{{"action","submit"},{"request_id","deadline_review"},{"tool","cad_gcode_review"},{"arguments",slow},{"budget",{{"timeout_ms",1}}}});
  done=terminal(service,"deadline_review");require(done.at("state")=="failed"&&done.at("error").at("code")=="job_timeout","Native review obeys job wall budget");
}
}
int main(){try{set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));analytic();service();std::cout<<checks<<" G-code checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
