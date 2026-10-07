#include "agentcad/cache.hpp"
#include "agentcad/drawing.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/service.hpp"
#include <cmath>
#include <array>
#include <future>
#include <iostream>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message) { ++checks; if(!value) throw std::runtime_error(message); }
struct Temp {
  fs::path path;
  Temp() { path=temporary_file(fs::temp_directory_path()); fs::remove(path); directory(path); }
  ~Temp() { std::error_code ignored; fs::remove_all(path,ignored); }
};
Json box() {
  return {{"schema_version",1},{"units","mm"},{"parameters",{{"width",20}}},
    {"features",Json::array({{{"id","base"},{"type","box"},{"size",Json::array({Json{{"parameter","width"}},10,6})}},
      {{"id","bore"},{"type","hole"},{"input","base"},{"origin",{10,5,-1}},{"axis",{0,0,1}},{"radius",2},{"depth",8}}})},{"output","bore"}};
}
void equivalent(const Json& a,const Json& b,const std::string& path="") {
  if(a.is_number() && b.is_number()) {
    require(std::abs(a.get<double>()-b.get<double>())<=1e-8*std::max(1.0,std::abs(a.get<double>())),"Round-trip geometry value changed at "+path+": "+a.dump()+" vs "+b.dump());
  } else if(a.is_object() || a.is_array()) {
    require(a.type()==b.type() && a.size()==b.size(),"Round-trip structure changed");
    if(a.is_object()) for(const auto& [key,value]:a.items()) equivalent(value,b.at(key),path+"/"+key);
    else for(std::size_t i=0;i<a.size();++i) equivalent(a[i],b[i],path+"/"+std::to_string(i));
  } else require(a==b,"Round-trip metadata changed");
}
Json mesh_metrics(const Json& mesh) {
  Json result=Json::object();
  for(std::size_t i=0;i<mesh.at("triangles").size();++i) {
    const auto& triangle=mesh.at("triangles")[i];
    std::array<std::array<double,3>,3> p;
    for(int v=0;v<3;++v) p[v]=mesh.at("positions").at(triangle[v].get<std::size_t>()).get<std::array<double,3>>();
    std::array<double,3> u,v,c;
    for(int j=0;j<3;++j) { u[j]=p[1][j]-p[0][j]; v[j]=p[2][j]-p[0][j]; }
    for(int j=0;j<3;++j) c[j]=u[(j+1)%3]*v[(j+2)%3]-u[(j+2)%3]*v[(j+1)%3];
    const double area=std::sqrt(c[0]*c[0]+c[1]*c[1]+c[2]*c[2])/2;
    const double volume=(p[0][0]*c[0]+p[0][1]*c[1]+p[0][2]*c[2])/6;
    const auto face=mesh.at("triangle_faces")[i].get<std::string>();
    if(!result.contains(face)) result[face]={0.0,0.0};
    result[face][0]=result[face][0].get<double>()+area;
    result[face][1]=result[face][1].get<double>()+volume;
  }
  return result;
}
void equivalent_view(const Json& a,const Json& b) {
  equivalent(a.at("summary"),b.at("summary"));
  equivalent(a.at("topology"),b.at("topology"));
  // Mesh triangle ordering is evaluation-local. Compare oriented volume and
  // area for each exact face, plus every sampled selection edge.
  equivalent(mesh_metrics(a.at("mesh")),mesh_metrics(b.at("mesh")));
  equivalent(a.at("mesh").at("edges"),b.at("mesh").at("edges"));
}
Json drawing_request(const Json& model,const Json& recipe=Json::object(),int revision=1) {
  return {{"kind","drawing"},{"drawing",normalize_drawing(recipe,model)},
    {"identity",{{"document_id","part"},{"revision",revision},{"kernel_version",kernel_version()}}}};
}
std::size_t entries(const fs::path& root) {
  std::size_t count=0;
  if(fs::exists(root)) for(const auto& entry:fs::directory_iterator(root)) if(entry.path().extension()==".json") ++count;
  return count;
}
}
int main() {
  try {
    set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));
    Temp temp; const auto model=box(); Json cold,warm;
    const auto first=evaluate_model(temp.path,model,{{"kind","view"}},&cold);
    require(!cold.at("geometry_hit"),"First request builds geometry");
    const auto second=evaluate_model(temp.path,model,{{"kind","view"}},&warm);
    require(warm.at("geometry_hit"),"New worker restores geometry");
    equivalent_view(first,second);
    BuiltModel fresh(model);
    equivalent(fresh.topology("base"),evaluate_model(temp.path,model,{{"kind","topology"},{"feature_id","base"}},&warm).at("topology"));
    require(warm.at("geometry_hit"),"Intermediate feature uses exact cache");
    const auto initial=evaluate_model(temp.path,model,drawing_request(model),&cold);
    require(cold.at("geometry_hit") && !cold.at("projection_hit"),"First drawing restores geometry and projects");
    const auto repeated=evaluate_model(temp.path,model,drawing_request(model),&warm);
    require(warm.at("geometry_hit") && warm.at("projection_hit"),"Repeated drawing hits both caches");
    require(initial==repeated,"Warm drawing artifacts and dimensions match exactly");
    auto recipe=Json{{"title","Updated title"},{"layout","first_angle"},{"formats",{"svg"}},
      {"general_tolerances",{{"linear",0.1}}},{"dimensions",Json::array({{{"view","front"},{"kind","width"},{"manufacturing_tolerance",{{"type","symmetric"},{"value",0.2}}}}})}};
    const auto annotated=evaluate_model(temp.path,model,drawing_request(model,recipe,2),&warm);
    require(warm.at("projection_hit"),"Layout, format, annotations and revision reuse projections");
    require(annotated.at("drawing")!=initial.at("drawing"),"Fresh render changes the output");
    require(annotated.at("drawing").at("dimensions")[0].at("label")=="20 +/-0.2","New tolerance label is rendered");
    auto renamed=drawing_request(model);
    renamed["drawing"]["views"][0]["id"]="plan";
    const auto names=evaluate_model(temp.path,model,renamed,&warm);
    require(warm.at("projection_hit"),"View names do not trigger projection");
    require(names.at("drawing").dump().find("plan")!=std::string::npos,"Current view names used");
    recipe={{"hidden_lines",false}};
    evaluate_model(temp.path,model,drawing_request(model,recipe),&warm);
    require(warm.at("geometry_hit") && !warm.at("projection_hit"),"Hidden line choice invalidates projections only");
    recipe={{"views",Json::array({{{"id","cut"},{"orientation","section"},{"section",{{"axis","z"},{"offset",3}}}}})}};
    auto section=evaluate_model(temp.path,model,drawing_request(model,recipe),&cold);
    auto section_warm=evaluate_model(temp.path,model,drawing_request(model,recipe),&warm);
    require(warm.at("projection_hit") && section==section_warm,"Hatched material regions round trip");
    recipe["views"][0]["section"]["offset"]=4;
    evaluate_model(temp.path,model,drawing_request(model,recipe),&warm);
    require(!warm.at("projection_hit"),"Section plane participates in projection key");
    recipe["views"][0]["hatch"]=false;
    evaluate_model(temp.path,model,drawing_request(model,recipe),&warm);
    require(!warm.at("projection_hit"),"Hatch material extraction participates in key");
    auto changed=model; changed["parameters"]["width"]=24;
    auto changed_result=evaluate_model(temp.path,changed,drawing_request(changed),&warm);
    require(!warm.at("geometry_hit") && !warm.at("projection_hit"),"Edit invalidates geometry and projections");
    require(changed_result.at("summary").at("volume_mm3")!=first.at("summary").at("volume_mm3"),"Edit returns new geometry");
    // Checksum, key, JSON corruption and damaged B-rep all fall back to exact rebuild.
    const auto key=geometry_cache_key(model); const auto root=temp.path/".cache";
    atomic_text(root/(key+".json"),"{broken");
    evaluate_model(temp.path,model,{{"kind","topology"}},&warm);
    require(!warm.at("geometry_hit"),"Truncated cache rebuilds");
    auto envelope=parse_json(read_text(root/(key+".json"),cache_entry_bytes),cache_entry_bytes);
    envelope["sha256"]="wrong"; atomic_text(root/(key+".json"),envelope.dump());
    evaluate_model(temp.path,model,{{"kind","topology"}},&warm);
    require(!warm.at("geometry_hit"),"Checksum mismatch rebuilds");
    auto stored=read_cache(root,key).value(); stored["snapshot"]["features"]["base"]["brep"]="bad B-rep";
    stage_cache(root/(key+".json"),key,stored);
    equivalent(first.at("topology"),evaluate_model(temp.path,model,{{"kind","topology"}},&warm).at("topology"));
    require(!warm.at("geometry_hit"),"Invalid B-rep rebuilds without publishing wrong geometry");
    const auto pkey=cold.at("projection_keys").at(0).get<std::string>();
    atomic_text(root/(pkey+".json"),"broken");
    recipe["views"][0]["hatch"]=true; recipe["views"][0]["section"]["offset"]=3;
    require(evaluate_model(temp.path,model,drawing_request(model,recipe),&warm)==section,"Damaged projection rebuild is equivalent");
    require(!warm.at("projection_hit"),"Damaged projection misses");
    fs::remove_all(root);
    equivalent_view(first,evaluate_model(temp.path,model,{{"kind","view"}},&warm));
    require(!warm.at("geometry_hit"),"Cache deletion loses no geometry");
    // Exports and imports preserve exact independent outputs across cache use.
    const auto step=temp.path/"part.step";
    evaluate_model(temp.path,model,{{"kind","export"},{"format","step"},{"path",path_to_utf8(step)}},&warm);
    require(warm.at("geometry_hit") && fs::file_size(step)>1000,"STEP export restores cached geometry");
    const auto content=read_text(step);
    Json imported={{"schema_version",1},{"units","mm"},{"parameters",Json::object()},
      {"features",Json::array({{{"id","imported"},{"type","import_step"},{"content",content},{"sha256",sha256(content)}}})},{"output","imported"}};
    const auto imported_cold=evaluate_model(temp.path,imported,{{"kind","topology"}});
    equivalent(imported_cold,evaluate_model(temp.path,imported,{{"kind","topology"}},&warm));
    require(warm.at("geometry_hit"),"Embedded STEP content restores provenance");
    const auto import_key=geometry_cache_key(imported);
    imported["features"][0]["content"]=content+"\n";
    require(geometry_cache_key(imported)!=import_key,"Imported content participates in key");
    for(const auto* fixture:{"angular-plate.create.json","nozzle.create.json","m20-knob.create.json"}) {
      const auto source=parse_json(read_text(fs::path(CAD_SOURCE_DIR)/"examples"/fixture)).at("model");
      BuiltModel original(source), restored(source,original.snapshot());
      for(const auto& feature:source.at("features")) {
        const auto id=feature.at("id").get<std::string>();
        equivalent(original.summary(id),restored.summary(id));
        equivalent(original.topology(id),restored.topology(id));
      }
    }
    // Cache hits must never become persistent selection IDs or bypass revision locks.
    Service service(temp.path);
    service.call("cad_create",{{"document_id","part"},{"model",model}});
    const auto a=service.call("cad_query",{{"document_id","part"},{"revision",1},{"kind","topology"}});
    const auto b=service.call("cad_query",{{"document_id","part"},{"revision",1},{"kind","topology"}});
    require(a.at("evaluation_id")!=b.at("evaluation_id"),"Cache hits receive fresh evaluation identities");
    service.call("cad_apply",{{"document_id","part"},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","width"},{"value",24}}})}});
    try { service.call("cad_apply",{{"document_id","part"},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","width"},{"value",20}}})}}); require(false,"Stale mutation must fail"); }
    catch(const Error& e) { require(e.code=="revision_conflict","Warm geometry cannot bypass revision conflict"); }
    require(service.call("cad_read",{{"document_id","part"}}).at("revision")==2,"Rejected mutation preserves HEAD");
    {
      Temp failure;
      auto invalid=drawing_request(model,{{"views",Json::array({{{"id","cut"},{"orientation","section"},{"section",{{"axis","z"},{"offset",100}}}}})}});
      try { evaluate_model(failure.path,model,invalid); require(false,"Empty section must fail"); }
      catch(const Error& e) { require(e.code=="empty_section","Worker failure remains explicit"); }
      require(entries(failure.path/".cache")==0,"Failed worker publishes no cache");
      invalid=drawing_request(model,{{"dimensions",Json::array({{{"view","top"},{"kind","radius"},{"center",{100,100}},{"radius",2}}})}});
      try { evaluate_model(failure.path,model,invalid); require(false,"Missing dimension must fail"); }
      catch(const Error& e) { require(e.code=="drawing_reference_not_found","Renderer failure remains explicit"); }
      require(entries(failure.path/".cache")==0,"Failure after projection staging publishes no cache");
    }
    {
      Temp concurrent;
      auto run=[&]{return evaluate_model(concurrent.path,model,drawing_request(model));};
      auto one=std::async(std::launch::async,run),two=std::async(std::launch::async,run);
      require(one.get()==two.get(),"Concurrent cold workers return equivalent results");
      run(); evaluate_model(concurrent.path,model,drawing_request(model),&warm);
      require(warm.at("geometry_hit") && warm.at("projection_hit"),"Concurrent publication leaves readable entries");
    }
    {
      Temp quota; const auto cache=quota.path/".cache"; directory(cache);
      const auto staged=quota.path/"staged";
      const auto large_key=sha256("large");
      stage_cache(staged,large_key,{{"test",std::string(2*1024*1024,'x')}});
      publish_cache(cache,staged,large_key);
      require(read_cache(cache,large_key)->at("test").get_ref<const std::string&>().size()==2*1024*1024,"Cache supports native geometry larger than document JSON limit");
      for(std::size_t i=0;i<cache_max_entries+3;++i) {
        const auto k=sha256(std::to_string(i)); stage_cache(staged,k,{{"test",i}}); publish_cache(cache,staged,k);
      }
      require(entries(cache)==cache_max_entries,"Cache entry count is bounded");
      const auto oversized=cache/(sha256("oversized")+".json"); atomic_text(oversized,"x"); fs::resize_file(oversized,cache_total_bytes);
      const auto k=sha256("last"); stage_cache(staged,k,{{"test","last"}}); publish_cache(cache,staged,k);
      std::uintmax_t bytes=0; for(const auto& e:fs::directory_iterator(cache)) if(e.path().extension()==".json") bytes+=e.file_size();
      require(bytes<=cache_total_bytes,"Cache bytes bounded by eviction");
      require(read_cache(cache,k).has_value(),"Newest entry survives eviction");
      atomic_text(staged,"x"); fs::resize_file(staged,cache_entry_bytes+1);
      publish_cache(cache,staged,sha256("too-large"));
      require(!read_cache(cache,sha256("too-large")),"Oversize worker entry is not published");
      { WorkspaceLock lock(cache); publish_cache(cache,staged,k); }
      require(read_cache(cache,k).has_value(),"Busy cache is optional");
    }
    {
      Temp links,outside; std::error_code error;
      fs::create_directory_symlink(outside.path,links.path/".cache",error);
      if(!error) {
        evaluate_model(links.path,model,{{"kind","summary"}},&warm);
        require(!warm.at("geometry_hit") && fs::is_empty(outside.path),"Symlink cache ignored without external writes");
      }
    }
    {
      // Projections are cached per view: adding, removing, reordering or editing
      // one view projects only that view, and views never repeat hidden-line work.
      Temp views; Json diagnostics; const auto root=views.path/".cache";
      const Json front={{"id","front"},{"orientation","front"}},top={{"id","top"},{"orientation","top"}},
        right={{"id","right"},{"orientation","right"}};
      const auto request=[&](const Json& list,bool hidden=true) {
        return drawing_request(model,{{"views",list},{"hidden_lines",hidden}});
      };
      const auto hits=[&]{return diagnostics.at("projection_hits");};
      evaluate_model(views.path,model,request(Json::array({front,top})),&diagnostics);
      require(hits()==Json::array({false,false}) && !diagnostics.at("projection_hit"),"Cold views are both projected");
      const auto keys=diagnostics.at("projection_keys");
      require(keys.size()==2 && keys[0]!=keys[1],"Each view has its own projection key");
      require(entries(root)==3,"One geometry entry and one projection entry per view");
      const auto front_entry=read_cache(root,keys[0].get<std::string>());
      require(front_entry && front_entry->at("views").size()==1,"A projection entry holds exactly one view");
      evaluate_model(views.path,model,request(Json::array({front,top,right})),&diagnostics);
      require(hits()==Json::array({true,true,false}) && !diagnostics.at("projection_hit"),"Adding a view projects only that view");
      require(diagnostics.at("projection_keys")[0]==keys[0] && diagnostics.at("projection_keys")[1]==keys[1],"Existing views keep their keys");
      require(entries(root)==4 && read_cache(root,keys[0].get<std::string>())==front_entry,"Existing entries are reused unchanged");
      evaluate_model(views.path,model,request(Json::array({top,front})),&diagnostics);
      require(hits()==Json::array({true,true}) && diagnostics.at("projection_hit"),"Reordering views reuses their projections");
      evaluate_model(views.path,model,request(Json::array({front,right})),&diagnostics);
      require(diagnostics.at("projection_hit"),"Removing a view reuses the remaining projections");
      const Json cut={{"id","right"},{"orientation","section"},{"section",{{"axis","z"},{"offset",3}}}};
      evaluate_model(views.path,model,request(Json::array({front,top,cut})),&diagnostics);
      require(hits()==Json::array({true,true,false}),"Editing one view invalidates only that view");
      evaluate_model(views.path,model,request(Json::array({front,top,cut}),false),&diagnostics);
      require(hits()==Json::array({false,false,false}),"Hidden-line choice invalidates every view");
      // A drawing assembled from independently cached views equals a cold one.
      Temp cold_workspace;
      const auto request_three=request(Json::array({front,top,right}));
      const auto cold_three=evaluate_model(cold_workspace.path,model,request_three,&diagnostics);
      require(hits()==Json::array({false,false,false}),"A fresh workspace projects every view");
      const auto assembled=evaluate_model(views.path,model,request_three,&diagnostics);
      require(diagnostics.at("projection_hit") && assembled==cold_three,"Per-view cache entries reproduce a cold drawing exactly");
    }
    {
      // Aggregate drawing limits hold however each view was obtained.
      const auto line=Json{{"kind","line"},{"hidden",false},{"points",{{0,0},{1,1}}}};
      const auto view_with=[&](std::size_t count) {
        Json entities=Json::array(); for(std::size_t i=0;i<count;++i) entities.push_back(line);
        return Json{{"id","v"},{"entities",entities}};
      };
      const Json requested=Json::array({Json{{"id","a"}},Json{{"id","b"}}});
      check_drawing_totals(requested,Json::array({view_with(5000),view_with(4999)}),"out");
      const auto exceeded=[&](const Json& projected,const Json& asked) {
        try { check_drawing_totals(asked,projected,"out"); } catch(const Error& e) { return e.code=="limit_exceeded"; }
        return false;
      };
      require(exceeded(Json::array({view_with(5000),view_with(5001)}),requested),"Views that individually fit cannot exceed the entity limit together");
      Json polyline_entities=Json::array();
      for(int i=0;i<3;++i) polyline_entities.push_back({{"kind","polyline"},{"hidden",false},{"points",Json(std::vector<Json>(70000,Json::array({0,0})))}});
      require(exceeded(Json::array({Json{{"id","p"},{"entities",polyline_entities}}}),Json::array({Json{{"id","p"}}})),"Total polyline points are bounded");
      const auto anchors=[&](int count) { Json list=Json::array(); for(int i=0;i<count;++i) list.push_back({{"part_id","p"},{"point",{0,0,0}}}); return list; };
      check_drawing_totals(Json::array({Json{{"id","a"},{"balloon_anchors",anchors(32)}},Json{{"id","b"},{"balloon_anchors",anchors(32)}}}),Json::array(),"out");
      require(exceeded(Json::array(),Json::array({Json{{"id","a"},{"balloon_anchors",anchors(32)}},Json{{"id","b"},{"balloon_anchors",anchors(33)}}})),"Balloon anchors stay bounded across views");
    }
    std::cout<<checks<<" geometry/projection cache checks passed\n"; return 0;
  } catch(const std::exception& e) { std::cerr<<"FAILED: "<<e.what()<<'\n'; return 1; }
}
