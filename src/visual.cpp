#include "agentcad/visual.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/model.hpp"
#include "agentcad/render_settings.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <map>
#include <set>
namespace agentcad {
namespace {
Json object(Json p,Json required){return {{"type","object"},{"properties",p},{"required",required},{"additionalProperties",false}};}
Json id(){return {{"type","string"},{"pattern","^[A-Za-z][A-Za-z0-9_-]{0,63}$"}};}
void sequence_label(const std::string& name){if(name.empty()||name.size()>64||std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isspace(c);})||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;}))throw Error("invalid_argument","Sequence name needs 1 to 64 UTF-8 bytes without control characters");}
void vector_unit(const Json& value){if(!value.is_array()||value.size()!=3)throw Error("invalid_argument","Explode direction must be a unit vector");double length=0;for(const auto& n:value){const auto v=number(n);length+=v*v;}if(std::abs(length-1)>1e-6)throw Error("invalid_argument","Explode direction must be a unit vector");}
}
Json visual_appearance_schema(){return object({{"default_color",{{"type","array"},{"items",{{"type","number"},{"minimum",0},{"maximum",1}}},{"minItems",3},{"maxItems",3}}},{"material",render_material_schema()},
 {"parts",{{"type","array"},{"maxItems",assembly_leaf_limit},{"items",object({{"part_id",occurrence_schema()},{"color",{{"type","array"},{"items",{{"type","number"},{"minimum",0},{"maximum",1}}},{"minItems",3},{"maxItems",3}}},{"material",render_material_schema()}},{"part_id","color"})}}}},{"default_color","parts"});}
