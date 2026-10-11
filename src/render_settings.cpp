#include "agentcad/render_settings.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace agentcad {
namespace {
Json object(Json p){return {{"type","object"},{"properties",p},{"additionalProperties",false}};}
Json range(double lo,double hi){return {{"type","number"},{"minimum",lo},{"maximum",hi}};}
Json rgb_schema(){return {{"type","array"},{"items",range(0,1)},{"minItems",3},{"maxItems",3}};}
void bound(const Json& value,double lo,double hi){if(!value.is_number()||!std::isfinite(value.get<double>())||value<lo||value>hi)throw Error("invalid_argument","Render number is outside its finite bounds");}
void boolean(const Json& value){if(!value.is_boolean())throw Error("invalid_argument","Render toggle must be boolean");}
void color(const Json& value){if(!value.is_array()||value.size()!=3)throw Error("invalid_argument","Render color must contain three channels");for(const auto& n:value)bound(n,0,1);}
void choice(const Json& value,const std::set<std::string>& choices){if(!value.is_string()||!choices.contains(value.get<std::string>()))throw Error("invalid_argument","Unknown render setting value");}
}
Json render_material_schema(){return object({{"opacity",range(0,1)},{"finish",{{"enum",{"matte","satin","glossy","metal"}}}}});}
Json normalize_render_material(const Json& value){fields(value,{}, {"opacity","finish"});auto result=Json{{"opacity",1},{"finish","satin"}};result.update(value);bound(result.at("opacity"),0,1);choice(result.at("finish"),{"matte","satin","glossy","metal"});return result;}
Json render_settings_schema(){return object({
 {"mode",{{"enum",{"solid","render","xray","hidden_line","wireframe"}}}},
 {"projection",{{"enum",{"orthographic","perspective"}}}},{"lens_mm",range(18,120)},
 {"surfaces",render_material_schema()},
 {"lighting",object({{"enabled",{{"type","boolean"}}},{"exposure",range(.1,4)},{"rotation_deg",range(-360,360)},{"fill",range(0,1)}})},
 {"background",object({{"color",rgb_schema()}})},
 {"floor",object({{"enabled",{{"type","boolean"}}},{"color",rgb_schema()},{"opacity",range(0,1)},{"finish",{{"enum",{"matte","glossy"}}}}})},
 {"grid",{{"type","boolean"}}},{"axes",{{"type","boolean"}}},{"edges",{{"type","boolean"}}}
 });}
Json normalize_render_settings(const Json& value){
 fields(value,{}, {"mode","projection","lens_mm","surfaces","lighting","background","floor","grid","axes","edges"});const auto mode=value.value("mode",Json("solid"));choice(mode,{"solid","render","xray","hidden_line","wireframe"});const bool studio=mode=="render";
 Json out={{"mode",mode},{"projection",studio?"perspective":"orthographic"},{"lens_mm",50},
  {"surfaces",{{"opacity",mode=="xray"?.22:1},{"finish","satin"}}},
  {"lighting",{{"enabled",true},{"exposure",1},{"rotation_deg",0},{"fill",.35}}},
  {"background",{{"color",studio?Json{.91,.93,.96}:Json{.941,.941,.941}}}},
  {"floor",{{"enabled",studio},{"color",{.8,.82,.85}},{"opacity",1},{"finish","matte"}}},
  {"grid",!studio},{"axes",!studio},{"edges",!studio}};
 for(const auto* key:{"surfaces","lighting","background","floor"})if(value.contains(key)){
  auto allowed=std::set<std::string>();for(const auto& item:out.at(key).items())allowed.insert(item.key());
  if(!value.at(key).is_object())throw Error("invalid_argument","Render settings group must be an object");
  for(const auto& item:value.at(key).items())if(!allowed.contains(item.key()))throw Error("invalid_argument","Unknown render settings group field");
  out[key].update(value.at(key));
 }
 for(const auto* key:{"projection","lens_mm","grid","axes","edges"})if(value.contains(key))out[key]=value.at(key);
 choice(out.at("projection"),{"orthographic","perspective"});bound(out.at("lens_mm"),18,120);
 out["surfaces"]=normalize_render_material(out.at("surfaces"));
 auto& light=out.at("lighting");boolean(light.at("enabled"));bound(light.at("exposure"),.1,4);bound(light.at("rotation_deg"),-360,360);bound(light.at("fill"),0,1);
 color(out.at("background").at("color"));auto& floor=out.at("floor");boolean(floor.at("enabled"));color(floor.at("color"));bound(floor.at("opacity"),0,1);choice(floor.at("finish"),{"matte","glossy"});
 for(const auto* key:{"grid","axes","edges"})boolean(out.at(key));return out;
}
}
