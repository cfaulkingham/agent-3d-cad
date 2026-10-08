#include "agentcad/section.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/hash.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <numbers>
#include <thread>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
void near(double a,double b,double tolerance=1e-6){require(std::abs(a-b)<tolerance,"Expected "+std::to_string(b)+", got "+std::to_string(a));}
void fails(const std::string& code,const std::function<void()>& fn){try{fn();require(false,"Expected "+code);}catch(const Error& e){require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what());}}
struct Temp{fs::path root;Temp(){root=temporary_file(fs::temp_directory_path());fs::remove(root);directory(root);}~Temp(){std::error_code ignored;fs::remove_all(root,ignored);}};
Json model(Json features,const std::string& output){return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",features},{"output",output}};}
Json box(){return model(Json::array({{{"id","box"},{"type","box"},{"size",{20,10,8}}}}),"box");}
Json query(Json normal,double offset){return {{"action","section"},{"plane",{{"normal",normal},{"offset_mm",offset}}}};}
void plane_points(const Json& r){
  const auto& n=r.at("plane").at("normal");
  const auto offset=[&](const Json& id){for(const auto& s:r.at("sections"))if(s.at("part_id")==id)return s.at("source_plane_offset_mm").get<double>();throw std::runtime_error("Missing plane scope");};
  const auto check=[&](const Json& p,const Json& id){double dot=0;for(int k=0;k<3;++k){require(std::isfinite(p[k].get<double>()),"Section coordinates finite");dot+=p[k].get<double>()*n[k].get<double>();}near(dot,offset(id));};
  for(const auto& region:r.at("regions"))check(region.at("center_mm"),region.at("part_id"));
  for(const auto& curve:r.at("curves")){check(curve.at("center_mm"),curve.at("part_id"));for(const auto& p:curve.at("points"))check(p,curve.at("part_id"));}
  const auto& mesh=r.at("mesh");require(mesh.at("triangles").size()==mesh.at("triangle_regions").size(),"Every cap triangle names its own derived region");
  for(std::size_t i=0;i<mesh.at("triangles").size();++i){const Json* region=nullptr;for(const auto& candidate:r.at("regions"))if(candidate.at("id")==mesh.at("triangle_regions")[i])region=&candidate;
    require(region!=nullptr,"Every cap region exists");for(const auto& index:mesh.at("triangles")[i]){require(index.get<std::size_t>()<mesh.at("positions").size(),"Cap index stays in bounds");check(mesh.at("positions")[index.get<std::size_t>()],region->at("part_id"));}}
}
void analytic(){
  BuiltModel solid(box());const auto before=solid.summary();auto r=solid.section(query({0,0,1},4));
  near(r.at("area_mm2"),200);near(r.at("boundary_length_mm"),60);require(r.at("regions").size()==1&&r.at("regions")[0].at("wire_count")==1&&r.at("curves").size()==4,"Box has one exact rectangular material cap");plane_points(r);
  require(solid.summary()==before,"Section computation preserves exact source measurements");
  r=solid.section(query({0,0,1},0));near(r.at("area_mm2"),200);plane_points(r);
  r=solid.section(query({0,0,1},1e12));require(r.at("status")=="empty"&&r.at("regions").empty()&&r.at("curves").empty()&&r.at("mesh").at("positions").empty(),"Outside plane produces an explicit complete empty section");
  r=solid.section(query({1/std::sqrt(3.),1/std::sqrt(3.),1/std::sqrt(3.)},0));require(r.at("status")=="tangent"&&r.at("area_mm2")==0&&!r.at("sections")[0].at("contact_points").empty(),"Corner tangency preserves point-only intersection without inventing a cap");
  const auto cylinder=model(Json::array({{{"id","cylinder"},{"type","cylinder"},{"radius",3},{"height",20}}}),"cylinder");
  r=BuiltModel(cylinder).section(query({1/std::sqrt(2.),0,1/std::sqrt(2.)},10/std::sqrt(2.)));near(r.at("area_mm2"),9*std::numbers::pi*std::sqrt(2.),1e-5);require(r.at("curves")[0].at("curve_kind")=="ellipse","Oblique round cylinder creates an analytic ellipse");plane_points(r);
  r=BuiltModel(cylinder).section(query({1,0,0},3));require(r.at("status")=="tangent"&&r.at("regions").empty(),"Cylinder tangent plane has curves but no material cap");near(r.at("boundary_length_mm"),20);plane_points(r);
  const auto ring=model(Json::array({{{"id","outer"},{"type","cylinder"},{"radius",5},{"height",10}},{{"id","inner"},{"type","cylinder"},{"radius",2},{"height",10}},{{"id","ring"},{"type","cut"},{"left","outer"},{"right","inner"}}}),"ring");
  r=BuiltModel(ring).section(query({0,0,1},5));near(r.at("area_mm2"),21*std::numbers::pi);near(r.at("boundary_length_mm"),14*std::numbers::pi);require(r.at("regions")[0].at("wire_count")==2,"Native annular cap preserves its interior wire");plane_points(r);
  const auto& mesh=r.at("mesh");for(const auto& t:mesh.at("triangles")){
    const auto& a=mesh.at("positions")[t[0].get<std::size_t>()];const auto& b=mesh.at("positions")[t[1].get<std::size_t>()];const auto& c=mesh.at("positions")[t[2].get<std::size_t>()];
    const auto side=[](const Json& x,const Json& y){return x[0].get<double>()*y[1].get<double>()-x[1].get<double>()*y[0].get<double>();};
    const auto ab=side(a,b),bc=side(b,c),ca=side(c,a);require(!(ab>=-1e-10&&bc>=-1e-10&&ca>=-1e-10)&&!(ab<=1e-10&&bc<=1e-10&&ca<=1e-10),"A cap triangle does not cover the actual bore center");}
  auto pattern=box();pattern["features"].push_back({{"id","twice"},{"type","circular_pattern"},{"input","box"},{"count",2},{"axis",{{"origin",{10,5,0}},{"direction",{0,0,1}}}},{"angle_deg",180}});pattern["output"]="twice";
  r=BuiltModel(pattern).section(query({0,0,1},4));near(r.at("area_mm2"),400);require(r.at("regions").size()==2&&r.at("area_semantics")=="sum_of_solid_sections","Interfering source solids retain separate caps, never claim union area");
  for(const auto& bad:Json::array({query({0,0,0},4),query({0,0,2},4),query({0,0,1},1e13),Json{{"action","section"},{"plane",{{"normal",{0,0,1}},{"offset_mm",4},{"script","bad"}}}},Json{{"action","section"},{"plane",{{"normal",{0,0,1}},{"offset_mm",4}}},{"part_ids",Json::array()}}}))fails("invalid_argument",[&]{solid.section(bad);});
  auto subset=query({0,0,1},4);subset["part_ids"]={"box"};fails("invalid_argument",[&]{solid.section(subset);});
}
Json assembly(){auto source=box();source["features"].push_back({{"id","assembly"},{"type","assembly"},{"parts",Json::array({{{"id","lower"},{"input","box"}},{{"id","upper"},{"input","box"},{"placement",{{"translation",{0,0,12}}}}}})}});source["output"]="assembly";return source;}
void composition(){
  BuiltModel source(assembly());auto q=query({0,0,1},10);auto r=source.section(q);require(r.at("status")=="empty","Source plane lies between saved parts");
  q["explode"]={{"distance_mm",8},{"directions",Json::array({{{"part_id","lower"},{"direction",{0,0,1}}},{{"part_id","upper"},{"direction",{0,0,-1}}}})}};
  r=source.section(q);near(r.at("area_mm2"),400);near(r.at("sections")[0].at("source_plane_offset_mm"),2);near(r.at("sections")[1].at("source_plane_offset_mm"),18);plane_points(r);
  for(const auto& s:r.at("sections")){const auto& n=q.at("plane").at("normal");double offset=s.at("source_plane_offset_mm");for(int i=0;i<3;++i)offset+=n[i].get<double>()*s.at("displacement_mm")[i].get<double>();near(offset,10);}
  q["part_ids"]={"upper"};r=source.section(q);near(r.at("area_mm2"),200);require(r.at("coverage")=="explicit_leaf_subset"&&r.at("sections").size()==1,"One-leaf query never claims complete assembly coverage");
  auto invalid=q;invalid["part_ids"]={"missing"};fails("selection_missing",[&]{source.section(invalid);});invalid=q;invalid["explode"]["directions"][0]["part_id"]="missing";fails("selection_missing",[&]{source.section(invalid);});
  q.erase("part_ids");q["explode"]={{"distance_mm",8},{"directions",Json::array()}};r=source.section(q);near(r.at("sections")[0].at("displacement_mm")[2],-8);near(r.at("sections")[1].at("displacement_mm")[2],8);require(r.at("status")=="empty","Default radial offsets match the displayed world plane");
  auto nested=assembly();nested["features"].push_back({{"id","nested"},{"type","assembly"},{"parts",Json::array({{{"id","left"},{"input","assembly"}},{{"id","right"},{"input","assembly"},{"placement",{{"translation",{30,0,0}}}}}})}});nested["output"]="nested";
  q=query({0,0,1},4);q["part_ids"]={"right/lower"};r=BuiltModel(nested).section(q);near(r.at("area_mm2"),200);near(r.at("regions")[0].at("center_mm")[0],40);require(r.at("regions")[0].at("part_id")=="right/lower","Derived nested cap keeps its full occurrence path");plane_points(r);
}
Json terminal(Service& service,const std::string& id){for(int i=0;i<10000;++i){try{auto j=service.call("cad_job",{{"action","get"},{"job_id",id}});if(j.at("state")!="queued"&&j.at("state")!="running"&&j.at("state")!="cancelling")return j;}catch(const Error& e){if(e.code!="workspace_busy")throw;}std::this_thread::sleep_for(std::chrono::milliseconds(2));}throw std::runtime_error("Section job did not finish");}
void service(){
  Temp temp;Service service(temp.root);const auto source=box();service.call("cad_create",{{"document_id","section"},{"model",source}});const auto shown=service.call("cad_query",{{"document_id","section"},{"revision",1},{"kind","topology"}});
  Json args={{"document_id","section"},{"revision",1},{"evaluation_id",shown.at("evaluation_id")},{"feature_id","box"},{"query",query({0,0,1},4)}};
  const auto head=read_text(temp.root/"documents/section/HEAD.json");const auto result=service.call("cad_measure",args);near(result.at("report").at("area_mm2"),200);require(result.at("model_sha256")==sha256(source.dump()),"Section result pins actual source identity");
  fs::remove_all(temp.root/".cache");near(service.call("cad_measure",args).at("report").at("area_mm2"),200);require(read_text(temp.root/"documents/section/HEAD.json")==head,"Native section preserves raw source HEAD");
  const auto job=service.call("cad_job",{{"action","submit"},{"request_id","exact_section"},{"tool","cad_measure"},{"arguments",args}});const auto done=terminal(service,job.at("job_id"));require(done.at("state")=="succeeded","Native section durable job completes");near(done.at("result").at("report").at("area_mm2"),200);
  service.call("cad_apply",{{"document_id","section"},{"expected_revision",1},{"operations",Json::array({{{"op","replace_feature"},{"id","box"},{"feature",{{"id","box"},{"type","box"},{"size",{30,10,8}}}}}})}});
  fails("stale_selection",[&]{service.call("cad_measure",args);});Service restarted(temp.root);near(terminal(restarted,job.at("job_id")).at("result").at("report").at("area_mm2"),200);
}
Json call(Service& service,const std::string& name,const Json& arguments){
  for(int attempt=0;;++attempt){try{return service.call(name,arguments);}catch(const Error& e){if(e.code!="workspace_busy"||attempt>=1000)throw;}std::this_thread::sleep_for(std::chrono::milliseconds(2));}
}
Json ready(Service& service,const std::string& view){
  for(int i=0;i<10000;++i){auto r=call(service,"cad_viewer",{{"action","sync"},{"view_id",view}});if(r.at("state")=="ready")return r;
    if(r.at("state")!="loading")throw std::runtime_error("Section view failed: "+r.dump());std::this_thread::sleep_for(std::chrono::milliseconds(2));}throw std::runtime_error("Section view did not load");
}
Json poll_section(Service& service,const Json& args){
  for(int i=0;i<10000;++i){auto r=call(service,"cad_viewer",args);if(r.at("state")!="queued"&&r.at("state")!="running"&&r.at("state")!="cancelling")return r;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
  throw std::runtime_error("Live section did not finish");
}
void live(){
  Temp temp;Service service(temp.root);call(service,"cad_create",{{"document_id","assembly"},{"model",assembly()}});
  call(service,"cad_create",{{"document_id","other"},{"model",box()}});call(service,"cad_open",{{"document_id","assembly"},{"view_id","caps"}});
  auto shown=ready(service,"caps");const auto eid=shown.at("evaluation_id");const auto head=read_text(temp.root/"documents/assembly/HEAD.json");
  Json p={{"clip",{{"normal",{0,0,1}},{"offset_mm",4},{"keep","negative"}}},{"explode",{{"distance_mm",0},{"directions",Json::array()}}}};
  Json context={{"action","context"},{"view_id","caps"},{"evaluation_id",eid},{"selection",nullptr},{"presentation",p}};
  call(service,"cad_viewer",context);Json action={{"action","section"},{"view_id","caps"},{"evaluation_id",eid}};
  auto start=action;start["query"]=query({0,0,1},4);const auto begun=call(service,"cad_viewer",start);require(begun.contains("job_id"),"Section start acknowledges a durable native job");
  auto finished=poll_section(service,action);require(finished.at("state")=="succeeded","Live section completes independently");near(finished.at("result").at("report").at("area_mm2"),200);
  require(ready(service,"caps").at("section").at("job_id")==begun.at("job_id"),"Ready sync retains current section identity");
  Json pair={{"action","measure"},{"view_id","caps"},{"evaluation_id",eid},{"query",{{"action","pair"},{"targets",Json::array({{{"kind","part"},{"part_id","lower"}},{{"kind","part"},{"part_id","upper"}}})}}}};
  const auto measured=call(service,"cad_viewer",pair);terminal(service,measured.at("job_id"));
  p["clip"]["keep"]="positive";context["presentation"]=p;context["hidden_part_ids"]={"lower"};const auto reversed=call(service,"cad_viewer",context);
  require(reversed.at("section").at("job_id")==begun.at("job_id")&&reversed.contains("measurement"),"Kept-side reversal and hidden leaves preserve independent native reports");
  Service restarted(temp.root);near(poll_section(restarted,action).at("result").at("report").at("area_mm2"),200);
  const auto before=read_text(temp.root/"views/caps/state.json");auto invalid=start;invalid["query"]["plane"]["offset_mm"]=3;
  fails("stale_selection",[&]{call(service,"cad_viewer",invalid);});invalid=start;invalid["action"]="measure";fails("invalid_argument",[&]{call(service,"cad_viewer",invalid);});invalid=pair;invalid["action"]="section";fails("invalid_argument",[&]{call(service,"cad_viewer",invalid);});
  require(read_text(temp.root/"views/caps/state.json")==before,"Rejected live plane/type queries preserve view state");
  p["clip"]["offset_mm"]=10;p["explode"]={{"distance_mm",8},{"directions",Json::array({{{"part_id","lower"},{"direction",{0,0,1}}},{{"part_id","upper"},{"direction",{0,0,-1}}}})}};
  context["presentation"]=p;const auto moved=call(service,"cad_viewer",context);require(!moved.contains("section")&&moved.contains("measurement"),"Changing plane/explosion retires only the section");
  start["query"]=query({0,0,1},10);start["query"]["explode"]=p.at("explode");call(service,"cad_viewer",start);finished=poll_section(service,action);near(finished.at("result").at("report").at("area_mm2"),400);
  const auto exploded_job=finished.at("job_id");std::reverse(p["explode"]["directions"].begin(),p["explode"]["directions"].end());context["presentation"]=p;
  require(call(service,"cad_viewer",context).at("section").at("job_id")==exploded_job,"Reordering identical direction overrides preserves geometric qualification");
  start["query"]["part_ids"]={"upper"};call(service,"cad_viewer",start);finished=poll_section(service,action);near(finished.at("result").at("report").at("area_mm2"),200);
  require(finished.at("result").at("report").at("coverage")=="explicit_leaf_subset","Native live reports retain partial cap coverage");
  auto clear=action;clear["query"]=nullptr;require(call(service,"cad_viewer",clear).at("state")=="empty","Clear retires the native section slot");
  p["clip"]["offset_mm"]=1e12;context["presentation"]=p;call(service,"cad_viewer",context);start["query"]=query({0,0,1},1e12);start["query"]["explode"]=p.at("explode");call(service,"cad_viewer",start);
  require(poll_section(service,action).at("result").at("report").at("status")=="empty","Declared large native live plane offsets produce complete empty sections");
  p["clip"]=nullptr;context["presentation"]=p;require(!call(service,"cad_viewer",context).contains("section"),"Disabling clipping retires caps");fails("stale_selection",[&]{call(service,"cad_viewer",start);});
  require(read_text(temp.root/"documents/assembly/HEAD.json")==head,"All section and view actions preserve raw saved source");
  p["clip"]={{"normal",{0,0,1}},{"offset_mm",4},{"keep","negative"}};p["explode"]={{"distance_mm",0},{"directions",Json::array()}};context["presentation"]=p;call(service,"cad_viewer",context);
  start["query"]=query({0,0,1},4);call(service,"cad_viewer",start);const auto old=poll_section(service,action);
  call(service,"cad_apply",{{"document_id","assembly"},{"expected_revision",1},{"operations",Json::array({{{"op","replace_feature"},{"id","box"},{"feature",{{"id","box"},{"type","box"},{"size",{30,10,8}}}}}})}});
  require(!call(service,"cad_context",{{"view_id","caps"}}).contains("section"),"Changed HEAD excludes obsolete section context");fails("stale_selection",[&]{call(service,"cad_viewer",action);});
  require(!ready(service,"caps").contains("section"),"New evaluation retires native section metadata");near(terminal(restarted,old.at("job_id")).at("result").at("report").at("area_mm2"),200);
  call(service,"cad_show",{{"view_id","caps"},{"document_id","other"}});require(!ready(service,"caps").contains("section"),"Retargeted views cannot carry another document's caps");
}
void cancellation(){
  Temp temp;Service service(temp.root);auto many=box();Json parts=Json::array();for(int i=0;i<64;++i)parts.push_back({{"id","part"+std::to_string(i)},{"input","box"},{"placement",{{"translation",{24*i,0,0}}}}});
  many["features"].push_back({{"id","row"},{"type","assembly"},{"parts",parts}});
  many["features"].push_back({{"id","assembly"},{"type","assembly"},{"parts",Json::array({{{"id","rowa"},{"input","row"}},{{"id","rowb"},{"input","row"},{"placement",{{"translation",{0,20,0}}}}}})}});many["output"]="assembly";
  call(service,"cad_create",{{"document_id","many"},{"model",many}});call(service,"cad_open",{{"document_id","many"},{"view_id","work"}});const auto shown=ready(service,"work"),eid=shown.at("evaluation_id");
  const auto head=read_text(temp.root/"documents/many/HEAD.json");const Json p={{"clip",{{"normal",{0,0,1}},{"offset_mm",4},{"keep","negative"}}},{"explode",{{"distance_mm",0},{"directions",Json::array()}}}};
  call(service,"cad_viewer",{{"action","context"},{"view_id","work"},{"evaluation_id",eid},{"selection",nullptr},{"presentation",p}});
  Json action={{"action","section"},{"view_id","work"},{"evaluation_id",eid},{"query",query({0,0,1},4)}};const auto started=call(service,"cad_viewer",action);
  bool running=false;for(int i=0;i<1000;++i){const auto job=call(service,"cad_job",{{"action","get"},{"job_id",started.at("job_id")}});if(job.at("state")=="running"){running=true;break;}if(job.at("state")!="queued")break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
  require(running,"Actual native section worker is observed running before cancellation");action["query"]=nullptr;require(call(service,"cad_viewer",action).at("state")=="empty","Clearing running native section detaches view metadata");
  const auto cancelled=terminal(service,started.at("job_id"));require(cancelled.at("state")=="cancelled"&&!cancelled.contains("result"),"Cancelled section never publishes a partial cap report: "+cancelled.dump());
  action["query"]=query({0,0,1},4);const auto moving=call(service,"cad_viewer",action);running=false;
  for(int i=0;i<1000;++i){const auto job=call(service,"cad_job",{{"action","get"},{"job_id",moving.at("job_id")}});if(job.at("state")=="running"){running=true;break;}if(job.at("state")!="queued")break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
  require(running,"Observe running section before moving its plane");auto moved=p;moved["clip"]["offset_mm"]=3;
  const auto retired=call(service,"cad_viewer",{{"action","context"},{"view_id","work"},{"evaluation_id",eid},{"selection",nullptr},{"presentation",moved}});
  require(!retired.contains("section")&&terminal(service,moving.at("job_id")).at("state")=="cancelled","Moving plane cancels the running job outside document/view locks");
  Json subset=Json::array();for(int i=0;i<23;++i)subset.push_back("rowa/part"+std::to_string(i));
  Json measure={{"action","measure"},{"view_id","work"},{"evaluation_id",eid},{"query",{{"action","clearance"},{"part_ids",subset}}}};
  const auto measuring=call(service,"cad_viewer",measure);running=false;
  for(int i=0;i<1000;++i){const auto job=call(service,"cad_job",{{"action","get"},{"job_id",measuring.at("job_id")}});if(job.at("state")=="running"){running=true;break;}if(job.at("state")!="queued")break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
  require(running,"Observe running clearance job before clearing its independent slot");measure["query"]=nullptr;
  require(call(service,"cad_viewer",measure).at("state")=="empty"&&terminal(service,measuring.at("job_id")).at("state")=="cancelled","Live measurement cancellation also avoids nested document locks");
  const Json args={{"document_id","many"},{"revision",1},{"feature_id","assembly"},{"evaluation_id",eid},{"query",query({0,0,1},4)}};
  const auto timeout=call(service,"cad_job",{{"action","submit"},{"request_id","section_deadline"},{"tool","cad_measure"},{"arguments",args},{"budget",{{"timeout_ms",1},{"memory_mb",2048}}}});
  const auto failed=terminal(service,timeout.at("job_id"));require(failed.at("state")=="failed"&&failed.at("error").at("code")=="job_timeout"&&!failed.contains("result"),"Section native worker deadline preserves failure without partial caps");
  require(read_text(temp.root/"documents/many/HEAD.json")==head,"Cancelled/timed-out native sections preserve raw HEAD");
}
}
int main(){try{configure_kernel_logging();set_worker_executable(CAD_SERVICE_EXE);analytic();composition();service();live();cancellation();std::cout<<"section: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
