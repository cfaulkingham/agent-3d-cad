#include "agentcad/model.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

namespace agentcad {
namespace {
const std::set<std::string> unary={"negate","abs","sqrt","square","sin","cos","tan","sin_deg","cos_deg","tan_deg","asin","acos","atan","floor","ceil","round","exp","log","not"};
const std::set<std::string> binary={"add","subtract","multiply","divide","min","max","pow","atan2","less","less_equal","greater","greater_equal","equal","not_equal","and","or"};
const std::set<std::string> ternary={"if","clamp"};
const std::set<std::string> comparisons={"less","less_equal","greater","greater_equal","equal","not_equal"};
const std::set<std::string> units={"mm","mm2","deg","rad","dimensionless"};
std::string argument_unit(const Json& expression,const std::string& unit,std::size_t index) {
  const auto op=text_field(expression,"op");
  if(comparisons.contains(op)||op=="atan2")return expression.value("argument_unit",std::string("dimensionless"));
  if(op=="sin"||op=="cos"||op=="tan")return "rad";
  if(op=="sin_deg"||op=="cos_deg"||op=="tan_deg")return "deg";
  if(op=="asin"||op=="acos"||op=="atan"||op=="exp"||op=="log"||op=="pow"||op=="not"||op=="and"||op=="or")return "dimensionless";
  if(op=="sqrt")return unit=="mm"?"mm2":"dimensionless";
  if(op=="square")return unit=="mm2"?"mm":"dimensionless";
  if((op=="multiply"||op=="divide")&&index==1)return "dimensionless";
  if(op=="if"&&index==0)return "dimensionless";
  return unit;
}
void check_tree(const Json& value,const Json& parameters,const std::string& unit,int depth,int& nodes) {
  if(++nodes>128||depth>16)throw Error("limit_exceeded","Expressions permit at most 128 nodes and 16 levels");
  if(value.is_number()){number(value);return;}
  if(!value.is_object()||!value.contains("expression")) {
    fields(value,{"parameter"});
    const auto name=text_field(value,"parameter");
    if(!parameters.contains(name))throw Error("invalid_model","Unknown parameter: "+name);
    number(parameters.at(name));return;
  }
  fields(value,{"expression"});const auto& expression=value.at("expression");
  fields(expression,{"op","args","unit"},{"argument_unit"});
  if(text_field(expression,"unit")!=unit||!units.contains(unit))
    throw Error("invalid_model","Expression unit does not match its argument context",{{"expected_unit",unit}});
  const auto op=text_field(expression,"op");
  const std::size_t arity=unary.contains(op)?1:binary.contains(op)?2:ternary.contains(op)?3:0;
  if(!arity)throw Error("invalid_model","Unsupported arithmetic expression operation");
  const auto& args=expression.at("args");
  if(!args.is_array()||args.size()!=arity)throw Error("invalid_model","Expression has incorrect argument count",{{"op",op},{"expected_count",arity}});
  if(expression.contains("argument_unit")&&(!comparisons.contains(op)&&op!="atan2"))
    throw Error("invalid_model","argument_unit is only supported for comparisons and atan2");
  if(expression.contains("argument_unit")&&!units.contains(text_field(expression,"argument_unit")))throw Error("invalid_model","Unsupported expression argument unit");
  const bool dimensionless=comparisons.contains(op)||op=="sin"||op=="cos"||op=="tan"||op=="sin_deg"||op=="cos_deg"||op=="tan_deg"||op=="pow"||op=="exp"||op=="log"||op=="not"||op=="and"||op=="or";
  if(dimensionless&&unit!="dimensionless")throw Error("invalid_model","Expression operation requires a dimensionless result",{{"op",op}});
  if((op=="asin"||op=="acos"||op=="atan"||op=="atan2")&&unit!="deg"&&unit!="rad")throw Error("invalid_model","Inverse trigonometric expressions require deg or rad results");
  if(op=="sqrt"&&unit!="mm"&&unit!="dimensionless")throw Error("invalid_model","Square root result must be mm or dimensionless");
  if(op=="square"&&unit!="mm2"&&unit!="dimensionless")throw Error("invalid_model","Square result must be mm2 or dimensionless");
  // Check every branch structurally, including inactive conditionals. Evaluation
  // is lazy, so guarded singularities are allowed without hiding bad references.
  for(std::size_t i=0;i<arity;++i)check_tree(args[i],parameters,argument_unit(expression,unit,i),depth+1,nodes);
}
double evaluate(const Json& value,const Json& parameters,const std::string& unit) {
  if(value.is_number())return number(value);
  if(value.contains("parameter"))return number(parameters.at(text_field(value,"parameter")));
  const auto& expression=value.at("expression");const auto op=text_field(expression,"op");const auto& args=expression.at("args");
  const auto at=[&](std::size_t i){return evaluate(args[i],parameters,argument_unit(expression,unit,i));};
  const auto domain=[&](bool valid,const char* message){if(!valid)throw Error("invalid_model",message,{{"op",op}});};
  const auto a=at(0);
  if(op=="if")return at(a!=0?1:2);
  if(op=="negate")return number(-a);
  if(op=="abs")return std::abs(a);
  if(op=="sqrt"){domain(a>=0,"Square root argument must be nonnegative");return std::sqrt(a);}
  if(op=="square")return number(a*a);
  if(op=="floor")return std::floor(a);
  if(op=="ceil")return std::ceil(a);
  if(op=="round")return std::round(a);
  if(op=="not")return a==0?1:0;
  if(op=="exp")return number(std::exp(a));
  if(op=="log"){domain(a>0,"Logarithm argument must be positive");return number(std::log(a));}
  if(op=="sin"||op=="cos"||op=="tan"||op=="sin_deg"||op=="cos_deg"||op=="tan_deg") {
    const auto angle=op.ends_with("_deg")?a*std::numbers::pi/180:a;
    if(op.starts_with("tan"))domain(std::abs(std::cos(angle))>1e-12,"Tangent is undefined at a right angle");
    return number(op.starts_with("sin")?std::sin(angle):op.starts_with("cos")?std::cos(angle):std::tan(angle));
  }
  if(op=="asin"||op=="acos"||op=="atan") {
    if(op!="atan")domain(a>=-1&&a<=1,"Inverse sine/cosine argument must be between -1 and 1");
    const auto angle=op=="asin"?std::asin(a):op=="acos"?std::acos(a):std::atan(a);
    return number(unit=="deg"?angle*180/std::numbers::pi:angle);
  }
  const auto b=at(1);
  if(op=="add")return number(a+b);
  if(op=="subtract")return number(a-b);
  if(op=="multiply")return number(a*b);
  if(op=="divide"){domain(b!=0,"Expression division by zero");return number(a/b);}
  if(op=="min")return std::min(a,b);
  if(op=="max")return std::max(a,b);
  if(op=="pow"){
    domain(!(a==0&&b<=0)&&!(a<0&&std::trunc(b)!=b),"Power arguments are outside the real finite domain");return number(std::pow(a,b));
  }
  if(op=="atan2"){
    domain(a!=0||b!=0,"atan2 is undefined at the origin");const auto angle=std::atan2(a,b);return unit=="deg"?angle*180/std::numbers::pi:angle;
  }
  if(op=="less")return a<b?1:0;
  if(op=="less_equal")return a<=b?1:0;
  if(op=="greater")return a>b?1:0;
  if(op=="greater_equal")return a>=b?1:0;
  if(op=="equal")return a==b?1:0;
  if(op=="not_equal")return a!=b?1:0;
  if(op=="and")return a!=0&&b!=0?1:0;
  if(op=="or")return a!=0||b!=0?1:0;
  const auto c=at(2);domain(b<=c,"Clamp minimum must not exceed maximum");return std::clamp(a,b,c);
}
}
double scalar(const Json& value,const Json& parameters,const std::string& unit) {
  int nodes=0;check_tree(value,parameters,unit,0,nodes);return evaluate(value,parameters,unit);
}
int pattern_count(const Json& value,const Json& parameters) {
  const auto count=scalar(value,parameters,"dimensionless");
  if(count<2||count>64||std::trunc(count)!=count)throw Error("invalid_model","Pattern count must evaluate to an integer from 2 to 64");
  return static_cast<int>(count);
}
Json scalar_expression_schema(const Json& scalar_ref) {
  Json arities=Json::array();
  for(const auto& [ops,count]:std::initializer_list<std::pair<std::set<std::string>,int>>{{unary,1},{binary,2},{ternary,3}})
    arities.push_back({{"properties",{{"op",{{"enum",ops}}},{"args",{{"minItems",count},{"maxItems",count}}}}}});
  Json content={{"type","object"},{"properties",{{"op",{{"type","string"}}},{"args",{{"type","array"},{"items",scalar_ref}}},{"unit",{{"enum",units}}},{"argument_unit",{{"enum",units}}}}},
    {"required",{"op","args","unit"}},{"additionalProperties",false},{"oneOf",arities}};
  auto argument_ops=comparisons;argument_ops.insert("atan2");
  content["allOf"]=Json::array({Json{{"if",{{"required",{"argument_unit"}}}},{"then",{{"properties",{{"op",{{"enum",argument_ops}}}}}}}}});
  return {{"type","object"},{"properties",{{"expression",content}}},{"required",{"expression"}},{"additionalProperties",false}};
}
}
