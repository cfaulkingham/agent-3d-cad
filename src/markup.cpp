#include "agentcad/markup.hpp"
#include <cmath>
#include <numbers>
#include <set>

namespace agentcad {
namespace {
constexpr std::size_t item_limit=256,point_limit=8192,stroke_limit=2048,text_limit=512,scene_limit=512*1024,frame_limit=64*1024;
Json object(Json properties,Json required){return {{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};}
Json range(double minimum,double maximum){return {{"type","number"},{"minimum",minimum},{"maximum",maximum}};}
Json array(Json entry,std::size_t minimum,std::size_t maximum){return {{"type","array"},{"items",entry},{"minItems",minimum},{"maxItems",maximum}};}
Json id(){return {{"type","string"},{"pattern","^[A-Za-z][A-Za-z0-9_-]{0,63}$"}};}
Json nullable(Json schema){return {{"anyOf",Json::array({schema,{{"type","null"}}})}};}
void require(bool condition,const char* message){if(!condition)throw Error("invalid_argument",message);}
void bound(const Json& value,double lo,double hi){require(value.is_number()&&std::isfinite(value.get<double>())&&value>=lo&&value<=hi,"Markup number is outside its finite bounds");}
void pair(const Json& value,double lo,double hi){require(value.is_array()&&value.size()==2,"Markup requires a two-element coordinate");for(const auto& v:value)bound(v,lo,hi);}
bool hash(const Json& value){if(!value.is_string())return false;const auto& s=value.get_ref<const std::string&>();return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
void plain(const Json& value,std::size_t depth=0){
 require(depth<=16,"Markup frame nesting exceeds 16 levels");
 if(value.is_null()||value.is_boolean())return;
 if(value.is_number()){require(std::isfinite(value.get<double>()),"Markup frame has a non-finite number");return;}
 if(value.is_string()){require(value.get_ref<const std::string&>().size()<=65536&&!invalid_utf8_offset(value.get_ref<const std::string&>()),"Invalid markup frame text");return;}
 if(value.is_array()){require(value.size()<=8192,"Markup frame array exceeds its limit");for(const auto& entry:value)plain(entry,depth+1);return;}
 require(value.is_object()&&value.size()<=128,"Invalid markup frame object");for(const auto& entry:value.items()){require(entry.key()!="__proto__"&&entry.key()!="constructor"&&entry.key()!="prototype","Invalid markup frame field");plain(entry.value(),depth+1);}
}
Json binding_schema(){
 const auto camera=object({{"yaw",range(-1e9,1e9)},{"pitch",range(-std::numbers::pi/2,std::numbers::pi/2)},{"zoom",range(.000001,1e9)},{"pan",array(range(-1e9,1e9),2,2)}},{"yaw","pitch","zoom","pan"});
 const auto settings=nullable(Json{{"type","object"}});
 const auto frame=object({{"camera",camera},{"hidden_part_ids",{{"type","array"},{"items",{{"type","string"},{"maxLength",512}}},{"maxItems",1024},{"uniqueItems",true}}},{"presentation",settings},{"appearance",settings},{"render",settings},{"artifact_pose",settings},{"section",settings},{"annotations",array(Json{{"type","object"}},0,32)}},{"camera","hidden_part_ids","presentation","appearance","render","artifact_pose","section","annotations"});
 const auto native=object({{"view_id",id()},{"document_id",id()},{"revision",{{"type","integer"},{"minimum",1},{"maximum",9007199254740991ULL}}},{"evaluation_id",id()},{"feature_id",id()},{"review_sha256",{{"type","null"}}},{"draft",{{"type","boolean"}}}},{"view_id","document_id","revision","evaluation_id","feature_id","review_sha256","draft"});
 const auto artifact=object({{"view_id",id()},{"document_id",{{"type","null"}}},{"revision",{{"type","null"}}},{"evaluation_id",id()},{"feature_id",{{"type","null"}}},{"review_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},{"draft",{{"const",false}}}},{"view_id","document_id","revision","evaluation_id","feature_id","review_sha256","draft"});
 return object({{"source",{{"oneOf",Json::array({native,artifact})}}},{"frame",frame},{"viewport",array(range(1,10000),2,2)}},{"source","frame","viewport"});
}
}
void validate_markup_binding(const Json& binding){
 fields(binding,{"source","frame","viewport"});const auto& source=binding.at("source");fields(source,{"view_id","document_id","revision","evaluation_id","feature_id","review_sha256","draft"});identifier(text_field(source,"view_id"));identifier(text_field(source,"evaluation_id"));require(source.at("draft").is_boolean(),"Markup draft must be boolean");
 if(source.at("review_sha256").is_null()){identifier(text_field(source,"document_id"));identifier(text_field(source,"feature_id"));require(source.at("revision").is_number_integer()&&source.at("revision")>=1&&source.at("revision")<=9007199254740991ULL,"Invalid markup revision");}
 else require(hash(source.at("review_sha256"))&&source.at("document_id").is_null()&&source.at("revision").is_null()&&source.at("feature_id").is_null()&&source.at("draft")==false,"Invalid artifact markup source");
 pair(binding.at("viewport"),1,10000);const auto& frame=binding.at("frame");fields(frame,{"camera","hidden_part_ids","presentation","appearance","render","artifact_pose","section","annotations"});const auto& camera=frame.at("camera");fields(camera,{"yaw","pitch","zoom","pan"});bound(camera.at("yaw"),-1e9,1e9);bound(camera.at("pitch"),-std::numbers::pi/2,std::numbers::pi/2);bound(camera.at("zoom"),.000001,1e9);pair(camera.at("pan"),-1e9,1e9);
 const auto& hidden=frame.at("hidden_part_ids");require(hidden.is_array()&&hidden.size()<=1024,"Invalid markup visibility");std::set<std::string> unique;for(const auto& entry:hidden){require(entry.is_string()&&entry.get_ref<const std::string&>().size()<=512,"Invalid markup visibility path");require(unique.insert(entry.get<std::string>()).second,"Duplicate markup visibility path");}
 for(const auto* key:{"presentation","appearance","render","artifact_pose","section"})require(frame.at(key).is_null()||frame.at(key).is_object(),"Invalid markup frame settings");require(frame.at("annotations").is_array()&&frame.at("annotations").size()<=32,"Invalid markup frame notes");for(const auto& note:frame.at("annotations"))require(note.is_object(),"Invalid markup frame note");plain(frame);require(frame.dump().size()<=frame_limit,"Markup frame exceeds 64 KiB");
}
void validate_markup_scene(const Json& scene){
 fields(scene,{"schema_version","binding","items"});require(scene.at("schema_version")==1,"Unsupported markup version");validate_markup_binding(scene.at("binding"));const auto& items=scene.at("items");require(items.is_array()&&items.size()<=item_limit,"Markup exceeds 256 items");std::set<std::string> ids;std::size_t points=0;
 for(const auto& item:items){const auto kind=text_field(item,"kind");require(std::set<std::string>{"pen","line","arrow","rectangle","ellipse","text"}.contains(kind),"Unknown markup tool");if(kind=="text")fields(item,{"id","kind","color","width","points","text","font_size"});else fields(item,{"id","kind","color","width","points"});const auto name=text_field(item,"id");identifier(name);require(ids.insert(name).second,"Duplicate markup item");const auto color=text_field(item,"color");require(color.size()==7&&color[0]=='#'&&color.find_first_not_of("0123456789abcdefABCDEF",1)==std::string::npos,"Invalid markup color");bound(item.at("width"),1,16);
  const auto& coordinates=item.at("points");require(coordinates.is_array(),"Markup points must be an array");require(kind=="pen"?coordinates.size()>=1&&coordinates.size()<=stroke_limit:coordinates.size()==(kind=="text"?1:2),"Invalid markup point count");for(const auto& p:coordinates)pair(p,0,1);points+=coordinates.size();
  if(kind=="text"){const auto text=text_field(item,"text");require(!text.empty()&&text.size()<=text_limit&&!invalid_utf8_offset(text),"Markup text requires 1–512 UTF-8 bytes");bool meaningful=false;std::size_t lines=1;for(unsigned char c:text){require(c>=32||c=='\n'||c=='\t',"Markup text has a control character");require(c!=127,"Markup text has a control character");if(c=='\n')++lines;if(c!=' '&&c!='\t'&&c!='\r'&&c!='\n')meaningful=true;}require(meaningful&&lines<=8,"Markup text must be meaningful and at most eight lines");bound(item.at("font_size"),10,48);}
 }
 require(points<=point_limit,"Markup exceeds 8192 total points");require(scene.dump().size()<=scene_limit,"Markup exceeds 512 KiB");
}
Json markup_scene_schema(){
 const auto point_schema=array(range(0,1),2,2);Json variants=Json::array();for(const std::string kind:{"pen","line","arrow","rectangle","ellipse","text"}){
  Json properties={{"id",id()},{"kind",{{"const",kind}}},{"color",{{"type","string"},{"pattern","^#[a-fA-F0-9]{6}$"}}},{"width",range(1,16)},{"points",array(point_schema,kind=="pen"||kind=="text"?1:2,kind=="pen"?stroke_limit:kind=="text"?1:2)}};Json required={"id","kind","color","width","points"};if(kind=="text"){properties["text"]={{"type","string"},{"minLength",1},{"maxLength",text_limit}};properties["font_size"]=range(10,48);required.push_back("text");required.push_back("font_size");}variants.push_back(object(properties,required));}
 return object({{"schema_version",{{"const",1}}},{"binding",binding_schema()},{"items",array(Json{{"oneOf",variants}},0,item_limit)}},{"schema_version","binding","items"});
}
bool markup_matches(const Json& scene,const Json& binding,bool compare_viewport){
 if(scene.is_null()||binding.is_null())return false;validate_markup_scene(scene);validate_markup_binding(binding);const auto& saved=scene.at("binding");if(saved.at("source")!=binding.at("source")||saved.at("frame")!=binding.at("frame"))return false;if(!compare_viewport)return true;
 const auto& a=saved.at("viewport");const auto& b=binding.at("viewport");return std::abs(a[0].get<double>()/a[1].get<double>()-b[0].get<double>()/b[1].get<double>())<1e-6;
}
Json markup_context(const Json& scene,const Json& binding){if(!scene.is_null())validate_markup_scene(scene);const auto count=scene.is_null()?0:scene.at("items").size();return {{"status",count?(markup_matches(scene,binding)?"current":"retired"):"empty"},{"coordinate_space","viewport_normalized"},{"anchor_lifetime","source_and_frame"},{"item_count",count},{"scene",scene}};}
Json markup_context_schema(){return object({{"status",{{"enum",{"current","retired","empty"}}}},{"coordinate_space",{{"const","viewport_normalized"}}},{"anchor_lifetime",{{"const","source_and_frame"}}},{"item_count",{{"type","integer"},{"minimum",0},{"maximum",item_limit}}},{"scene",nullable(markup_scene_schema())}},{"status","coordinate_space","anchor_lifetime","item_count","scene"});}
}
