#include "agentcad/fabrication.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/hash.hpp"
#include <BRepPrimAPI_MakeCone.hxx>
#include <STEPControl_Writer.hxx>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <set>
#include <thread>

using namespace agentcad;
namespace {
int checks=0;
void require(bool yes,const std::string& message){++checks;if(!yes)throw std::runtime_error(message);}
void near(double a,double b,double tolerance=1e-5){require(std::abs(a-b)<tolerance,"Measured "+std::to_string(a)+" differs from analytic "+std::to_string(b));}
struct Temp{fs::path root;Temp(){root=temporary_file(fs::temp_directory_path());fs::remove(root);directory(root);}~Temp(){std::error_code ignored;fs::remove_all(root,ignored);}};
void fails(const std::string& code,const std::function<void()>& action){try{action();require(false,"Expected "+code);}catch(const Error& e){require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what());}}
Json source(Json features,const std::string& output){return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",features},{"output",output}};}
Json box(){return source(Json::array({{{"id","plate"},{"type","box"},{"size",{20,10,2}}}}),"plate");}
Json profile(const std::string& process){return {{"process",process},{"orientation",{{"build_direction",{0,0,1}},{"x_direction",{1,0,0}}}}};}
Json options(Json p){return {{"profile",p}};}
const Json& check(const Json& report,const std::string& id,bool assembly=false){
  const auto& list=assembly?report.at("checks"):report.at("parts")[0].at("checks");
  for(const auto& c:list)if(c.at("id")==id)return c;throw std::runtime_error("Missing check "+id);
}
Json review(const Json& model,Json p){return BuiltModel(model).fabrication_review(options(p));}
std::set<std::string> exports(const fs::path& root){std::set<std::string> result;for(const auto& p:fs::directory_iterator(root/"exports"))result.insert(path_to_utf8(p.path().filename()));return result;}
Json job(Service& service,const Json& args){for(int i=0;i<2000;++i){try{return service.call("cad_job",args);}catch(const Error& e){if(e.code!="workspace_busy")throw;}std::this_thread::sleep_for(std::chrono::milliseconds(2));}throw std::runtime_error("Job metadata stayed busy");}
Json terminal(Service& service,const std::string& id){for(int i=0;i<10000;++i){auto result=job(service,{{"action","get"},{"job_id",id}});const auto state=result.at("state");if(state!="queued"&&state!="running"&&state!="cancelling")return result;std::this_thread::sleep_for(std::chrono::milliseconds(2));}throw std::runtime_error("Review job did not finish");}
void fdm(){
  auto p=profile("fdm");p["build_envelope_mm"]={20,10,2};p["minimum_wall_mm"]=3;p["overhang_angle_deg"]=45;
  const auto result=review(box(),p);
  require(result.at("status")=="fail","Thin plate reports a finding rather than production approval");
  require(check(result,"build_envelope").at("status")=="pass","Exact oriented box fits within numerical tolerance");
  require(check(result,"sampled_wall_thickness").at("status")=="fail","Supplied 3 mm minimum detects the actual 2 mm plate");
  near(check(result,"sampled_wall_thickness").at("evidence").at("minimum_sampled_chord_mm"),2);
  const auto mesh=check(result,"mesh_topology");require(mesh.at("status")=="pass","Independent planar mesh has closed consistent winding");
  near(mesh.at("evidence").at("signed_volume_mm3"),400);
  require(check(result,"mesh_self_intersections").at("status")=="unknown"&&check(result,"global_minimum_wall").at("status")=="unknown","Unsupported global checks never become passes");
  auto coincident=box();coincident["features"].push_back({{"id","duplicate"},{"type","pattern"},{"input","plate"},{"count",2},{"step",{20,0,0}}});coincident["output"]="duplicate";
  const auto bad_mesh=review(coincident,p);
  require(check(bad_mesh,"mesh_topology").at("status")=="fail"&&check(bad_mesh,"mesh_topology").at("evidence").at("nonmanifold_edges").get<int>()>0,"Touching source solids with coincident faces produce measured nonmanifold mesh edges rather than a sound-mesh pass");
  require(check(result,"fdm_overhang").at("status")=="pass","Plate bed-contact underside is explicitly excluded");
  near(check(result,"fdm_overhang").at("evidence").at("area_exceeding_limit_mm2"),0);
  p["orientation"]={{"build_direction",{1,0,0}},{"x_direction",{0,1,0}}};p["build_envelope_mm"]={10,2,19};
  const auto oriented=review(box(),p);
  require(check(oriented,"build_envelope").at("status")=="fail","Rotated 20 mm build height exceeds the supplied 19 mm machine height");
  const auto size=check(oriented,"build_envelope").at("evidence").at("size_mm");near(size[0],10);near(size[1],2);near(size[2],20);
  const auto overhang=source(Json::array({{{"id","stem"},{"type","box"},{"size",{4,4,6}}},
    {{"id","cap"},{"type","box"},{"size",{12,4,2}},{"origin",{-4,0,6}}},{{"id","tee"},{"type","fuse"},{"left","stem"},{"right","cap"}}}),"tee");
  p=profile("fdm");p["overhang_angle_deg"]=45;
  const auto bad=review(overhang,p);
  require(check(bad,"fdm_overhang").at("status")=="fail","Horizontal cap creates measured unsupported underside");
  near(check(bad,"fdm_overhang").at("evidence").at("area_exceeding_limit_mm2"),32);
  near(check(bad,"fdm_overhang").at("evidence").at("max_angle_from_vertical_deg"),90);
  p["orientation"]={{"build_direction",{0,1,0}},{"x_direction",{1,0,0}}};
  require(check(review(overhang,p),"fdm_overhang").at("status")=="pass","Putting the same T profile flat on its supplied build frame removes non-bed overhangs");
}
Json tube(){return source(Json::array({{{"id","outside"},{"type","cylinder"},{"radius",5},{"height",10}},
  {{"id","bore"},{"type","cylinder"},{"radius",4},{"height",12},{"origin",{0,0,-1}}},
  {{"id","tube"},{"type","cut"},{"left","outside"},{"right","bore"}}}),"tube");}
void cnc_and_sheet(){
  auto p=profile("cnc");p["tool_radius_mm"]=4.2;p["minimum_wall_mm"]=1.1;
  const auto result=review(tube(),p);
  require(check(result,"cnc_internal_cylinder_radius").at("status")=="fail","Exact 4 mm inner cylinder rejects a stated 4.2 mm cutter radius");
  near(check(result,"cnc_internal_cylinder_radius").at("evidence").at("cylinders")[0].at("radius_mm"),4);
  near(check(result,"sampled_wall_thickness").at("evidence").at("minimum_sampled_chord_mm"),1);
  require(check(result,"sampled_wall_thickness").at("status")=="fail","Exact radial rays detect the 1 mm tube wall");
  p["tool_radius_mm"]=3;p["minimum_wall_mm"]=.5;
  const auto fit=review(tube(),p);require(check(fit,"cnc_internal_cylinder_radius").at("status")=="pass","Measured axial cylinder accepts a smaller supplied cutter");
  require(check(fit,"cnc_toolpath_and_stock").at("status")=="unknown","Radius check does not invent cutter swept-volume or fixture evidence");
  const auto stacked=source(Json::array({{{"id","one"},{"type","box"},{"size",{5,5,2}}},
    {{"id","two"},{"type","pattern"},{"input","one"},{"count",2},{"step",{0,0,3}}}}),"two");
  require(check(review(stacked,profile("cnc")),"cnc_point_access").at("status")=="fail","Saved upper solid blocks upward tool rays from lower solid top");
  p=profile("sheet_laser");p["sheet_thickness_mm"]=2;p["sheet_thickness_tolerance_mm"]=0;
  const auto sheet=review(box(),p);require(check(sheet,"sheet_prismatic").at("status")=="pass"&&check(sheet,"sheet_stock_thickness").at("status")=="pass","Planar sheet matches explicit 2 mm stock without invented allowance");
  p["sheet_thickness_mm"]=3;require(check(review(box(),p),"sheet_stock_thickness").at("status")=="fail","Incorrect stock thickness is a measured finding");
  auto bevel=box();bevel["features"].push_back({{"id","bevel"},{"type","chamfer"},{"input","plate"},{"distance",.2},{"edges","all"}});bevel["output"]="bevel";
  require(check(review(bevel,p),"sheet_prismatic").at("status")=="fail","Tilted bevel walls are not a constant-height sheet profile");
  p["sheet_thickness_mm"]=10;const auto ring=review(tube(),p);
  require(check(ring,"sheet_prismatic").at("status")=="pass","Exact axial cylinders support through-holes in a constant-height sheet");
  near(check(ring,"sheet_prismatic").at("evidence").at("thickness_mm"),10);
  const auto omitted=review(box(),profile("sheet_laser"));require(check(omitted,"sheet_stock_thickness").at("status")=="unknown","Missing supplied stock never passes");
}
Json cone(double bottom,double top,Temp& temporary){
  const auto path=temporary.root/"independent.step";STEPControl_Writer writer;
  require(writer.Transfer(BRepPrimAPI_MakeCone(bottom,top,10).Shape(),STEPControl_AsIs)==IFSelect_RetDone&&writer.Write(path_to_utf8(path).c_str())==IFSelect_RetDone,"Independent analytic cone fixture exports STEP");
  const auto bytes=read_text(path,524288);return source(Json::array({{{"id","cone"},{"type","import_step"},{"content",bytes},{"sha256",sha256(bytes)}}}),"cone");
}
void molding(){
  Temp temporary;auto p=profile("molding");p["minimum_draft_deg"]=3;p["parting_plane_mm"]=0;
  const auto tapered=review(cone(5,4,temporary),p);
  require(check(tapered,"molding_sampled_draft").at("status")=="pass","Five-point-seven degree cone exceeds supplied three degree draft at exact sampled normals");
  near(check(tapered,"molding_sampled_draft").at("evidence").at("minimum_sampled_signed_draft_deg"),std::atan(.1)*180/std::acos(-1.0));
  require(check(tapered,"molding_sampled_undercuts").at("status")=="pass","Outward-tapered cone has no obstruction at its measured pull samples");
  require(check(tapered,"molding_global_release_and_flow").at("status")=="unknown","Sampled mold checks do not certify global release or polymer flow");
  const auto undercut=review(cone(5,6,temporary),p);
  require(check(undercut,"molding_sampled_draft").at("status")=="fail"&&check(undercut,"molding_sampled_undercuts").at("status")=="fail","Reverse taper produces negative signed draft and measured undercut evidence");
  require(check(tapered,"sampled_wall_thickness").at("status")=="unknown","No wall threshold means no pass even with measured chords");
}
Json nested(double separation=12){
  return source(Json::array({{{"id","block"},{"type","box"},{"size",{10,5,2}}},
    {{"id","module"},{"type","assembly"},{"parts",Json::array({{{"id","leaf"},{"input","block"}}})}},
    {{"id","assembly"},{"type","assembly"},{"parts",Json::array({{{"id","left"},{"input","module"}},
      {{"id","right"},{"input","module"},{"placement",{{"translation",{separation,0,0}}}}}})}}}),"assembly");
}
void clearance_and_history(){
  auto opt=options(profile("fdm"));opt["minimum_clearance_mm"]=1.5;
  const auto spaced=BuiltModel(nested()).fabrication_review(opt);
  require(spaced.at("parts").size()==1&&spaced.at("parts")[0].at("quantity")==2,"Repeated nested source is reviewed once with both occurrence identities");
  require(check(spaced,"assembly_clearance",true).at("status")=="pass","Exact nested occurrence gap meets supplied minimum");
  near(check(spaced,"assembly_clearance",true).at("evidence").at("minimum_distance_mm"),2);
  const auto pair=check(spaced,"assembly_interference",true).at("evidence").at("pairs")[0];
  require(pair.at("a")=="left/leaf"&&pair.at("b")=="right/leaf","Pair evidence retains complete nested occurrence paths");
  near(pair.at("intersection_volume_mm3"),0);
  const auto overlap=BuiltModel(nested(8)).fabrication_review(opt);
  require(check(overlap,"assembly_interference",true).at("status")=="fail"&&check(overlap,"assembly_clearance",true).at("status")=="fail","Exact common material detects overlapping nested occurrences");
  near(check(overlap,"assembly_interference",true).at("evidence").at("pairs")[0].at("intersection_volume_mm3"),20);
  near(check(BuiltModel(nested(5)).fabrication_review(opt),"assembly_interference",true).at("evidence").at("pairs")[0].at("intersection_volume_mm3"),50);
  auto zero=opt;zero["minimum_clearance_mm"]=0;
  const auto touching=BuiltModel(nested(10)).fabrication_review(zero);
  require(check(touching,"assembly_interference",true).at("status")=="pass"&&check(touching,"assembly_clearance",true).at("status")=="pass","Touching faces have no common material and meet an explicitly supplied zero gap");
  near(check(touching,"assembly_interference",true).at("evidence").at("pairs")[0].at("intersection_volume_mm3"),0);
  auto rotated=nested();rotated["features"][2]["parts"][1]["placement"]={{"translation",{5,6,0}},
    {"rotation",{{"origin",{0,0,0}},{"axis",{0,0,1}},{"angle_deg",90}}}};
  near(check(BuiltModel(rotated).fabrication_review(opt),"assembly_clearance",true).at("evidence").at("minimum_distance_mm"),1);
  auto many=nested();many["features"][2]["parts"]=Json::array();
  for(int i=0;i<24;++i)many["features"][2]["parts"].push_back({{"id","p"+std::to_string(i)},{"input","module"},{"placement",{{"translation",{i*12,0,0}}}}});
  const auto unmeasured=BuiltModel(many).fabrication_review(options(profile("fdm")));
  require(check(unmeasured,"assembly_interference",true).at("status")=="unknown","An assembly beyond the automatic pair budget never passes without measurements");
  auto selected=options(profile("cnc"));selected["clearance_pairs"]=Json::array({{{"a","p0/leaf"},{"b","p23/leaf"}}});
  const auto chosen=BuiltModel(many).fabrication_review(selected);
  near(check(chosen,"assembly_clearance",true).at("evidence").at("minimum_distance_mm"),266);
  auto override=options(profile("fdm"));override["parts"]=Json::array({{{"feature_id","block"},{"profile",profile("cnc")}}});
  require(BuiltModel(nested()).fabrication_review(override).at("parts")[0].at("profile").at("process")=="cnc","Explicit source process profile overrides the assembly-wide caller assumption");
  opt["clearance_pairs"]=Json::array({{{"a","left/leaf"},{"b","right/leaf"}}});
  require(check(BuiltModel(nested()).fabrication_review(opt),"assembly_clearance",true).at("evidence").at("scope")=="explicit_or_unmeasured_pairs","Explicit subset never claims global occurrence coverage");
  Temp temporary;Service service(temporary.root);const auto model=nested();
  service.call("cad_create",{{"document_id","fixture"},{"model",model}});
  const auto result=service.call("cad_fabrication_review",{{"document_id","fixture"},{"revision",1},{"options",opt}});
  const auto path=path_from_utf8(text_field(result,"path"));const auto bytes=read_text(path,64*1024*1024);const auto saved=parse_json(bytes,64*1024*1024);
  require(saved.at("source").at("model_sha256")==sha256(model.dump())&&result.at("model_sha256")==sha256(model.dump()),"Review binds saved source intent and native build");
  require(result.at("sha256")==sha256(bytes)&&result.at("bytes")==bytes.size(),"Review artifact independently matches reply hash and bytes");
  service.call("cad_apply",{{"document_id","fixture"},{"expected_revision",1},{"operations",Json::array({{{"op","set_part_placement"},{"assembly_id","assembly"},{"part_id","right"},{"placement",{{"translation",{8,0,0}}}}}})}});
  const auto request=Json{{"action","submit"},{"request_id","historical_review"},{"tool","cad_fabrication_review"},
    {"arguments",{{"document_id","fixture"},{"revision",1},{"options",opt}}}};
  job(service,request);const auto old=terminal(service,"historical_review");require(old.at("state")=="succeeded","Historical review executes through durable native jobs");
  near(check(old.at("result").at("report"),"assembly_clearance",true).at("evidence").at("minimum_distance_mm"),2);
  require(job(service,request).at("result")==old.at("result"),"Review job replay retains original artifact and identity");
  require(service.call("cad_read",{{"document_id","fixture"}}).at("revision")==2&&read_text(path,64*1024*1024)==bytes,"Historical review preserves HEAD and earlier report bytes");
  const auto package=service.call("cad_manufacture",{{"document_id","fixture"},{"revision",2},{"options",{{"drawings",false},{"fabrication_review",opt}}}});
  const auto manifest=parse_json(read_text(path_from_utf8(text_field(package,"path")),64*1024*1024),64*1024*1024);
  require(manifest.at("process_review").at("status")=="fail"&&manifest.at("process_review").at("report_path")=="review.json","Manufacturing package preserves measured findings instead of relabeling them as passed");
  const auto review_path=path_from_utf8(text_field(package,"directory"))/"review.json";bool hashed=false;
  for(const auto& a:manifest.at("artifacts"))if(a.at("path")=="review.json"){hashed=true;require(a.at("sha256")==sha256(read_text(review_path,64*1024*1024)),"Integrated review is a hashed portable package artifact");}
  require(hashed,"Measured manufacturing report is included in manifest artifacts");
  const auto before=exports(temporary.root);auto wrong=opt;wrong["clearance_pairs"][0]["a"]="left";
  fails("invalid_argument",[&]{service.call("cad_fabrication_review",{{"document_id","fixture"},{"revision",2},{"options",wrong}});});
  require(exports(temporary.root)==before,"Invalid group occurrence publishes no report");
  const auto leaf=service.call("cad_fabrication_review",{{"document_id","fixture"},{"revision",2},{"feature_id","block"},{"options",options(profile("cnc"))}});
  require(leaf.at("feature_id")=="block"&&leaf.at("report").at("parts").size()==1,"Scoped single-source review remains available from an assembly document");
}
void validation(){
  for(auto p:Json::array({profile("magic"),profile("fdm"),profile("fdm"),profile("cnc"),profile("sheet_laser")})){
    const auto i=checks;
    if(p.at("process")=="fdm"){if(i%2)p["orientation"]["build_direction"]={0,0,0};else p["orientation"]["x_direction"]={0,0,1};}
    else if(p.at("process")=="cnc")p["overhang_angle_deg"]=45;
    else if(p.at("process")=="sheet_laser")p["sheet_thickness_mm"]=2;
    fails("invalid_argument",[&]{validate_fabrication_options(options(p));});
  }
  auto repeated=options(profile("fdm"));repeated["clearance_pairs"]=Json::array({{{"a","left/leaf"},{"b","right/leaf"}},{{"a","right/leaf"},{"b","left/leaf"}}});
  fails("invalid_argument",[&]{validate_fabrication_options(repeated);});
  repeated=options(profile("fdm"));repeated["parts"]=Json::array({{{"feature_id","plate"},{"profile",profile("cnc")}},{{"feature_id","plate"},{"profile",profile("fdm")}}});
  fails("invalid_argument",[&]{validate_fabrication_options(repeated);});
  auto unselected=options(profile("fdm"));unselected["parts"]=Json::array({{{"feature_id","missing"},{"profile",profile("cnc")}}});
  fails("invalid_argument",[&]{BuiltModel(box()).fabrication_review(unselected);});
  auto empty=options(profile("fdm"));empty["clearance_pairs"]=Json::array();
  require(check(BuiltModel(nested()).fabrication_review(empty),"assembly_interference",true).at("status")=="unknown","Zero measured pairs never passes interference");
}
void cancellation(){
  Temp temporary;Service service(temporary.root);Json features=Json::array(),parts=Json::array();
  for(int i=0;i<48;++i){const auto id="b"+std::to_string(i);features.push_back({{"id",id},{"type","box"},{"size",{10+i,8,2}}});parts.push_back({{"id",id},{"input",id},{"placement",{{"translation",{i*100,0,0}}}}});}
  features.push_back({{"id","assembly"},{"type","assembly"},{"parts",parts}});const auto model=source(features,"assembly");
  service.call("cad_create",{{"document_id","cancel_review"},{"model",model}});
  fs::remove_all(temporary.root/".cache");const auto before=exports(temporary.root);
  job(service,{{"action","submit"},{"request_id","cancel_measured_review"},{"tool","cad_fabrication_review"},
    {"arguments",{{"document_id","cancel_review"},{"revision",1},{"options",options(profile("fdm"))}}},{"budget",{{"timeout_ms",90000}}}});
  bool staged=false;
  for(int i=0;i<3000&&!staged;++i){
    for(const auto& entry:fs::directory_iterator(temporary.root/".workers"))if(fs::exists(entry.path()/"feature-0.cache"))staged=true;
    if(!staged)std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  require(staged&&job(service,{{"action","get"},{"job_id","cancel_measured_review"}}).at("state")=="running","Cancellation reaches a live worker after an exact native dependency has been built and staged");
  job(service,{{"action","cancel"},{"job_id","cancel_measured_review"}});
  require(terminal(service,"cancel_measured_review").at("state")=="cancelled","Measured review cancellation becomes terminal");
  require(exports(temporary.root)==before&&service.call("cad_read",{{"document_id","cancel_review"}}).at("model")==model,"Cancelled review publishes no report and preserves complete saved intent");
}
}
int main(){try{configure_kernel_logging();set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));fdm();cnc_and_sheet();molding();clearance_and_history();validation();cancellation();std::cout<<checks<<" fabrication checks passed\n";return 0;}
catch(const Error& e){std::cerr<<e.code<<": "<<e.what()<<" "<<e.details.dump()<<'\n';return 1;}catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;}}