Json visual_animation_schema(){return object({{"view_id",id()},{"sequence_name",{{"type","string"},{"minLength",1},{"maxLength",64}}},{"sample_rate_hz",{{"type","number"},{"minimum",1},{"maximum",60}}},{"max_frames",{{"type","integer"},{"minimum",2},{"maximum",2048}}}},{"view_id","sequence_name"});}
void validate_visual_animation(const Json& value){fields(value,{"view_id","sequence_name"},{"sample_rate_hz","max_frames"});portable_identifier(text_field(value,"view_id"));sequence_label(text_field(value,"sequence_name"));const auto rate=value.contains("sample_rate_hz")?number(value.at("sample_rate_hz")):15.;if(rate<1||rate>60)throw Error("invalid_argument","Animation sample rate must be 1 to 60 Hz");if(value.contains("max_frames")&&(!value.at("max_frames").is_number_integer()||value.at("max_frames")<2||value.at("max_frames")>2048))throw Error("invalid_argument","Animation frame cap must be 2 to 2048");}
Json visual_sample_times(const Json& sequence,double rate,std::size_t limit){
 const auto& frames=sequence.at("frames");if(!frames.is_array()||frames.size()<2||frames.size()>64)throw Error("invalid_argument","A visual sequence needs 2 to 64 keyframes");double previous=-1;std::set<double> times;
 for(const auto& frame:frames){fields(frame,{"time_s","presentation","joints"});const auto time=number(frame.at("time_s"));if(time<0||time>3600||time<=previous||(previous<0&&time!=0))throw Error("invalid_argument","Sequence times must start at zero and increase within 3600 seconds");previous=time;times.insert(time);}
 if(rate<1||rate>60||limit<2||limit>2048||std::ceil(previous*rate)+1>static_cast<double>(limit))throw Error("limit_exceeded","Animation sample count exceeds the requested frame budget");
 for(std::size_t n=0;n<=static_cast<std::size_t>(std::ceil(previous*rate));++n)times.insert(std::min(previous,n/rate));if(times.size()>limit)throw Error("limit_exceeded","Animation keyframes and uniform samples exceed the frame budget");return times;
}
Json capture_visual_sequence(Store& store,const Json& value,const Json& record,const std::string& feature,bool allow_clipping){
 validate_visual_animation(value);const auto parent=store.root()/"views",path=parent/text_field(value,"view_id");
 if(fs::is_symlink(fs::symlink_status(parent))||fs::is_symlink(fs::symlink_status(path))||!fs::is_regular_file(fs::symlink_status(path/"state.json")))throw Error("stale_selection","Animation needs an existing native view sequence");
 WorkspaceLock lock(path);const auto state=parse_json(read_text(path/"state.json"));if(state.value("read_only",false))throw Error("read_only_artifact","Native GLB animation requires a native saved sequence");
 const auto name=text_field(value,"sequence_name");for(const auto& sequence:state.value("sequences",Json::array()))if(sequence.at("name")==name){
  fields(sequence,{"name","source","frames"});const auto& source=sequence.at("source");
  if(source.at("document_id")!=record.at("document_id")||source.at("revision")!=record.at("revision")||text_field(source,"feature_id")!=feature||text_field(source,"model_sha256")!=sha256(record.at("model").dump()))throw Error("stale_selection","Animation sequence belongs to another model, revision or feature");
  const auto rate=value.value("sample_rate_hz",15.);const auto times=visual_sample_times(sequence,rate,value.value("max_frames",std::size_t(2048)));
  for(const auto& frame:sequence.at("frames")){visual_sequence_operations(frame.at("joints"),record.at("model"),feature);if(!allow_clipping&&!frame.at("presentation").at("clip").is_null())throw Error("unsupported_export_effect","Animated GLB cannot represent clipping planes");}
  return {{"sequence",sequence},{"sequence_sha256",sha256(sequence.dump())},{"view_id",value.at("view_id")},{"sample_rate_hz",rate},{"times",times}};
 }
 throw Error("not_found","Saved animation sequence is absent",{{"name",name}});
}
Json visual_sequence_operations(const Json& joints,const Json& model,const std::string& feature){
 if(!joints.is_array()||joints.size()>16)throw Error("invalid_argument","A sequence supports at most sixteen mechanism definitions");const auto mechanisms=assembly_mechanisms(model,feature);std::set<std::string> scopes;Json operations=Json::array();
 for(const auto& item:joints){fields(item,{"assembly_id","values"});const auto scope_id=text_field(item,"assembly_id");model_identifier(scope_id);const Json* scope=nullptr;for(const auto& candidate:mechanisms)if(candidate.at("assembly_id")==scope_id)scope=&candidate;
  if(!scope||!scopes.insert(scope_id).second)throw Error("invalid_argument","Sequence mechanisms must be unique and reachable");const auto& values=item.at("values");if(!values.is_array()||values.empty()||values.size()>126)throw Error("invalid_argument","Sequence must include every independent coordinate");std::set<std::string> expected,seen;
  for(const auto& dof:scope->at("motion").at("dofs"))if(!dof.at("driven").get<bool>())expected.insert(text_field(dof,"mate_id")+"."+text_field(dof,"coordinate"));
  for(const auto& v:values){fields(v,{"mate_id","coordinate","value"});number(v.at("value"));const auto key=text_field(v,"mate_id")+"."+text_field(v,"coordinate");if(!expected.contains(key)||!seen.insert(key).second)throw Error("invalid_argument","Sequence coordinates must cover independent degrees exactly once");auto operation=v;operation["op"]="set_joint_value";operation["assembly_id"]=scope_id;operations.push_back(operation);}
  if(seen!=expected)throw Error("invalid_argument","Sequence omits an independent coordinate");
 }
 if(operations.size()>126)throw Error("limit_exceeded","Sequence exceeds 126 independent coordinates");if(!operations.empty())apply_operations(model,operations);return operations;
}
Json visual_sequence_sample(const Json& sequence,double time){
 const auto& frames=sequence.at("frames");if(!std::isfinite(time)||time<0||time>number(frames.back().at("time_s")))throw Error("invalid_argument","Animation time is outside this sequence");std::size_t upper=1;while(upper+1<frames.size()&&number(frames.at(upper).at("time_s"))<time)++upper;const auto& a=frames[upper-1];const auto& b=frames[upper];const auto t=(time-number(a.at("time_s")))/(number(b.at("time_s"))-number(a.at("time_s")));auto sample=a;sample["time_s"]=time;
 for(const auto* field:{"distance_mm"})sample["presentation"]["explode"][field]=number(a.at("presentation").at("explode").at(field))*(1-t)+number(b.at("presentation").at("explode").at(field))*t;
 if(!sample.at("presentation").at("clip").is_null())sample["presentation"]["clip"]["offset_mm"]=number(a.at("presentation").at("clip").at("offset_mm"))*(1-t)+number(b.at("presentation").at("clip").at("offset_mm"))*t;
 std::map<std::string,double> ends;for(const auto& scope:b.at("joints"))for(const auto& v:scope.at("values"))ends[text_field(scope,"assembly_id")+"/"+text_field(v,"mate_id")+"/"+text_field(v,"coordinate")]=number(v.at("value"));
 for(auto& scope:sample["joints"])for(auto& v:scope["values"]){const auto key=text_field(scope,"assembly_id")+"/"+text_field(v,"mate_id")+"/"+text_field(v,"coordinate");if(!ends.contains(key))throw Error("invalid_argument","Animation frame coordinate sets differ");v["value"]=number(v.at("value"))*(1-t)+ends.at(key)*t;}return sample;
}
Json visual_pose_parts(const Json& summary,const Json& presentation){
 fields(presentation,{"clip","explode"});if(!presentation.at("clip").is_null())throw Error("unsupported_export_effect","GLB cannot represent a clipping plane");const auto& explode=presentation.at("explode");fields(explode,{"distance_mm","directions"});const auto distance=number(explode.at("distance_mm"));if(distance<0||distance>1e6)throw Error("invalid_argument","Explode distance exceeds bounds");if(!summary.contains("assembly"))throw Error("invalid_argument","Native animation needs an assembly with leaf placements");
 const auto& parts=summary.at("assembly").at("parts");std::map<std::string,Json> directions;std::set<std::string> owners;for(const auto& part:parts)owners.insert(text_field(part,"id"));
 if(!explode.at("directions").is_array()||explode.at("directions").size()>assembly_leaf_limit)throw Error("invalid_argument","Invalid explode direction list");for(const auto& item:explode.at("directions")){fields(item,{"part_id","direction"});const auto owner=text_field(item,"part_id");if(!owners.contains(owner)||!directions.emplace(owner,item.at("direction")).second)throw Error("invalid_argument","Explode directions need unique current leaves");vector_unit(item.at("direction"));}
 const auto& bounds=summary.at("bounds_mm");double span=1;for(int i=0;i<3;++i)span=std::max(span,number(bounds.at("max").at(i))-number(bounds.at("min").at(i)));std::vector<std::string> ordered(owners.begin(),owners.end());Json result=Json::array();for(const auto& part:parts){const auto owner=text_field(part,"id");auto transform=part.at("transform");double d[3]={0,0,0},length=0;for(int i=0;i<3;++i){d[i]=directions.contains(owner)?number(directions.at(owner).at(i)):(number(part.at("bounds_mm").at("min").at(i))+number(part.at("bounds_mm").at("max").at(i))-number(bounds.at("min").at(i))-number(bounds.at("max").at(i)))*.5;length+=d[i]*d[i];}length=std::sqrt(length);if(!directions.contains(owner)&&length<1e-12*span){const auto index=std::find(ordered.begin(),ordered.end(),owner)-ordered.begin();for(auto& v:d)v=0;d[index%3]=index%2?-1:1;length=1;}for(int i=0;i<3;++i)transform[i*4+3]=number(transform.at(i*4+3))+distance*d[i]/length;result.push_back({{"id",owner},{"transform",transform}});}return result;
}
}
