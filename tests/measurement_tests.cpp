#include "agentcad/measurement.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/hash.hpp"
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <thread>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
void near(double a,double b,double tolerance=1e-6){require(std::abs(a-b)<tolerance,"Expected "+std::to_string(b)+", measured "+std::to_string(a));}
void fails(const std::string& code,const std::function<void()>& fn){try{fn();require(false,"Expected "+code);}catch(const Error& e){require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what());}}
struct Temp{fs::path root;Temp(){root=temporary_file(fs::temp_directory_path());fs::remove(root);directory(root);}~Temp(){std::error_code ignored;fs::remove_all(root,ignored);}};
Json model(Json features,const std::string& output){return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",features},{"output",output}};}
Json box(){return model(Json::array({{{"id","box"},{"type","box"},{"size",{20,10,2}}}}),"box");}
Json part(const std::string& id){return {{"kind","part"},{"part_id",id}};}
Json entity(const std::string& kind,const Json& descriptor){return {{"kind",kind},{"entity_id",descriptor.at("id")}};}
Json pair(Json a,Json b){return {{"action","pair"},{"targets",Json::array({a,b})}};}
Json assembly(Json translation,std::size_t count=2,bool rounded=false){
  auto result=box();std::string input="box";
  if(rounded){input="rounded";result["features"].push_back({{"id",input},{"type","fillet"},{"input","box"},{"radius",.2},{"edges","all"}});}
  Json parts=Json::array();for(std::size_t i=0;i<count;++i){auto placement=translation;for(auto& v:placement)v=v.get<double>()*i;
    parts.push_back({{"id","p"+std::to_string(i)},{"input",input},{"placement",{{"translation",placement}}}});}
  result["features"].push_back({{"id","assembly"},{"type","assembly"},{"parts",parts}});result["output"]="assembly";return result;
}
Json find(const Json& list,const std::function<bool(const Json&)>& test){for(const auto& item:list)if(test(item))return item;throw std::runtime_error("Missing analytic entity fixture");}
void witness(const Json& measured){for(const auto& item:measured.at("pairs"))for(const auto& w:item.at("witnesses")){
  double square=0;for(std::size_t k=0;k<3;++k){const auto a=w.at("a_mm")[k].get<double>(),b=w.at("b_mm")[k].get<double>();require(std::isfinite(a)&&std::isfinite(b),"Finite closest-point witness");square+=(a-b)*(a-b);}near(std::sqrt(square),item.at("distance_mm"));}}
Json get_job(Service& s,const Json& args){for(int i=0;i<1000;++i){try{return s.call("cad_job",args);}catch(const Error& e){if(e.code!="workspace_busy")throw;}std::this_thread::sleep_for(std::chrono::milliseconds(2));}throw std::runtime_error("Job metadata stayed busy");}
Json terminal(Service& s,const std::string& id){for(int i=0;i<10000;++i){auto j=get_job(s,{{"action","get"},{"job_id",id}});if(j.at("state")!="queued"&&j.at("state")!="running"&&j.at("state")!="cancelling")return j;std::this_thread::sleep_for(std::chrono::milliseconds(2));}throw std::runtime_error("Measurement job did not finish");}
void analytic(){
  BuiltModel b(box());const auto topo=b.topology();
  const auto face=[&](int axis,double value){return find(topo.at("faces"),[&](const Json& f){return std::abs(f.at("center_mm")[axis].get<double>()-value)<1e-8&&std::abs(f.at("normal")[axis].get<double>())>.9;});};
  auto measured=b.measure(pair(entity("face",face(2,0)),entity("face",face(2,2))),topo);
  near(measured.at("minimum_distance_mm"),2);near(measured.at("pairs")[0].at("angle_deg"),0);
  require(measured.at("pairs")[0].at("interference").is_null(),"Face distance does not claim material intersection");witness(measured);
  measured=b.measure(pair(entity("face",face(0,0)),entity("face",face(2,2))),topo);near(measured.at("minimum_distance_mm"),0);near(measured.at("pairs")[0].at("angle_deg"),90);
  const auto edge=find(topo.at("edges"),[](const Json& e){return e.at("curve_kind")=="line"&&std::abs(e.at("direction")[2].get<double>())>.9;});
  measured=b.measure(pair(entity("edge",edge),entity("face",face(2,2))),topo);near(measured.at("pairs")[0].at("angle_deg"),90);require(measured.at("pairs")[0].at("angle_method")=="line_to_plane","Mixed analytic angle has explicit meaning");
  auto stale=topo;for(auto& f:stale["faces"])if(f.at("id")==face(2,0).at("id"))f["area_mm2"]=123;
  fails("stale_selection",[&]{b.measure(pair(entity("face",face(2,0)),entity("face",face(2,2))),stale);});
  fails("selection_missing",[&]{b.measure(pair({{"kind","edge"},{"entity_id","edge-999"}},entity("face",face(2,2))),topo);});
  auto repeated=box();repeated["features"].push_back({{"id","twice"},{"type","circular_pattern"},{"input","box"},{"count",2},{"axis",{{"origin",{10,5,0}},{"direction",{0,0,1}}}},{"angle_deg",180}});repeated["output"]="twice";
  BuiltModel ambiguous(repeated);const auto repeated_topo=ambiguous.topology();
  fails("selection_ambiguous",[&]{ambiguous.measure(pair(entity("face",repeated_topo.at("faces")[0]),entity("face",repeated_topo.at("faces")[1])),repeated_topo);});
  BuiltModel diagonal(assembly({23,14,7}));measured=diagonal.measure(pair(part("p0"),part("p1")),Json::object());near(measured.at("minimum_distance_mm"),std::sqrt(50));witness(measured);
  require(measured.at("pairs")[0].at("interference")==false,"Separated solids report no common material");
  BuiltModel touching(assembly({20,0,0}));auto threshold=pair(part("p0"),part("p1"));threshold["minimum_clearance_mm"]=0;
  measured=touching.measure(threshold,Json::object());near(measured.at("minimum_distance_mm"),0);near(measured.at("pairs")[0].at("intersection_volume_mm3"),0);require(measured.at("status")=="pass","Touching material has no positive-volume interference at a supplied zero clearance");
  BuiltModel overlap(assembly({15,0,0}));measured=overlap.measure(threshold,Json::object());near(measured.at("pairs")[0].at("intersection_volume_mm3"),100);require(measured.at("status")=="fail"&&measured.at("interference_count")==1,"Positive overlap fails even with zero requested clearance");
  witness(measured);witness(touching.measure(threshold,Json::object()));
  threshold["minimum_clearance_mm"]=std::sqrt(50)+.1;require(diagonal.measure(threshold,Json::object()).at("status")=="fail","Explicit minimum rejects the actual source gap");
  auto contained=box();contained["features"].push_back({{"id","small"},{"type","box"},{"size",{1,1,.5}},{"origin",{5,5,.5}}});
  contained["features"].push_back({{"id","assembly"},{"type","assembly"},{"parts",Json::array({{{"id","outer"},{"input","box"}},{{"id","inner"},{"input","small"}}})}});contained["output"]="assembly";
  measured=BuiltModel(contained).measure(pair(part("outer"),part("inner")),Json::object());near(measured.at("minimum_distance_mm"),0);near(measured.at("pairs")[0].at("intersection_volume_mm3"),.5);witness(measured);
  const auto contacts=model(Json::array({{{"id","plate"},{"type","box"},{"size",{30,20,3}}},{{"id","pin"},{"type","cylinder"},{"radius",2},{"height",7}},
    {{"id","assembly"},{"type","assembly"},{"parts",Json::array({{{"id","lower"},{"input","plate"},{"placement",{{"translation",{25,40,0}}}}},{{"id","upper"},{"input","plate"},{"placement",{{"translation",{25,40,10}}}}},{{"id","pin"},{"input","pin"},{"placement",{{"translation",{30,45,3}}}}}})}}}),"assembly");
  measured=BuiltModel(contacts).measure({{"action","clearance"}},Json::object());near(measured.at("minimum_distance_mm"),0);witness(measured);
  for(const auto& item:measured.at("pairs")){require(!item.at("witnesses").empty(),"Every contact pair retains qualified native witnesses");
    for(const auto& w:item.at("witnesses"))for(const auto* key:{"a_mm","b_mm"})require(w.at(key)[0].get<double>()>=25-1e-7&&w.at(key)[1].get<double>()>=40-1e-7,"Contact witnesses stay within translated source material");
    require(item.at("solution_count").get<std::size_t>()>=item.at("witnesses").size()+item.at("rejected_witness_count").get<std::size_t>(),"Raw native candidates account for rejected witnesses");}
  const auto cylinders=model(Json::array({{{"id","a"},{"type","cylinder"},{"radius",2},{"height",3}},{{"id","b"},{"type","cylinder"},{"radius",3},{"height",3},{"origin",{10,0,0}}},
    {{"id","assembly"},{"type","assembly"},{"parts",Json::array({{{"id","a"},{"input","a"}},{{"id","b"},{"input","b"}}})}}}),"assembly");
  BuiltModel curved(cylinders);const auto curved_topo=curved.topology();const auto ca=find(curved_topo.at("faces"),[](const Json& f){return f.at("part_id")=="a"&&f.at("surface_kind")=="cylinder";}),cb=find(curved_topo.at("faces"),[](const Json& f){return f.at("part_id")=="b"&&f.at("surface_kind")=="cylinder";});
  measured=curved.measure(pair(entity("face",ca),entity("face",cb)),curved_topo);near(measured.at("minimum_distance_mm"),5);witness(measured);require(!measured.at("pairs")[0].contains("angle_deg"),"Curved surfaces do not receive a guessed analytic angle");
  auto nested=assembly({25,0,0});nested["features"].push_back({{"id","nested"},{"type","assembly"},{"parts",Json::array({{{"id","left"},{"input","assembly"}},{{"id","right"},{"input","assembly"},{"placement",{{"translation",{0,0,10}}}}}})}});nested["output"]="nested";BuiltModel hierarchy(nested);
  measured=hierarchy.measure(pair(part("left/p0"),part("right/p0")),Json::object());near(measured.at("minimum_distance_mm"),8);fails("selection_missing",[&]{hierarchy.measure(pair(part("left"),part("right/p0")),Json::object());});
  BuiltModel many(assembly({25,0,0},23));measured=many.measure({{"action","clearance"}},Json::object());near(measured.at("minimum_distance_mm"),5);require(measured.at("pairs").size()==253&&measured.at("coverage")=="all_assembly_leaves","Every unordered leaf pair is measured at the exact pair bound");
  measured=many.measure({{"action","clearance"},{"part_ids",{"p0","p2"}}},Json::object());near(measured.at("minimum_distance_mm"),30);require(measured.at("coverage")=="explicit_leaf_subset","Subset never claims complete assembly coverage");
  BuiltModel excessive(assembly({25,0,0},24));fails("limit_exceeded",[&]{excessive.measure({{"action","clearance"}},Json::object());});
  for(const auto& q:Json::array({Json{{"action","pair"},{"targets",Json::array({part("p0"),part("p0")})}},Json{{"action","clearance"},{"part_ids",{"p0","p0"}}},Json{{"action","clearance"},{"script","bad"}},Json{{"action","clearance"},{"minimum_clearance_mm",-1}}}))fails("invalid_argument",[&]{validate_measurement_query(q);});
}
void service(){
  Temp temp;Service s(temp.root);const auto source=assembly({25,0,0});s.call("cad_create",{{"document_id","fixture"},{"model",source}});
  const auto eval=s.call("cad_query",{{"document_id","fixture"},{"revision",1},{"kind","topology"}});
  Json args={{"document_id","fixture"},{"revision",1},{"evaluation_id",eval.at("evaluation_id")},{"feature_id","assembly"},{"query",pair(part("p0"),part("p1"))}};
  const auto before=read_text(temp.root/"documents/fixture/HEAD.json");const auto result=s.call("cad_measure",args);near(result.at("report").at("minimum_distance_mm"),5);
  require(result.at("model_sha256")==sha256(source.dump())&&result.at("native_build")==parse_json(read_text(temp.root/"evaluations"/(eval.at("evaluation_id").get<std::string>()+".json"))).at("_native_build"),"Measurement pins actual source/build identity");
  require(read_text(temp.root/"documents/fixture/HEAD.json")==before,"Measurement preserves raw HEAD");
  const auto top=find(eval.at("topology").at("faces"),[](const Json& f){return f.at("part_id")=="p0"&&std::abs(f.at("center_mm")[2].get<double>()-2)<1e-8&&std::abs(f.at("normal")[2].get<double>())>.9;}),bottom=find(eval.at("topology").at("faces"),[](const Json& f){return f.at("part_id")=="p0"&&std::abs(f.at("center_mm")[2].get<double>())<1e-8&&std::abs(f.at("normal")[2].get<double>())>.9;});
  auto faces=args;faces["query"]=pair(entity("face",top),entity("face",bottom));near(s.call("cad_measure",faces).at("report").at("minimum_distance_mm"),2);fs::remove_all(temp.root/".cache");near(s.call("cad_measure",faces).at("report").at("minimum_distance_mm"),2);
  const auto file=temp.root/"evaluations"/(eval.at("evaluation_id").get<std::string>()+".json");const auto metadata=read_text(file);auto corrupt=parse_json(metadata);corrupt["_native_build"]="other";atomic_text(file,corrupt.dump());
  fails("stale_selection",[&]{s.call("cad_measure",args);});atomic_text(file,metadata);
  auto bad=args;bad["feature_id"]="box";fails("stale_selection",[&]{s.call("cad_measure",bad);});
  bad=args;bad["query"]=pair(part("p0"),part("missing"));fails("selection_missing",[&]{s.call("cad_measure",bad);});
  const auto job=get_job(s,{{"action","submit"},{"request_id","measured"},{"tool","cad_measure"},{"arguments",args}});const auto completed=terminal(s,job.at("job_id"));require(completed.at("state")=="succeeded"&&completed.at("result")==result,"Durable job returns the exact native measurement contract");
  s.call("cad_apply",{{"document_id","fixture"},{"expected_revision",1},{"operations",Json::array({{{"op","set_part_placement"},{"assembly_id","assembly"},{"part_id","p1"},{"placement",{{"translation",{30,0,0}}}}}})}});
  fails("stale_selection",[&]{s.call("cad_measure",args);});
  Service restarted(temp.root);require(terminal(restarted,job.at("job_id")).at("result")==result,"Historical measurement job remains a qualified historical result");
  const auto fresh=restarted.call("cad_query",{{"document_id","fixture"},{"revision",2},{"kind","topology"}});args["revision"]=2;args["evaluation_id"]=fresh.at("evaluation_id");near(restarted.call("cad_measure",args).at("report").at("minimum_distance_mm"),10);
  const auto draft=restarted.call("cad_preview",{{"document_id","fixture"},{"expected_revision",2},{"operations",Json::array({{{"op","set_part_placement"},{"assembly_id","assembly"},{"part_id","p1"},{"placement",{{"translation",{35,0,0}}}}}})},{"kind","mesh"}});args["evaluation_id"]=draft.at("evaluation_id");fails("draft_selection",[&]{restarted.call("cad_measure",args);});
  args["evaluation_id"]=fresh.at("evaluation_id");fs::remove(temp.root/"evaluations"/(fresh.at("evaluation_id").get<std::string>()+".json"));fails("stale_selection",[&]{restarted.call("cad_measure",args);});
}
Json ready(Service& s,const std::string& view){for(int i=0;i<3000;++i){auto result=s.call("cad_viewer",{{"action","sync"},{"view_id",view}});if(result.at("state")=="ready")return result;require(result.at("state")=="loading","Live evaluation remains loading until ready");std::this_thread::sleep_for(std::chrono::milliseconds(5));}throw std::runtime_error("View did not become ready");}
void live(){
  Temp temp;Service s(temp.root);s.call("cad_create",{{"document_id","live"},{"model",assembly({25,0,0})}});s.call("cad_open",{{"document_id","live"},{"view_id","measure"}});
  auto shown=ready(s,"measure");const auto head=read_text(temp.root/"documents/live/HEAD.json");
  Json args={{"action","measure"},{"view_id","measure"},{"evaluation_id",shown.at("evaluation_id")},{"query",pair(part("p0"),part("p1"))}};
  const auto started=s.call("cad_viewer",args);require(started.contains("job_id"),"Viewer submits a native durable measurement job");args.erase("query");
  Json measured;for(int i=0;i<3000;++i){try{measured=s.call("cad_viewer",args);}catch(const Error& e){if(e.code!="workspace_busy")throw;continue;}if(measured.at("state")=="succeeded")break;std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  require(measured.at("state")=="succeeded","Viewer polls an actual native measurement result");near(measured.at("result").at("report").at("minimum_distance_mm"),5);
  const auto ctx=s.call("cad_context",{{"view_id","measure"}});require(ctx.at("measurement").at("job_id")==started.at("job_id"),"Agent context retains the source-qualified measurement job");
  s.call("cad_viewer",{{"action","context"},{"view_id","measure"},{"evaluation_id",shown.at("evaluation_id")},{"selection",nullptr},{"hidden_part_ids",{"p0","p1"}},
    {"presentation",{{"clip",{{"normal",{0,0,1}},{"offset_mm",100},{"keep","positive"}}},{"explode",{{"distance_mm",50},{"directions",Json::array()}}}}}});
  require(s.call("cad_viewer",args).at("result")==measured.at("result"),"Hiding, clipping and explosion do not alter the native source measurement");require(read_text(temp.root/"documents/live/HEAD.json")==head,"Native viewer measurement preserves raw source HEAD");
  Service reopened(temp.root);require(reopened.call("cad_viewer",args).at("result")==measured.at("result"),"Reopened view restores the exact durable source result");
  auto invalid=args;invalid["query"]={{"action","clearance"},{"script","bad"}};fails("invalid_argument",[&]{reopened.call("cad_viewer",invalid);});require(reopened.call("cad_context",{{"view_id","measure"}}).at("measurement")==ctx.at("measurement"),"Invalid queries cannot overwrite existing measurement context");
  auto clear=args;clear["query"]=nullptr;require(reopened.call("cad_viewer",clear).at("state")=="empty","Clear result removes native view measurement");require(!reopened.call("cad_context",{{"view_id","measure"}}).contains("measurement"),"Cleared context has no measurement reference");
  args["query"]=pair(part("p0"),part("p1"));reopened.call("cad_viewer",args);args.erase("query");
  reopened.call("cad_apply",{{"document_id","live"},{"expected_revision",1},{"operations",Json::array({{{"op","set_part_placement"},{"assembly_id","assembly"},{"part_id","p1"},{"placement",{{"translation",{30,0,0}}}}}})}});
  const auto stale=reopened.call("cad_context",{{"view_id","measure"}});require(stale.at("stale")==true&&!stale.contains("measurement"),"Changed HEAD cannot present old measurement context as current");
  fails("stale_selection",[&]{reopened.call("cad_viewer",args);});shown=ready(reopened,"measure");require(!shown.contains("measurement"),"New evaluation clears measurement metadata");
}
void cancellation(){
  Temp temp;Service s(temp.root);s.call("cad_create",{{"document_id","bounded"},{"model",assembly({25,0,0},23,true)}});
  auto shown=s.call("cad_query",{{"document_id","bounded"},{"revision",1},{"kind","topology"}});Json args={{"document_id","bounded"},{"revision",1},{"feature_id","assembly"},{"evaluation_id",shown.at("evaluation_id")},{"query",{{"action","clearance"}}}};
  const auto before=read_text(temp.root/"documents/bounded/HEAD.json");
  auto started=get_job(s,{{"action","submit"},{"request_id","cancel_measure"},{"tool","cad_measure"},{"arguments",args}});bool running=false;
  for(int i=0;i<1000;++i){auto current=get_job(s,{{"action","get"},{"job_id",started.at("job_id")}});if(current.at("state")=="running"){running=true;break;}if(current.at("state")!="queued")break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
  require(running,"Observe a live native measurement job before cancellation");get_job(s,{{"action","cancel"},{"job_id",started.at("job_id")}});require(terminal(s,started.at("job_id")).at("state")=="cancelled","Cancelled measurement terminates without a result");
  started=get_job(s,{{"action","submit"},{"request_id","deadline_measure"},{"tool","cad_measure"},{"arguments",args},{"budget",{{"timeout_ms",1},{"memory_mb",512}}}});const auto timed=terminal(s,started.at("job_id"));
  require(timed.at("state")=="failed"&&timed.at("error").at("code")=="job_timeout","Measurement obeys native worker deadline: "+timed.dump());require(read_text(temp.root/"documents/bounded/HEAD.json")==before,"Cancellation and deadline preserve raw HEAD");
}
}
int main(){try{configure_kernel_logging();set_worker_executable(CAD_SERVICE_EXE);analytic();service();live();cancellation();std::cout<<"measurement: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
