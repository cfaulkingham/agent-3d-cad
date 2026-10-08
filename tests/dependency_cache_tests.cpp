#include "agentcad/cache.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/drawing.hpp"
#include "agentcad/service.hpp"
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <cmath>
#include <functional>
#include <iostream>
#include <set>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
struct Temp {
  fs::path root;
  Temp(){root=temporary_file(fs::temp_directory_path());fs::remove(root);directory(root);}
  ~Temp(){std::error_code ignored;fs::remove_all(root,ignored);}
};
void equivalent(const Json& left,const Json& right) {
  if(left.is_number()&&right.is_number()) {
    require(std::abs(left.get<double>()-right.get<double>())<1e-7*std::max(1.0,std::abs(left.get<double>())),"Cached geometry measurement differs");
  }else if(left.is_object()||left.is_array()) {
    require(left.type()==right.type()&&left.size()==right.size(),"Cached geometry structure differs");
    if(left.is_object())for(const auto& item:left.items())equivalent(item.value(),right.at(item.key()));
    else for(std::size_t i=0;i<left.size();++i)equivalent(left[i],right[i]);
  }else require(left==right,"Cached geometry identity/provenance differs: "+left.dump()+" / "+right.dump());
}
Json model() {
  return parse_json(R"({"schema_version":1,"units":"mm","parameters":{"width":24,"thickness":6,"radius":2,"angle":30,"spare_height":3,"unused":111},"features":[
    {"id":"profile","type":"sketch","workplane":{"origin":[0,0,0],"normal":[0,0,1],"x_direction":[1,0,0]},"profile":{"type":"rectangle","width":{"parameter":"width"},"height":12}},
    {"id":"prism","type":"extrude","input":"profile","distance":{"parameter":"thickness"}},
    {"id":"bevel","type":"chamfer","input":"prism","distance":0.5,"edges":"all"},
    {"id":"bore","type":"hole","input":"bevel","origin":[5,5,-1],"axis":[0,0,1],"radius":{"parameter":"radius"},"depth":12},
    {"id":"bolt","type":"cylinder","radius":1.5,"height":8},
    {"id":"spare","type":"cylinder","radius":1,"height":{"parameter":"spare_height"}},
    {"id":"module","type":"assembly","parts":[{"id":"body","input":"bore"},{"id":"pin","input":"bolt"}],"mates":[
      {"id":"hinge","type":"revolute","parent":"body","child":"pin","angle_deg":{"parameter":"angle"},"angle_limits_deg":[0,180],
       "parent_frame":{"origin":[5,5,{"parameter":"thickness"}],"normal":[0,0,1],"x_direction":[1,0,0]},
       "child_frame":{"origin":[0,0,0],"normal":[0,0,1],"x_direction":[1,0,0]}}],
     "poses":[{"id":"flat","values":[{"mate_id":"hinge","coordinate":"angle_deg","value":0}]}],"bom":[{"input":"bore","part_number":"BODY"}]},
    {"id":"fixture","type":"assembly","parts":[{"id":"left","input":"module"},{"id":"right","input":"module","placement":{"translation":[60,10,0],"rotation":{"origin":[0,0,0],"axis":[0,0,1],"angle_deg":-25}}},{"id":"extra","input":"bolt","placement":{"translation":[100,0,0]}}]}],"output":"fixture"})");
}
void hits(const Json& diagnostics,const Json& current,const std::set<std::string>& rebuilt) {
  require(diagnostics.at("feature_hits").size()==current.at("features").size(),"Every evaluated dependency has cache evidence");
  for(const auto& feature:current.at("features")) {
    const auto id=text_field(feature,"id");
    require(diagnostics.at("feature_hits").at(id).get<bool>()==!rebuilt.contains(id),"Unexpected cache decision for "+id+": "+diagnostics.at("feature_hits").dump());
  }
}
std::set<std::string> all(const Json& current) {
  std::set<std::string> names;for(const auto& feature:current.at("features"))names.insert(text_field(feature,"id"));return names;
}
std::map<std::string,std::string> files(const fs::path& root) {
  std::map<std::string,std::string> hashes;
  for(const auto& entry:fs::directory_iterator(root))if(entry.path().extension()==".json")hashes[path_to_utf8(entry.path().filename())]=sha256(read_text(entry.path(),cache_entry_bytes));
  return hashes;
}
void incremental() {
  Temp temp;auto current=model();Json diagnostics;
  const auto run=[&](const std::set<std::string>& rebuilt) {
    const auto evaluated=evaluate_model(temp.root,current,{{"kind","view"}},&diagnostics);
    hits(diagnostics,current,rebuilt);BuiltModel cold(current);
    equivalent(evaluated.at("summary"),cold.summary());equivalent(evaluated.at("topology"),cold.topology());
    std::set<std::string> owners;
    for(const auto& face:evaluated.at("topology").at("faces"))owners.insert(text_field(face,"part_id"));
    require(owners==std::set<std::string>({"left/body","left/pin","right/body","right/pin","extra"}),"Nested cached ownership retains every independent occurrence");
    return evaluated;
  };
  run(all(current));run({});
  current["parameters"]["radius"]=2.5;run({"bore","module","fixture"});
  current["parameters"]["thickness"]=7;run({"prism","bevel","bore","module","fixture"});
  current["parameters"]["width"]=30;run({"profile","prism","bevel","bore","module","fixture"});
  current["parameters"]["angle"]=75;run({"module","fixture"});
  current["features"][7]["parts"][1]["placement"]["translation"][0]=70;run({"fixture"});
  const auto geometry_key=geometry_cache_key(current);
  current["parameters"]["unused"]=222;
  current["features"][6]["bom"][0]["part_number"]="REVISED-BODY";
  current["features"][6]["poses"].push_back({{"id","upright"},{"values",Json::array({{{"mate_id","hinge"},{"coordinate","angle_deg"},{"value",90}}})}});
  const auto updated=run({});
  require(diagnostics.at("geometry_hit")&&geometry_cache_key(current)==geometry_key,"Unused parameters, BOM and named poses retain exact geometry");
  require(updated.at("summary").at("assembly").at("mechanisms")[0].at("motion").at("poses")==Json({"flat","upright"}),"Whole-model hits report current named poses");
  auto invalid=current;invalid["features"][6]["poses"][0]["values"][0]["value"]=200;
  try{evaluate_model(temp.root,invalid,{{"kind","summary"}});require(false,"Invalid named pose must fail on a geometry hit");}
  catch(const Error& error){require(error.code=="invalid_model","Every named pose is validated even when geometry is unchanged");}
  // Drawing projections depend on their output closure, not a hidden branch.
  const auto drawing=[&]{return Json{{"kind","drawing"},{"drawing",normalize_drawing({{"formats",{"svg"}},{"views",Json::array({{{"id","top"},{"orientation","top"}}})}},current)},
    {"identity",{{"document_id","fixture"},{"revision",1},{"kernel_version",kernel_version()}}}};};
  const auto before=evaluate_model(temp.root,current,drawing(),&diagnostics);
  current["parameters"]["spare_height"]=4;
  const auto after=evaluate_model(temp.root,current,drawing(),&diagnostics);
  hits(diagnostics,current,{"spare"});require(diagnostics.at("projection_hit")&&before==after,"A changed unreachable branch is validated without reprojecting unchanged output");
  // Removing a model entry forces the partial-cache path. A damaged feature
  // alone rebuilds; its unchanged descendants can still restore their results.
  const auto root=temp.root/".cache";
  const auto damage=[&](const std::string& id,const std::function<void(Json&)>& mutate) {
    fs::remove(root/(geometry_cache_key(current)+".json"));
    const auto key=feature_cache_keys(current).at(id).get<std::string>();
    auto entry=read_cache(root,key).value();mutate(entry);stage_cache(root/(key+".json"),key,entry);
    run({id});require(!diagnostics.at("geometry_hit"),"Invalid model snapshots fall back to dependency caches");
  };
  damage("bevel",[](Json& entry){entry["snapshot"]["brep"]="invalid B-rep";});
  damage("module",[](Json& entry){entry["snapshot"]["parts"][0]["transform"][3]=999;});
  damage("bore",[](Json& entry){entry["snapshot"]["provenance"]["feature_id"]="wrong";});
  fs::remove(root/(geometry_cache_key(current)+".json"));
  atomic_text(root/(feature_cache_keys(current).at("profile").get<std::string>()+".json"),"{truncated");run({"profile"});
  const auto intact=files(root);
  auto failed=current;failed["parameters"]["spare_height"]=5;
  failed["features"].push_back({{"id","bad_radius"},{"type","fillet"},{"input","prism"},{"radius",1000},{"edges","all"}});
  try{evaluate_model(temp.root,failed,{{"kind","summary"}});require(false,"Failed feature must not publish earlier staged entries");}
  catch(const Error& error){require(error.code=="kernel_failure"&&error.details.at("feature_id")=="bad_radius","The failing native feature remains explicit");}
  require(files(root)==intact,"Failed candidates publish no feature/model cache entries");
  const auto step=temp.root/"cached.step";
  evaluate_model(temp.root,current,{{"kind","export"},{"format","step"},{"path",path_to_utf8(step)}},&diagnostics);
  STEPControl_Reader reader;reader.SetShapeProcessFlags(ShapeProcess::OperationsFlags{});
  require(reader.ReadFile(path_to_utf8(step).c_str())==IFSelect_RetDone&&reader.TransferRoots()==reader.NbRootsForTransfer(),"Cached export transfers every independent STEP root");
  const auto shape=reader.OneShape();require(BRepCheck_Analyzer(shape).IsValid(),"Independent STEP readback is valid");
  GProp_GProps volume;BRepGProp::VolumeProperties(shape,volume);
  equivalent(volume.Mass(),BuiltModel(current).summary().at("volume_mm3"));
  NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> solids;TopExp::MapShapes(shape,TopAbs_SOLID,solids);
  require(solids.Extent()==5,"Cached STEP retains all repeated exact solids");
  fs::remove_all(root);run(all(current));
}
void component_metadata() {
  Temp temp;Service service(temp.root);auto source=model();
  service.call("cad_create",{{"document_id","library"},{"model",source}});
  const auto seed=parse_json(R"({"schema_version":1,"units":"mm","parameters":{"angle":45},"features":[{"id":"seed","type":"box","size":[1,1,1]}],"output":"seed"})");
  service.call("cad_create",{{"document_id","consumer"},{"model",seed}});
  auto capture=Json{{"op","set_component"},{"id","imported"},{"source_document_id","library"},{"source_revision",1},{"source_feature_id","module"},
    {"bindings",{{"angle",{{"parameter","angle"}}}}}};
  const auto initial=service.call("cad_apply",{{"document_id","consumer"},{"expected_revision",1},{"operations",Json::array({capture,{{"op","set_output"},{"feature_id","imported"}}})}});
  service.call("cad_apply",{{"document_id","library"},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","spare_height"},{"value",9}}})}});
  capture["source_revision"]=2;
  const auto updated=service.call("cad_apply",{{"document_id","consumer"},{"expected_revision",2},{"operations",Json::array({capture})}});
  require(geometry_cache_key(initial.at("model"))==geometry_cache_key(updated.at("model")),"Unrelated library edits preserve captured geometry fingerprints");
  require(updated.at("summary").at("components")[0].at("source").at("revision")==2,"A whole-model geometry hit reports the new component pin");
  require(service.call("cad_read",{{"document_id","consumer"},{"revision",2}}).at("model")==initial.at("model"),"Cached source updates preserve consumer history");
  auto changed=updated.at("model");changed["parameters"]["angle"]=90;Json diagnostics;
  const auto result=evaluate_model(temp.root,changed,{{"kind","view"}},&diagnostics);
  hits(diagnostics,changed,{"imported"});
  require(result.at("summary").at("assembly").at("motion").at("dofs")[0].at("value")==90,"Only the bound component mechanism changes for a pose parameter edit");
  require(!result.at("summary").at("components")[0].at("modified").get<bool>(),"Bound parameter edits preserve source tracking");
}
}
int main(){try{configure_kernel_logging();set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));incremental();component_metadata();std::cout<<checks<<" dependency cache checks passed\n";return 0;}
catch(const std::exception& error){std::cerr<<"FAILED: "<<error.what()<<'\n';return 1;}}
