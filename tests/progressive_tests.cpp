#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/hash.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
using namespace agentcad;
namespace {
int checks=0;
void require(bool condition,const char* why){++checks;if(!condition)throw std::runtime_error(why);}
template<class F>void fails(const std::string& code,F fn){try{fn();}catch(const Error&e){require(e.code==code,("Expected "+code+", got "+e.code+": "+e.what()).c_str());return;}throw std::runtime_error("Expected "+code);}
struct Temp{fs::path root=fs::temp_directory_path()/("cad-progressive-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));Temp(){directory(root);}~Temp(){std::error_code e;fs::remove_all(root,e);}};
Json call(Service&s,const std::string& tool,const Json&a){for(int n=0;;++n){try{return s.call(tool,a);}catch(const Error&e){if(e.code!="workspace_busy"||n>1000)throw;}std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
Json fixture(){auto m=parse_json(read_text(path_from_utf8(CAD_SOURCE_DIR)/"examples/nested-assembly.create.json")).at("model");m["features"][1]={{"id","rod"},{"type","cylinder"},{"radius",3.123456789},{"height",6.345678912}};return m;}
void mapping(const BuiltModel& built,const Json& manifest){
 const auto& topology=manifest.at("topology");for(const auto&o:manifest.at("components").at("occurrences")){
  const auto source=built.topology(o.at("geometry_id"));const auto& m=o.at("transform");
  for(const auto* key:{"faces","edges"})for(std::size_t i=0;i<o.at(key).size();++i){const auto id=o.at(key)[i];const Json* placed=nullptr;for(const auto&x:topology.at(key))if(x.at("id")==id)placed=&x;
   require(placed&&placed->at("part_id")==o.at("id"),"Every exact placed mapping owns its occurrence");const auto p=source.at(key)[i].at("center_mm");
   for(int a=0;a<3;++a){double expected=number(m[a*4+3]);for(int k=0;k<3;++k)expected+=number(m[a*4+k])*number(p[k]);require(std::abs(expected-number(placed->at("center_mm")[a]))<1e-7,"Exact transform history maps each source center to its displayed descriptor");}
  }
 }
}
void equivalent(const Json&a,const Json&b){if(a.is_number()&&b.is_number()){require(std::abs(number(a)-number(b))<1e-7,"Roundtrip descriptor remains within native geometry tolerance");return;}require(a.type()==b.type(),"Descriptor structure survives cache");if(a.is_object()){require(a.size()==b.size(),"Descriptor field count survives cache");for(const auto& [key,value]:a.items())equivalent(value,b.at(key));}else if(a.is_array()){require(a.size()==b.size(),"Descriptor order/cardinality survives cache");for(std::size_t i=0;i<a.size();++i)equivalent(a[i],b[i]);}else require(a==b,"Descriptor labels and correspondence survive cache");}
void run(){
 const auto model=fixture();validate_model(model);BuiltModel built(model);const auto summary=built.summary();auto manifest=built.component_manifest();require(!manifest.contains("mesh"),"Manifest never tessellates the complete scene");require(manifest.at("components").at("geometries").size()==2,"Repeated geometry sources are deduplicated");require(manifest.at("components").at("occurrences").size()==5,"Nested leaf occurrences retain all placements");mapping(built,manifest);
 const auto coarse=built.component_mesh("","rod",0),medium=built.component_mesh("","rod",1),fine=built.component_mesh("","rod",2),coarse_again=built.component_mesh("","rod",0);
 require(coarse.at("mesh").at("triangles").size()<medium.at("mesh").at("triangles").size()&&medium.at("mesh").at("triangles").size()<fine.at("mesh").at("triangles").size(),"Curved exact sources refine with requested detail");
 require(coarse==coarse_again,"Coarsening discards prior fine triangulations and regenerates the requested level");require(summary==built.summary(),"All display tessellation leaves exact mass properties unchanged");
 BuiltModel restored(model,built.snapshot());const auto cached=restored.component_manifest();mapping(restored,cached);equivalent(cached,manifest);require(restored.component_mesh("","rod",2).at("topology_sha256")==fine.at("topology_sha256"),"Snapshot cache preserves qualified source topology");
 for(const auto& g:manifest.at("components").at("geometries"))for(int level=0;level<3;++level)require(restored.component_mesh("",g.at("id"),level).at("mesh").at("linear_deflection_mm")==g.at("levels")[level].at("linear_deflection_mm"),"Cache restoration preserves exact declared detail tolerances for noninteger source bounds");
 fails("invalid_argument",[&]{built.component_mesh("","unknown",0);});fails("invalid_argument",[&]{built.component_mesh("","rod",3);});
 Json many={{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",Json::array({{{"id","block"},{"type","box"},{"size",{1,2,3}}},{{"id","row"},{"type","assembly"},{"parts",Json::array()}},{{"id","array"},{"type","assembly"},{"parts",Json::array()}}})},{"output","array"}};
 for(int i=0;i<32;++i)many["features"][1]["parts"].push_back({{"id","p"+std::to_string(i)},{"input","block"},{"placement",{{"translation",{i*3,0,0}}}}});
 for(int i=0;i<32;++i)many["features"][2]["parts"].push_back({{"id","r"+std::to_string(i)},{"input","row"},{"placement",{{"translation",{0,i*4,0}}}}});
 validate_model(many);BuiltModel large(many);fails("limit_exceeded",[&]{large.topology();});const auto large_manifest=large.component_manifest();require(large_manifest.at("components").at("occurrences").size()==1024,"Manifest handles 1024 independently placed leaves beyond old global topology ceiling");require(large_manifest.at("components").at("geometries").size()==1,"1024 occurrences reuse one local source");require(large.component_mesh("","block",0).at("mesh").at("triangles").size()==12,"Large scene request tessellates only its one source");
 Temp t;Service service(t.root);call(service,"cad_create",{{"document_id","assembly"},{"model",model}});const auto head=read_text(t.root/"documents/assembly/HEAD.json");
 auto evaluation=call(service,"cad_query",{{"document_id","assembly"},{"revision",1},{"kind","component_manifest"}});auto args=Json{{"document_id","assembly"},{"revision",1},{"kind","component_mesh"},{"evaluation_id",evaluation.at("evaluation_id")},{"geometry_id","rod"},{"level",0}};
 auto result=call(service,"cad_query",args);require(result.at("evaluation_id")==evaluation.at("evaluation_id"),"Component result retains manifest evaluation");require(!result.contains("topology")&&!result.contains("mesh"),"Independent component result has no aggregate mesh");
 args["geometry_id"]="unknown";fails("invalid_argument",[&]{call(service,"cad_query",args);});args["geometry_id"]="rod";
 call(service,"cad_open",{{"document_id","assembly"},{"view_id","progressive"}});Json live;
 for(int n=0;n<10000;++n){live=call(service,"cad_viewer",{{"action","sync"},{"view_id","progressive"},{"progressive",true}});if(live.at("state")=="ready")break;require(live.at("state")=="loading","Live manifest native job remains responsive");std::this_thread::sleep_for(std::chrono::milliseconds(2));}
 require(live.value("progressive",false),"Live sync negotiates geometry-free progressive mode");std::string frozen;Json offset=0;
 do{const auto chunk=call(service,"cad_viewer",{{"action","mesh"},{"view_id","progressive"},{"evaluation_id",live.at("evaluation_id")},{"offset",offset}});frozen+=chunk.at("data").get<std::string>();offset=chunk.at("next_offset");}while(!offset.is_null());require(!parse_json(frozen).contains("mesh"),"Initial live transfer contains zero triangles");
 Json request={{"action","component"},{"view_id","progressive"},{"evaluation_id",live.at("evaluation_id")},{"geometry_id","rod"},{"level",2},{"request_id","fixture"}};std::string mesh;
 for(int n=0;n<10000;++n){const auto r=call(service,"cad_viewer",request);if(r.at("component_state")=="loading"){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}require(r.at("component_state")=="ready","Independent component worker completes");mesh+=r.at("data").get<std::string>();if(r.at("next_offset").is_null())break;request["offset"]=r.at("next_offset");}
 require(parse_json(mesh).at("component_mesh").at("level")==2,"Chunked component retains requested level");require(read_text(t.root/"documents/assembly/HEAD.json")==head,"Progressive browsing never edits source HEAD");
 Json cancel=request;cancel.erase("offset");cancel["request_id"]="cancel_me";cancel["level"]=1;call(service,"cad_viewer",cancel);cancel["cancel"]=true;require(call(service,"cad_viewer",cancel).at("component_state")=="cancelled","Component worker cancellation is explicit");
 call(service,"cad_apply",{{"document_id","assembly"},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","angle"},{"value",45}}})}});
 fails("stale_selection",[&]{call(service,"cad_query",args);});fails("stale_selection",[&]{call(service,"cad_viewer",request);});
 auto single=model;single["output"]="rod";BuiltModel one(single);const auto single_manifest=one.component_manifest();require(single_manifest.at("components").at("occurrences")[0].at("id").is_null(),"Standalone parts use an identity occurrence without invented assembly ownership");require(one.component_mesh("","rod",0).at("mesh").at("triangles").size()>0,"Standalone progressive display has real geometry");
}
}
int main(){try{set_worker_executable(CAD_SERVICE_EXE);run();std::cout<<"progressive: "<<checks<<" checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
