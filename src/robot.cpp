#include "agentcad/robot.hpp"
#include "agentcad/model.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <numbers>
#include <set>
#include <sstream>

namespace agentcad {
namespace {
Json object(Json properties,Json required) {
  return {{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};
}
std::string formatted(double value) {
  if(!std::isfinite(value)) throw Error("invalid_argument","Robot values must be finite");
  std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17)<<(value==0?0:value);return out.str();
}
double numeric(const Json& value) {
  if(!value.is_number()) throw Error("invalid_argument","Robot physical values must be numbers");
  const double result=value.get<double>();
  if(!std::isfinite(result) || std::abs(result)>1e9) throw Error("invalid_argument","Robot physical value exceeds its finite numeric bound");
  return result;
}
void vector(const Json& value,std::size_t size) {
  if(!value.is_array() || value.size()!=size) throw Error("invalid_argument","Robot vector has incorrect length");
  for(const auto& item:value) numeric(item);
}
std::string triple(const Json& value,double scale=1) {
  return formatted(value[0].get<double>()*scale)+" "+formatted(value[1].get<double>()*scale)+" "+formatted(value[2].get<double>()*scale);
}
std::pair<std::string,std::string> pose(const Json& m) {
  const double pitch=std::asin(std::clamp(-m[8].get<double>(),-1.0,1.0));
  const bool regular=std::abs(std::cos(pitch))>1e-10;
  const double roll=regular?std::atan2(m[9].get<double>(),m[10].get<double>()):0;
  const double yaw=regular?std::atan2(m[4].get<double>(),m[0].get<double>()):std::atan2(-m[1].get<double>(),m[5].get<double>());
  return {triple(Json::array({m[3],m[7],m[11]}),.001),triple(Json::array({roll,pitch,yaw}))};
}
std::string origin(const Json& matrix) {
  const auto [xyz,rpy]=pose(matrix);return "<origin xyz=\""+xyz+"\" rpy=\""+rpy+"\"/>";
}
std::string sdf_pose(const Json& matrix,const std::string& relative) {
  const auto [xyz,rpy]=pose(matrix);return "<pose relative_to=\""+relative+"\">"+xyz+" "+rpy+"</pose>";
}
std::string key(const Json& coordinate) {return text_field(coordinate,"mate_id")+"."+text_field(coordinate,"coordinate");}
double scale(const Json& coordinate) {return coordinate.at("coordinate")=="angle_deg"?std::numbers::pi/180:.001;}
double determinant(double a,double b,double c,double d,double e,double f) {
  return a*b*c+2*d*e*f-a*f*f-b*e*e-c*d*d;
}
void check_inertia(const Json& value) {
  vector(value,6);
  const double norm=std::max({numeric(value[0]),numeric(value[1]),numeric(value[2])});
  if(norm<=0) throw Error("invalid_argument","Inertia must be positive definite");
  const double a=numeric(value[0])/norm,b=numeric(value[1])/norm,c=numeric(value[2])/norm,
    d=numeric(value[3])/norm,e=numeric(value[4])/norm,f=numeric(value[5])/norm;
  if(a<=0 || b<=0 || c<=0 || a*b-d*d<=0 || determinant(a,b,c,d,e,f)<=0)
    throw Error("invalid_argument","Inertia must be positive definite");
  // The second-moment tensor C=trace(I)/2*identity-I must be positive
  // semidefinite; this tests principal-moment triangle inequalities in any frame.
  const double trace=(a+b+c)/2,x=trace-a,y=trace-b,z=trace-c,tolerance=1e-12;
  if(std::min({x,y,z,x*y-d*d,x*z-e*e,y*z-f*f,determinant(x,y,z,-d,-e,-f)}) < -tolerance)
    throw Error("invalid_argument","Inertia violates physical principal-moment triangle inequalities");
}
std::string inertia_xml(const Json& item,bool sdf) {
  const auto& values=item.at("inertia_kg_m2");
  const auto xyz=triple(item.at("center_of_mass_m"));
  const std::array<const char*,6> names={"ixx","iyy","izz","ixy","ixz","iyz"};
  std::string out="<inertial>";
  if(sdf) out+="<pose>"+xyz+" 0 0 0</pose><mass>"+formatted(item.at("mass_kg"))+"</mass><inertia>";
  else out+="<origin xyz=\""+xyz+"\" rpy=\"0 0 0\"/><mass value=\""+formatted(item.at("mass_kg"))+"\"/><inertia";
  for(std::size_t i=0;i<names.size();++i) {
    const std::string name=names[i];
    out+=sdf?"<"+name+">"+formatted(values[i])+"</"+name+">":" "+name+"=\""+formatted(values[i])+"\"";
  }
  return out+(sdf?"</inertia>":"/>")+"</inertial>";
}
}

Json robot_options_schema() {
  const Json id={{"type","string"},{"pattern","^[A-Za-z][A-Za-z0-9_-]{0,63}$"}};
  const Json real={{"type","number"},{"minimum",-1e9},{"maximum",1e9}};
  const auto vector=[&](int n){return Json{{"type","array"},{"items",real},{"minItems",n},{"maxItems",n}};};
  const auto property=object({{"mate_id",id},{"coordinate",{{"enum",{"angle_deg","travel_mm"}}}},
    {"effort",{{"type","number"},{"minimum",0},{"maximum",1e9}}},{"velocity",{{"type","number"},{"exclusiveMinimum",0},{"maximum",1e9}}}},
    {"mate_id","coordinate","effort","velocity"});
  const auto inertial=object({{"link",{{"type","string"},{"pattern","^(part_|carrier_)[A-Za-z][A-Za-z0-9_-]{0,63}$"}}},
    {"mass_kg",{{"type","number"},{"exclusiveMinimum",0},{"maximum",1e9}}},{"center_of_mass_m",vector(3)},{"inertia_kg_m2",vector(6)}},
    {"link","mass_kg","center_of_mass_m","inertia_kg_m2"});
  return object({{"format",{{"enum",{"urdf","srdf","sdf"}}}},
    {"joint_properties",{{"type","array"},{"items",property},{"maxItems",126}}},
    {"inertials",{{"type","array"},{"items",inertial},{"maxItems",127}}}}, {"format","joint_properties"});
}
void validate_robot_options(const Json& options) {
  fields(options,{"format","joint_properties"},{"inertials"});
  const auto format=text_field(options,"format");
  if(format!="urdf" && format!="srdf" && format!="sdf") throw Error("invalid_argument","Robot format must be urdf, srdf or sdf");
  const auto& properties=options.at("joint_properties");
  if(!properties.is_array() || properties.size()>126) throw Error("invalid_argument","Robot export permits at most 126 joint property records");
  std::set<std::string> seen;
  for(const auto& item:properties) {
    fields(item,{"mate_id","coordinate","effort","velocity"});model_identifier(text_field(item,"mate_id"));
    const auto coordinate=text_field(item,"coordinate");
    if(coordinate!="angle_deg" && coordinate!="travel_mm") throw Error("invalid_argument","Unknown robot coordinate");
    if(!seen.insert(key(item)).second) throw Error("invalid_argument","Duplicate joint properties");
    if(numeric(item.at("effort"))<0 || numeric(item.at("velocity"))<=0) throw Error("invalid_argument","Effort must be nonnegative and velocity positive, in SI units");
  }
  if(options.contains("inertials")) {
    const auto& inertials=options.at("inertials");
    if(!inertials.is_array() || inertials.size()>127) throw Error("invalid_argument","Robot export permits at most 127 inertials");
    seen.clear();
    for(const auto& item:inertials) {
      fields(item,{"link","mass_kg","center_of_mass_m","inertia_kg_m2"});
      const auto link=text_field(item,"link");
      if(link.starts_with("part_")) model_identifier(link.substr(5));
      else if(link.starts_with("carrier_")) model_identifier(link.substr(8));
      else throw Error("invalid_argument","Robot inertial link must start with part_ or carrier_");
      if(!seen.insert(link).second) throw Error("invalid_argument","Duplicate link inertial");
      if(numeric(item.at("mass_kg"))<=0) throw Error("invalid_argument","Link mass must be positive");
      vector(item.at("center_of_mass_m"),3);check_inertia(item.at("inertia_kg_m2"));
    }
  }
}

Json robot_description(const Json& model,const Json& frames,const Json& options) {
  validate_robot_options(options);
  const bool sdf=options.at("format")=="sdf";
  const Json* assembly=nullptr;
  for(const auto& feature:model.at("features")) if(feature.at("id")==frames.at("feature_id")) assembly=&feature;
  if(!assembly) throw Error("invalid_argument","Robot assembly is missing");
  const auto& parameters=model.at("parameters");
  std::map<std::string,Json> dofs,properties,inertials,couplings;
  std::map<std::string,std::string> joints,meshes;
  for(const auto& item:frames.at("motion").at("dofs")) dofs[key(item)]=item;
  for(const auto& item:options.at("joint_properties")) {
    if(!dofs.contains(key(item))) throw Error("invalid_argument","Joint properties reference an absent coordinate",{{"mate_id",item.at("mate_id")},{"coordinate",item.at("coordinate")}});
    properties[key(item)]=item;
  }
  for(const auto& [id,dof]:dofs) if(!properties.contains(id)) throw Error("invalid_argument","Every moving coordinate requires explicit SI effort and velocity",{{"mate_id",dof.at("mate_id")},{"coordinate",dof.at("coordinate")}});
  std::set<std::string> links;
  for(const auto& item:frames.at("links")) links.insert(text_field(item,"name"));
  for(const auto& item:options.value("inertials",Json::array())) {
    const auto name=text_field(item,"link");
    if(!links.contains(name)) throw Error("invalid_argument","Inertial references an absent robot link",{{"link",name}});
    inertials[name]=item;
  }
  if(sdf) for(const auto& name:links) if(!inertials.contains(name))
    throw Error("invalid_argument","SDF requires an explicit inertial for every part and cylindrical carrier link",{{"link",name}});
  for(const auto& item:frames.at("joints")) if(item.contains("coordinate")) joints[key(item)]=text_field(item,"name");
  for(const auto& item:assembly->value("couplings",Json::array())) couplings[key(item.at("target"))]=item;
  for(const auto& item:frames.at("links")) if(item.contains("input")) meshes.emplace(text_field(item,"input"),"");
  Json mesh_sources=Json::array();int index=0;
  for(auto& [source,path]:meshes) {path="meshes/mesh_"+std::to_string(index++)+".stl";mesh_sources.push_back({{"feature_id",source},{"path",path}});}
  const auto name="robot_"+text_field(frames,"feature_id");
  std::string xml="<?xml version=\"1.0\"?>\n";
  xml+=sdf?"<sdf version=\"1.12\"><model name=\""+name+"\"><static>false</static>":"<robot name=\""+name+"\"><link name=\"world\"/>";
  for(const auto& link:frames.at("links")) {
    const auto lname=text_field(link,"name");xml+="\n<link name=\""+lname+"\">";
    if(sdf) xml+=sdf_pose(link.at("world"),"__model__");
    if(inertials.contains(lname)) xml+=inertia_xml(inertials.at(lname),sdf);
    if(link.contains("input")) for(const auto* kind:{"visual","collision"}) {
      const auto path=meshes.at(text_field(link,"input"));
      xml+="<"+std::string(kind)+(sdf?" name=\""+std::string(kind)+"\"":"")+">";
      if(sdf) xml+=sdf_pose(link.at("mesh_origin"),lname)+"<geometry><mesh><uri>"+path+"</uri><scale>0.001 0.001 0.001</scale></mesh></geometry>";
      else xml+=origin(link.at("mesh_origin"))+"<geometry><mesh filename=\""+path+"\" scale=\"0.001 0.001 0.001\"/></geometry>";
      xml+="</"+std::string(kind)+">";
    }
    xml+="</link>";
  }
  Json coordinates=Json::array();
  for(const auto& joint:frames.at("joints")) {
    const auto jname=text_field(joint,"name"),parent=text_field(joint,"parent"),child=text_field(joint,"child");
    xml+="\n<joint name=\""+jname+"\" type=\""+text_field(joint,"type")+"\">";
    if(sdf) xml+="<parent>"+parent+"</parent><child>"+child+"</child><pose relative_to=\""+child+"\">0 0 0 0 0 0</pose>";
    else xml+="<parent link=\""+parent+"\"/><child link=\""+child+"\"/>"+origin(joint.at("origin"));
    if(joint.contains("coordinate")) {
      const auto k=key(joint);const auto& dof=dofs.at(k);const auto& property=properties.at(k);const double factor=scale(dof),rest=dof.at("value");
      const auto lower=formatted((dof.at("minimum").get<double>()-rest)*factor),upper=formatted((dof.at("maximum").get<double>()-rest)*factor);
      if(sdf) xml+="<axis><xyz>0 0 1</xyz><limit><lower>"+lower+"</lower><upper>"+upper+"</upper><effort>"+formatted(property.at("effort"))+"</effort><velocity>"+formatted(property.at("velocity"))+"</velocity></limit>";
      else xml+="<axis xyz=\"0 0 1\"/><limit lower=\""+lower+"\" upper=\""+upper+"\" effort=\""+formatted(property.at("effort"))+"\" velocity=\""+formatted(property.at("velocity"))+"\"/>";
      Json coordinate={{"joint",jname},{"mate_id",dof.at("mate_id")},{"coordinate",dof.at("coordinate")},{"source_value",rest},{"si_scale",factor},
        {"lower",(dof.at("minimum").get<double>()-rest)*factor},{"upper",(dof.at("maximum").get<double>()-rest)*factor}};
      if(couplings.contains(k)) {
        const auto& coupling=couplings.at(k);const auto source=key(coupling.at("source"));
        const double multiplier=scalar(coupling.at("ratio"),parameters,"dimensionless")*factor/scale(dofs.at(source));
        if(sdf) xml+="<mimic joint=\""+joints.at(source)+"\" axis=\"axis\"><multiplier>"+formatted(multiplier)+"</multiplier><offset>0</offset><reference>0</reference></mimic>";
        else xml+="<mimic joint=\""+joints.at(source)+"\" multiplier=\""+formatted(multiplier)+"\" offset=\"0\"/>";
        coordinate["mimic"]={{"joint",joints.at(source)},{"multiplier",multiplier},{"offset",0}};
      }
      if(sdf) xml+="</axis>";
      coordinates.push_back(std::move(coordinate));
    }
    xml+="</joint>";
  }
  xml+=sdf?"\n</model></sdf>\n":"\n</robot>\n";
  Json poses=Json::object();
  // A group is deliberately the complete mechanism for pose inspection. No
  // end effector, IK chain, controller, collision disable or safety claim is inferred.
  std::string srdf="<?xml version=\"1.0\"?>\n<robot name=\""+name+"\"><group name=\"mechanism\">";
  for(const auto& joint:frames.at("joints")) if(joint.contains("coordinate")) srdf+="<joint name=\""+text_field(joint,"name")+"\"/>";
  if(dofs.empty()) for(const auto& link:frames.at("links")) srdf+="<link name=\""+text_field(link,"name")+"\"/>";
  srdf+="</group>";
  for(const auto& pose:assembly->value("poses",Json::array())) {
    const auto pid=text_field(pose,"id");
    const auto posed=apply_operations(model,Json::array({{{"op","apply_pose"},{"assembly_id",frames.at("feature_id")},{"pose_id",pid}}}));
    const Json* feature=nullptr;for(const auto& item:posed.at("features")) if(item.at("id")==frames.at("feature_id")) feature=&item;
    const auto motion=assembly_motion(*feature,posed.at("parameters"));Json values=Json::object();
    srdf+="<group_state name=\""+pid+"\" group=\"mechanism\">";
    for(const auto& dof:motion.at("dofs")) {
      const auto k=key(dof);const double value=(dof.at("value").get<double>()-dofs.at(k).at("value").get<double>())*scale(dof);
      values[joints.at(k)]=value;
      if(!dof.at("driven").get<bool>()) srdf+="<joint name=\""+joints.at(k)+"\" value=\""+formatted(value)+"\"/>";
    }
    srdf+="</group_state>";poses[pid]=values;
  }
  srdf+="</robot>\n";
  Json files=Json::object();
  if(sdf) files["model.sdf"]=xml;else {files["model.urdf"]=xml;files["model.srdf"]=srdf;}
  const auto path="model."+text_field(options,"format");
  return {{"files",files},{"path",path},{"mesh_sources",mesh_sources},
    {"ledger",{{"schema_version",1},{"robot",name},{"feature_id",frames.at("feature_id")},{"format",options.at("format")},
      {"coordinate_convention","q_export = (q_native - source_value) * si_scale; exported zero reproduces the saved native pose"},
      {"mesh_units","mm"},{"mesh_scale",.001},{"frames_units","mm"},{"frames",frames},{"coordinates",coordinates},{"poses",poses},
      {"joint_properties",options.at("joint_properties")},{"inertials",options.value("inertials",Json::array())},
      {"physical_data","Supplied explicitly by the caller; no density, actuator rating or inertia inferred"},
      {"limitations",Json::array({"Mesh collision geometry is the tessellated CAD shape, not a convex decomposition","No collision checking, controller, end effector or IK configuration is inferred","Consumer support for mimic joints, especially mixed angular/linear couplings, must be checked"})}}}};
}
}
