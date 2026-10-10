#include "artifact_internal.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <cmath>
#include <memory>

namespace agentcad {
using namespace artifact_detail;
namespace {
constexpr std::size_t review_limit=64*1024*1024;
Json object(Json properties,Json required){return {{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};}
void digest(const std::string&hash){if(hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos)throw Error("invalid_argument","Expected SHA-256 must be 64 lowercase hex digits");}
void source_association(const Json&value){fields(value,{"document_id","revision","feature_id"});identifier(text_field(value,"document_id"));revision_number(value.at("revision"));model_identifier(text_field(value,"feature_id"));}
void absolute_file(const fs::path&path){require(path.is_absolute()&&fs::is_regular_file(path)&&!fs::is_symlink(fs::symlink_status(path)),"Artifact input must be an absolute regular non-symlink file");
  for(auto p=path.parent_path();!p.empty()&&p!=p.root_path();p=p.parent_path())require(!fs::is_symlink(fs::symlink_status(p)),"Artifact input parent must not be a symlink");}
std::string capture(const fs::path&path,const std::string&hash,std::size_t limit=artifact_input_limit){absolute_file(path);check_job_cancelled();auto raw=read_text(path,limit);if(sha256(raw)!=hash)throw Error("artifact_mismatch","Artifact differs from its expected SHA-256");return raw;}
std::string extension(const fs::path&path){auto result=path_to_utf8(path.extension());for(auto&c:result)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return result;}
Json identity_for(const Json&args,const std::string&raw,const std::string&build){
  require(!build.empty()&&build.size()<=256&&!invalid_utf8_offset(build)&&std::none_of(build.begin(),build.end(),[](unsigned char c){return c<32||c==127;}),"Explicit native build identity required");
  Json source={{"format",args.at("format")},{"sha256",sha256(raw)},{"bytes",raw.size()},{"declared_units",args.at("units")},{"path","original"+extension(path_from_utf8(text_field(args,"path")))},{"native_build",build},{"references",Json::array()}};
  source["native_source_association"]=args.contains("native_source")?Json{{"qualification","caller_declared"},{"reference",args.at("native_source")},{"authoritative_editable_source","native_document"}}:Json(nullptr);return source;
}
Parsed step(const fs::path&workspace,const std::string&raw){
  Json model={{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",Json::array({{{"id","Imported"},{"type","import_step"},{"content",raw},{"sha256",sha256(raw)}}})},{"output","Imported"}};
  const auto view=evaluate_model(workspace,model,{{"kind","view"},{"feature_id","Imported"}});const auto&mesh=view.at("mesh");std::vector<Vec> v;std::vector<std::array<std::size_t,3>> t;
  for(const auto&p:mesh.at("positions"))v.push_back({p[0],p[1],p[2]});for(const auto&p:mesh.at("triangles"))t.push_back({p[0],p[1],p[2]});Parsed parsed;parsed.representation="exact_brep_import";parsed.geometry.mesh(v,t,identity(),"step/imported_shape");
  for(const auto&e:mesh.at("edges")){std::vector<Vec> points;for(const auto&p:e.at("points"))points.push_back({p[0],p[1],p[2]});if(points.size()>=2)parsed.geometry.line(points,"step/derived_curve/"+std::to_string(parsed.geometry.polylines.size()+1));}
  parsed.metadata={{"kernel_version","8.0.1"},{"exact_summary",view.at("summary")},{"import_history","opaque_source_only"}};parsed.limitations.push_back("STEP is imported into an ephemeral exact OCCT model for review. Tessellated display groups and curves are local to the artifact hash; no original face/edge selectors or recovered editable feature history are provided.");return parsed;
}
Parsed parse_artifact(const fs::path& workspace,const Json& source,const std::string& raw,const References& refs) {
  const auto format=text_field(source,"format");if(format=="step")return step(workspace,raw);
  if(format=="stl")return stl(raw,unit_scale(text_field(source,"declared_units")));
  if(format=="glb")return glb(raw);if(format=="dxf")return dxf(raw,text_field(source,"declared_units"));
  if(format=="3mf")return three_mf(raw);return robot(raw,format,refs);
}
std::string native_record(Store& store,const Json& reference) {
  source_association(reference);const auto record=store.read(text_field(reference,"document_id"),revision_number(reference.at("revision")));
  bool found=false;for(const auto& feature:record.at("model").at("features"))if(feature.at("id")==reference.at("feature_id"))found=true;
  if(!found)throw Error("artifact_source_missing","Declared native source feature is absent from the historical revision");
  return sha256(record.dump());
}
void persisted_text(const std::string& value,std::size_t limit) {
  require(!value.empty()&&value.size()<=limit&&!invalid_utf8_offset(value)&&std::none_of(value.begin(),value.end(),[](unsigned char c){return c<32||c==127;}),"Invalid persisted artifact text");
}
void file_identity(const Json& file,bool reference=false) {
  if(reference)fields(file,{"uri","path","sha256","bytes","units"});
  require(file.at("bytes").is_number_unsigned()||(file.at("bytes").is_number_integer()&&file.at("bytes")>=0),"Invalid persisted artifact byte count");
  if(reference||file.value("format",Json())!="step")require(file.at("bytes")<=artifact_input_limit,"Persisted artifact byte count exceeds limit");digest(text_field(file,"sha256"));relative_uri(text_field(file,"path"));
  if(reference){relative_uri(text_field(file,"uri"));const auto units=text_field(file,"units");require(std::set<std::string>{"mm","cm","m","in","ft","um"}.contains(units),"Captured reference units must be canonical explicit units");unit_scale(units);}
}
void review_contract(const Json& review) {
  fields(review,{"schema_version","source","summary","geometry","metadata","limitations","read_only","editable_history_recovered","native_selection_references","selection_lifetime"});
  require(review.at("schema_version")==1&&review.at("read_only")==true&&review.at("editable_history_recovered")==false&&review.at("native_selection_references")==false&&review.at("selection_lifetime")=="review_sha256","Unsupported persisted review qualification");
  const auto& source=review.at("source");fields(source,{"format","sha256","bytes","declared_units","path","native_build","references","native_source_association"});file_identity(source);persisted_text(text_field(source,"native_build"),256);
  Json args={{"action","review"},{"path",path_to_utf8(fs::temp_directory_path()/path_from_utf8(text_field(source,"path")))},{"format",source.at("format")},{"units",source.at("declared_units")},{"expected_sha256",source.at("sha256")}};validate_artifact_review_arguments(args);
  require(source.at("path")=="original"+extension(path_from_utf8(text_field(source,"path"))),"Original source path is not canonical");
  const auto& refs=source.at("references");require(refs.is_array()&&refs.size()<=64,"Invalid captured reference list");std::size_t total=0;std::set<std::string> uris,paths;
  for(const auto& ref:refs){file_identity(ref,true);const auto path=text_field(ref,"path");require(path.starts_with("references/")&&paths.insert(path).second&&uris.insert(text_field(ref,"uri")).second,"Invalid captured reference path or duplicate URI");const auto bytes=ref.at("bytes").get<std::size_t>();require(bytes<=artifact_expansion_limit-total,"Captured reference budget exceeded");total+=bytes;}
  require(refs.empty()||source.at("format")=="urdf"||source.at("format")=="sdf","Unexpected references for source format");
  const auto& association=source.at("native_source_association");if(!association.is_null()){fields(association,{"qualification","reference","authoritative_editable_source","source_record_sha256"});require(association.at("qualification")=="caller_declared"&&association.at("authoritative_editable_source")=="native_document","Invalid source association qualification");source_association(association.at("reference"));digest(text_field(association,"source_record_sha256"));}
  const auto& g=review.at("geometry");fields(g,{"positions","triangles","triangle_groups","groups","polylines"});
  for(const auto* key:{"positions","triangles","triangle_groups","groups","polylines"})require(g.at(key).is_array(),"Geometry collections must be arrays");
  require(g.at("positions").size()<=artifact_geometry_limit&&g.at("triangles").size()<=artifact_geometry_limit&&g.at("groups").size()<=10000&&g.at("polylines").size()<=10000,"Persisted geometry budget exceeded");
  const auto point=[](const Json& p){require(p.is_array()&&p.size()==3,"Invalid persisted point");for(const auto& x:p){require(x.is_number(),"Invalid point coordinate");finite(x.get<double>());}};
  for(const auto& p:g.at("positions"))point(p);
  std::set<std::string> groups,covered;std::size_t ordinal=0;
  for(const auto& group:g.at("groups")){require(group.is_object()&&group.at("editable")==false&&group.at("id")=="artifact-"+std::to_string(++ordinal),"Invalid read-only mesh label");persisted_text(text_field(group,"source_ref"),4096);groups.insert(text_field(group,"id"));}
  require(g.at("triangle_groups").size()==g.at("triangles").size(),"Invalid triangle group mapping");
  for(std::size_t n=0;n<g.at("triangles").size();++n){if(n%4096==0)check_job_cancelled();const auto& t=g.at("triangles")[n];require(t.is_array()&&t.size()==3,"Invalid triangle tuple");for(const auto& i:t)require((i.is_number_unsigned()||(i.is_number_integer()&&i>=0))&&i<g.at("positions").size(),"Triangle exceeds vertex buffer");require(t[0]!=t[1]&&t[1]!=t[2]&&t[0]!=t[2],"Triangle repeats an index");const auto label=g.at("triangle_groups")[n].get<std::string>();require(groups.contains(label),"Triangle uses an unknown group");covered.insert(label);}
  require(covered==groups,"Mesh labels are not completely covered");std::size_t curve_points=0;ordinal=0;
  for(const auto& curve:g.at("polylines")){require(curve.is_object()&&curve.at("editable")==false&&curve.at("id")=="curve-"+std::to_string(++ordinal),"Invalid read-only curve label");persisted_text(text_field(curve,"source_ref"),4096);const auto& points=curve.at("points");require(points.is_array()&&points.size()>=2&&points.size()<=artifact_geometry_limit-curve_points,"Invalid persisted curve points");curve_points+=points.size();for(const auto& p:points)point(p);}
  require(review.at("metadata").is_object()&&review.at("limitations").is_array()&&review.at("limitations").size()<=128,"Invalid review metadata or limitations");for(const auto& limit:review.at("limitations"))persisted_text(limit.get<std::string>(),16384);
  Geometry computed;computed.positions=g.at("positions");computed.triangles=g.at("triangles");computed.groups=g.at("groups");computed.polylines=g.at("polylines");
  const auto representation=text_field(review.at("summary"),"representation");const std::map<std::string,std::string> representations={{"step","exact_brep_import"},{"stl","triangle_mesh"},{"glb","triangle_mesh"},{"3mf","triangle_mesh"},{"dxf","drawing_curves"},{"urdf","robot_description"},{"sdf","robot_description"},{"srdf","robot_semantics"}};
  require(representation==representations.at(text_field(source,"format"))&&computed.summary(representation)==review.at("summary"),"Persisted summary does not qualify its geometry/source format");
}
bool equivalent(const Json& a,const Json& b,bool tolerant) {
  if(a.is_number()&&b.is_number()){const double x=a.get<double>(),y=b.get<double>();return std::isfinite(x)&&std::isfinite(y)&&(tolerant?std::abs(x-y)<=1e-8+1e-7*std::max(std::abs(x),std::abs(y)):a==b);}
  if(a.type()!=b.type()||a.size()!=b.size())return false;
  if(a.is_array()){for(std::size_t i=0;i<a.size();++i)if(!equivalent(a[i],b[i],tolerant))return false;return true;}
  if(a.is_object()){for(const auto& [key,value]:a.items())if(!b.contains(key)||!equivalent(value,b.at(key),tolerant))return false;return true;}return a==b;
}
struct Stage {fs::path path;explicit Stage(const fs::path&root){directory(root);require(!fs::is_symlink(fs::symlink_status(root)),"Artifact package parent is a symlink");path=temporary_directory(root);}~Stage(){std::error_code ignored;fs::remove_all(path,ignored);}};
Json ledger(const fs::path&root,const std::vector<std::string>&paths,const Json&source){Json entries=Json::array();std::size_t total=0;for(const auto&name:paths){check_job_cancelled();const auto path=root/path_from_utf8(relative_uri(name));absolute_file(path);const auto size=fs::file_size(path);const bool step_source=source.at("format")=="step"&&source.at("path")==name;if(!step_source){require(size<=256*1024*1024-total,"Review package budget exceeded");total+=size;}entries.push_back({{"path",name},{"bytes",size},{"sha256",sha256_file(path,step_source?unlimited_bytes:review_limit,check_job_cancelled)}});}return entries;}
Json result(const fs::path&root,const std::string&raw,const Json&review){return {{"action","review"},{"directory",path_to_utf8(root)},{"path",path_to_utf8(root/"review.json")},{"sha256",sha256(raw)},{"source",review.at("source")},{"summary",review.at("summary")},{"read_only",true},{"editable_history_recovered",false},{"selection_lifetime","review_sha256"},{"native_selection_references",false}};}
}
Json artifact_review_definitions(){
  const Json text={{"type","string"}},hash={{"type","string"},{"pattern","^[0-9a-f]{64}$"}},units={{"enum",{"file","mm","cm","m","in","ft","um"}}};
  const Json id={{"type","string"},{"pattern","^[A-Za-z][A-Za-z0-9_-]{0,63}$"}};
  const Json native=object({{"document_id",id},{"revision",{{"type","integer"},{"minimum",1},{"maximum",9007199254740991ULL}}},{"feature_id",id}},{"document_id","revision","feature_id"});
  const Json reference=object({{"uri",{{"type","string"},{"minLength",1},{"maxLength",240}}},{"path",text},{"expected_sha256",hash},{"units",{{"enum",{"mm","cm","m","in","ft","um"}}}}},{"uri","path","expected_sha256","units"});
  Json arguments=object({{"action",{{"const","review"}}},{"path",text},{"expected_sha256",hash},{"format",{{"enum",{"step","stl","3mf","glb","dxf","urdf","sdf","srdf"}}}},{"units",units},{"references",{{"type","array"},{"items",reference},{"maxItems",64}}},{"native_source",native}},{"action","path","expected_sha256","format","units"});
  arguments["allOf"]=Json::array({
    {{"if",{{"properties",{{"format",{{"enum",{"step","3mf"}}}}}}}},{"then",{{"properties",{{"units",{{"const","file"}}}}}}}},
    {{"if",{{"properties",{{"format",{{"enum",{"glb","urdf","sdf","srdf"}}}}}}}},{"then",{{"properties",{{"units",{{"const","m"}}}}}}}},
    {{"if",{{"properties",{{"format",{{"enum",{"stl","dxf"}}}}}}}},{"then",{{"properties",{{"units",{{"enum",{"mm","cm","m","in","ft","um"}}}}}}}}}
  });
  const Json file=object({{"path",text},{"bytes",{{"type","integer"},{"minimum",0},{"maximum",artifact_input_limit}}},{"sha256",hash}},{"path","bytes","sha256"});
  const Json association={{"oneOf",Json::array({{{"type","null"}},object({{"qualification",{{"const","caller_declared"}}},{"reference",native},{"authoritative_editable_source",{{"const","native_document"}}},{"source_record_sha256",hash}},{"qualification","reference","authoritative_editable_source","source_record_sha256"})})}};
  Json ref_source=file;ref_source["properties"]["uri"]=text;ref_source["properties"]["units"]=reference.at("properties").at("units");ref_source["required"].push_back("uri");ref_source["required"].push_back("units");
  Json source=file;source["properties"]["bytes"].erase("maximum");
  source["allOf"]=Json::array({{{"if",{{"properties",{{"format",{{"not",{{"const","step"}}}}}}}}},{"then",{{"properties",{{"bytes",{{"maximum",artifact_input_limit}}}}}}}}});
  source["properties"]["format"]=arguments["properties"]["format"];source["properties"]["declared_units"]=units;source["properties"]["native_build"]=text;source["properties"]["references"]={{"type","array"},{"items",ref_source},{"maxItems",64}};source["properties"]["native_source_association"]=association;for(const auto*key:{"format","declared_units","native_build","references","native_source_association"})source["required"].push_back(key);
  const Json xyz={{"type","array"},{"items",{{"type","number"},{"minimum",-1e9},{"maximum",1e9}}},{"minItems",3},{"maxItems",3}};
  const Json bounds={{"oneOf",Json::array({{{"type","null"}},object({{"min",xyz},{"max",xyz}},{"min","max"})})}};
  const Json count={{"type","integer"},{"minimum",0},{"maximum",artifact_geometry_limit}};
  const Json summary=object({{"representation",{{"enum",{"exact_brep_import","triangle_mesh","drawing_curves","robot_description","robot_semantics"}}}},{"units",{{"const","mm"}}},{"vertices",count},{"triangles",count},{"curves",count},{"groups",count},{"bounds",bounds},{"editable",{{"const",false}}}},{"representation","units","vertices","triangles","curves","groups","bounds","editable"});
  const Json response=object({{"action",{{"enum",{"review","verify"}}}},{"directory",text},{"path",text},{"sha256",hash},{"source",source},{"summary",summary},{"read_only",{{"const",true}}},{"editable_history_recovered",{{"const",false}}},{"selection_lifetime",{{"const","review_sha256"}}},{"native_selection_references",{{"const",false}}}},{"action","directory","path","sha256","source","summary","read_only","editable_history_recovered","selection_lifetime","native_selection_references"});
  return {{"artifact_review_arguments",arguments},{"artifact_review_source",source},{"artifact_review_summary",summary},{"artifact_review_result",response}};
}
void validate_artifact_review_arguments(const Json&args){
  fields(args,{"action","path","expected_sha256","format","units"},{"references","native_source"});if(args.at("action")!="review")throw Error("invalid_argument","External artifact action must be read-only review");
  if(!path_from_utf8(text_field(args,"path")).is_absolute())throw Error("invalid_argument","Artifact paths must be absolute");digest(text_field(args,"expected_sha256"));const auto format=text_field(args,"format"),units=text_field(args,"units");
  const std::map<std::string,std::set<std::string>> suffix={{"step",{".step",".stp"}},{"stl",{".stl"}},{"3mf",{".3mf"}},{"glb",{".glb"}},{"dxf",{".dxf"}},{"urdf",{".urdf"}},{"sdf",{".sdf"}},{"srdf",{".srdf"}}};
  if(!suffix.contains(format)||!suffix.at(format).contains(extension(path_from_utf8(text_field(args,"path")))))throw Error("invalid_argument","Explicit artifact format differs from filename extension");
  if(format=="step"||format=="3mf"){if(units!="file")throw Error("invalid_argument","STEP and 3MF review require units=file");}
  else if(format=="glb"||format=="urdf"||format=="sdf"||format=="srdf"){if(units!="m")throw Error("invalid_argument","GLB and robot descriptions require explicit intrinsic meter units");}
  else {if(!std::set<std::string>{"mm","cm","m","in","ft","um"}.contains(units))throw Error("invalid_argument","Artifact units must be mm/cm/m/in/ft/um");unit_scale(units);}
  if(args.contains("native_source"))source_association(args.at("native_source"));if(args.contains("references")){const auto&refs=args.at("references");require(refs.is_array()&&refs.size()<=64,"Artifact reference budget exceeded");if(format!="urdf"&&format!="sdf"&&!refs.empty())unsupported("Explicit file references currently belong to URDF/SDF mesh review");std::set<std::string> uris;
    for(const auto&r:refs){fields(r,{"uri","path","expected_sha256","units"});require(uris.insert(relative_uri(text_field(r,"uri"))).second,"Duplicate explicit artifact reference");require(path_from_utf8(text_field(r,"path")).is_absolute(),"Reference paths must be absolute");digest(text_field(r,"expected_sha256"));const auto ref_units=text_field(r,"units");if(!std::set<std::string>{"mm","cm","m","in","ft","um"}.contains(ref_units))throw Error("invalid_argument","Reference units must be mm/cm/m/in/ft/um");unit_scale(ref_units);}}
}
Json review_external_artifact(const fs::path&workspace,const Json&args,const std::string&build){
  validate_artifact_review_arguments(args);const auto path=path_from_utf8(text_field(args,"path"));const auto raw=capture(path,text_field(args,"expected_sha256"),args.at("format")=="step"?unlimited_bytes:artifact_input_limit);auto source=identity_for(args,raw,build);Store store(workspace);
  if(args.contains("native_source")){DocumentLock lock(workspace,text_field(args.at("native_source"),"document_id"),LockWait::publication);source["native_source_association"]["source_record_sha256"]=native_record(store,args.at("native_source"));}
  References refs;std::size_t reference_bytes=0;
  if(args.contains("references"))for(const auto&r:args.at("references")){const auto uri=text_field(r,"uri");const auto refpath=path_from_utf8(text_field(r,"path"));require(refpath.lexically_normal()==(path.parent_path()/path_from_utf8(uri)).lexically_normal(),"Reference path does not match the source-contained URI");auto bytes=capture(refpath,text_field(r,"expected_sha256"));require(bytes.size()<=artifact_expansion_limit-reference_bytes,"Reference bytes exceed 128 MiB");reference_bytes+=bytes.size();refs.emplace(uri,Reference{uri,bytes,sha256(bytes),text_field(r,"units"),refpath});}
  Parsed parsed;try{parsed=parse_artifact(workspace,source,raw,refs);}catch(const Json::exception&){throw Error("artifact_invalid","Artifact fields are malformed or incomplete");}
  const auto parent=workspace/"artifact_reviews";Stage stage(parent);std::vector<std::string> files={text_field(source,"path")};atomic_text(stage.path/path_from_utf8(files.front()),raw,source.at("format")=="step"?unlimited_bytes:artifact_input_limit);
  unsigned ordinal=0;for(const auto&[uri,r]:refs){const auto name="references/"+std::to_string(++ordinal)+extension(r.path);directory(stage.path/"references");atomic_text(stage.path/path_from_utf8(name),r.bytes,artifact_input_limit);files.push_back(name);source["references"].push_back({{"uri",uri},{"path",name},{"sha256",r.hash},{"bytes",r.bytes.size()},{"units",r.units}});}
  Json review={{"schema_version",1},{"source",source},{"summary",parsed.geometry.summary(parsed.representation)},{"geometry",parsed.geometry.result()},{"metadata",parsed.metadata},{"limitations",parsed.limitations},{"read_only",true},{"editable_history_recovered",false},{"native_selection_references",false},{"selection_lifetime","review_sha256"}};
  review_contract(review);const auto review_raw=review.dump(2);require(review_raw.size()<=review_limit,"Review JSON exceeds 64 MiB");atomic_text(stage.path/"review.json",review_raw,review_limit);files.push_back("review.json");const auto manifest=Json{{"schema_version",1},{"source_sha256",source.at("sha256")},{"review_sha256",sha256(review_raw)},{"artifacts",ledger(stage.path,files,source)}};
  atomic_text(stage.path/"manifest.json",manifest.dump(2));check_job_cancelled();const auto target=parent/sha256(review_raw);std::unique_ptr<WorkspaceLock> workspace_lock;std::unique_ptr<DocumentLock> source_lock;
  if(args.contains("native_source")){source_lock=std::make_unique<DocumentLock>(workspace,text_field(args.at("native_source"),"document_id"),LockWait::publication);if(native_record(store,args.at("native_source"))!=text_field(source.at("native_source_association"),"source_record_sha256"))throw Error("artifact_source_mismatch","Historical native source changed before artifact publication");}
  else workspace_lock=std::make_unique<WorkspaceLock>(workspace);
  if(fs::exists(target)){auto replay=verify_external_artifact(target,sha256(review_raw));replay["action"]="review";return replay;}fs::rename(stage.path,target);return result(target,review_raw,review);
}
Json verify_external_artifact(const fs::path&package,const std::string&expected){
  digest(expected);require(package.is_absolute()&&fs::is_directory(package)&&!fs::is_symlink(fs::symlink_status(package)),"Review package must be an absolute non-symlink directory");try{const auto raw=read_text(package/"review.json",review_limit);if(sha256(raw)!=expected)throw Error("artifact_mismatch","Review differs from expected SHA-256");const auto review=parse_json(raw,review_limit);review_contract(review);const auto manifest=parse_json(read_text(package/"manifest.json"));
  fields(manifest,{"schema_version","source_sha256","review_sha256","artifacts"});require(manifest.at("schema_version")==1&&manifest.at("review_sha256")==expected&&manifest.at("source_sha256")==review.at("source").at("sha256"),"Review manifest identity differs");
  std::vector<std::string> paths={text_field(review.at("source"),"path")};for(const auto&r:review.at("source").at("references"))paths.push_back(text_field(r,"path"));paths.push_back("review.json");require(ledger(package,paths,review.at("source"))==manifest.at("artifacts"),"Review artifact ledger differs");
  std::set<std::string> remaining(paths.begin(),paths.end());remaining.insert("manifest.json");std::size_t entries=0;for(const auto&e:fs::recursive_directory_iterator(package)){check_job_cancelled();require(++entries<=70&&!e.is_symlink()&&(e.is_regular_file()||e.is_directory()),"Unexpected review package entry");const auto u=e.path().lexically_relative(package).generic_u8string();const std::string name(reinterpret_cast<const char*>(u.data()),u.size());if(e.is_directory())require(name=="references","Unexpected review package directory");else require(remaining.erase(name)==1,"Unexpected review package file");}
  require(remaining.empty(),"Incomplete review package");const auto&source=review.at("source");const auto source_raw=read_text(package/path_from_utf8(text_field(source,"path")),source.at("format")=="step"?unlimited_bytes:artifact_input_limit);require(sha256(source_raw)==text_field(source,"sha256")&&source_raw.size()==source.at("bytes"),"Captured source differs from review");
  References refs;for(const auto&r:source.at("references")){const auto captured=read_text(package/path_from_utf8(text_field(r,"path")),artifact_input_limit);require(sha256(captured)==text_field(r,"sha256")&&captured.size()==r.at("bytes"),"Captured reference differs from review");const auto uri=text_field(r,"uri");refs.emplace(uri,Reference{uri,captured,text_field(r,"sha256"),text_field(r,"units"),package/path_from_utf8(text_field(r,"path"))});}
  // The OS temporary root may be a platform alias (macOS /tmp). Resolve only
  // this service-owned scratch root; caller source/package paths remain strict.
  Stage parser_workspace(fs::canonical(fs::temp_directory_path()));const auto parsed=parse_artifact(parser_workspace.path,source,source_raw,refs);const bool tolerant=source.at("format")=="step";
  if(!equivalent(parsed.geometry.result(),review.at("geometry"),tolerant)||!equivalent(parsed.geometry.summary(parsed.representation),review.at("summary"),tolerant)||!equivalent(parsed.metadata,review.at("metadata"),tolerant)||parsed.limitations!=review.at("limitations"))throw Error("artifact_mismatch","Captured source does not reproduce the persisted review qualification");
  require(read_text(package/"review.json",review_limit)==raw&&parse_json(read_text(package/"manifest.json"))==manifest,"Review package changed during verification");auto response=result(package,raw,review);response["action"]="verify";return response;
  }catch(const Json::exception&){throw Error("artifact_invalid","Persisted artifact JSON fields are malformed or incomplete");}
  catch(const fs::filesystem_error&){throw Error("artifact_mismatch","Review package files are missing or changed");}
  catch(const Error& e){if(e.code=="invalid_json"||e.code=="invalid_argument"||e.code=="limit_exceeded")throw Error("artifact_invalid",e.what());if(e.code=="storage_error"||e.code=="not_found")throw Error("artifact_mismatch",e.what());throw;}

}
}
