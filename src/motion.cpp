#include "agentcad/model.hpp"
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace agentcad {
namespace {
struct Dof {
  std::string mate,coordinate,unit;
  double initial,minimum,maximum;
  bool explicit_value;
};
struct Coupling {
  std::string id,source,target;
  double ratio,offset;
};
std::string key(const std::string& mate,const std::string& coordinate) {return mate+"."+coordinate;}
std::string reference(const Json& value) {
  fields(value,{"mate_id","coordinate"});
  const auto mate=text_field(value,"mate_id"),coordinate=text_field(value,"coordinate");model_identifier(mate);
  if (coordinate!="angle_deg" && coordinate!="travel_mm") throw Error("invalid_model","Motion coordinate must be angle_deg or travel_mm");
  return key(mate,coordinate);
}
}
Json assembly_motion(const Json& feature,const Json& parameters) {
  std::map<std::string,Dof> dofs;
  std::vector<std::string> order;
  if (feature.contains("mates")) for (const auto& mate:feature.at("mates")) {
    const auto id=text_field(mate,"id"),type=text_field(mate,"type");
    for (const auto* coordinate:{"angle_deg","travel_mm"}) {
      const bool angular=std::string(coordinate)=="angle_deg";
      if (angular ? type!="revolute" && type!="cylindrical" : type!="slider" && type!="cylindrical") continue;
      const auto limits=angular?"angle_limits_deg":"travel_limits_mm",unit=angular?"deg":"mm";
      if (!mate.contains(limits) || !mate.at(limits).is_array() || mate.at(limits).size()!=2)
        throw Error("invalid_model",std::string("Moving mate requires ")+limits+" with [minimum, maximum]",{{"mate_id",id}});
      const double minimum=scalar(mate.at(limits)[0],parameters,unit),maximum=scalar(mate.at(limits)[1],parameters,unit);
      if (minimum>maximum) throw Error("invalid_model","Joint minimum exceeds maximum",{{"mate_id",id},{"coordinate",coordinate}});
      const auto k=key(id,coordinate);order.push_back(k);
      dofs.emplace(k,Dof{id,coordinate,unit,scalar(mate.value(coordinate,Json(0)),parameters,unit),minimum,maximum,mate.contains(coordinate)});
    }
  }
  std::map<std::string,Coupling> drivers;
  if (feature.contains("couplings")) {
    const auto& couplings=feature.at("couplings");
    if (!couplings.is_array() || couplings.size()>126) throw Error("invalid_model","An assembly permits at most 126 couplings");
    std::set<std::string> ids;
    for (const auto& coupling:couplings) {
      const auto id=text_field(coupling,"id");
      try {
        fields(coupling,{"id","source","target","ratio"},{"offset"});model_identifier(id);
        if (!ids.insert(id).second) throw Error("invalid_model","Duplicate coupling ID");
        const auto source=reference(coupling.at("source")),target=reference(coupling.at("target"));
        if (!dofs.contains(source) || !dofs.contains(target)) throw Error("invalid_model","Coupling must reference existing moving coordinates");
        if (source==target) throw Error("invalid_model","A coordinate cannot drive itself");
        if (drivers.contains(target)) throw Error("invalid_model","A coordinate permits only one coupling driver");
        if (dofs.at(target).explicit_value) throw Error("invalid_model","A coupled coordinate cannot also have an authored value",{{"mate_id",dofs.at(target).mate},{"coordinate",dofs.at(target).coordinate}});
        const double ratio=scalar(coupling.at("ratio"),parameters,"dimensionless");
        if (std::abs(ratio)<1e-12) throw Error("invalid_model","Coupling ratio must be nonzero");
        drivers.emplace(target,Coupling{id,source,target,ratio,scalar(coupling.value("offset",Json(0)),parameters,dofs.at(target).unit)});
      } catch (const Error& e) {auto details=e.details;details["coupling_id"]=id;throw Error(e.code,e.what(),details);}
    }
  }
  const auto evaluate=[&](const std::map<std::string,double>& overrides) {
    std::map<std::string,double> values;std::set<std::string> visiting;
    std::function<double(const std::string&)> resolve=[&](const std::string& k) -> double {
      if (values.contains(k)) return values.at(k);
      const auto& dof=dofs.at(k);
      if (!visiting.insert(k).second) throw Error("invalid_model","Motion coupling graph contains a cycle",{{"mate_id",dof.mate},{"coordinate",dof.coordinate}});
      double value=overrides.contains(k)?overrides.at(k):dof.initial;
      if (drivers.contains(k)) {
        const auto& driver=drivers.at(k);value=resolve(driver.source)*driver.ratio+driver.offset;
      }
      if (!std::isfinite(value) || std::abs(value)>1e6 || value<dof.minimum || value>dof.maximum)
        throw Error("invalid_model","Joint coordinate exceeds its limits",{{"mate_id",dof.mate},{"coordinate",dof.coordinate},{"value",value},{"minimum",dof.minimum},{"maximum",dof.maximum}});
      visiting.erase(k);values.emplace(k,value);return value;
    };
    for (const auto& k:order) resolve(k);
    return values;
  };
  const auto values=evaluate({});Json poses=Json::array();
  if (feature.contains("poses")) {
    const auto& declared=feature.at("poses");
    if (!declared.is_array() || declared.size()>64) throw Error("invalid_model","An assembly permits at most 64 named poses");
    std::set<std::string> ids;
    for (const auto& pose:declared) {
      const auto id=text_field(pose,"id");
      try {
        fields(pose,{"id","values"});model_identifier(id);
        if (!ids.insert(id).second) throw Error("invalid_model","Duplicate pose ID");
        const auto& supplied=pose.at("values");
        if (!supplied.is_array() || supplied.size()>126) throw Error("invalid_model","A pose permits at most 126 coordinate values");
        std::map<std::string,double> overrides;
        for (const auto& item:supplied) {
          fields(item,{"mate_id","coordinate","value"});
          const auto k=reference({{"mate_id",item.at("mate_id")},{"coordinate",item.at("coordinate")}});
          if (!dofs.contains(k)) throw Error("invalid_model","Pose references an absent motion coordinate");
          if (drivers.contains(k)) throw Error("invalid_model","Pose cannot override a coupled coordinate");
          if (!overrides.emplace(k,scalar(item.at("value"),parameters,dofs.at(k).unit)).second)
            throw Error("invalid_model","Pose repeats a coordinate");
        }
        if (overrides.size()+drivers.size()!=dofs.size()) throw Error("invalid_model","Named pose must specify every independent coordinate");
        evaluate(overrides);poses.push_back(id);
      } catch (const Error& e) {auto details=e.details;details["pose_id"]=id;throw Error(e.code,e.what(),details);}
    }
  }
  Json result={{"dofs",Json::array()},{"poses",poses}};
  for (const auto& k:order) {
    const auto& dof=dofs.at(k);
    Json item={{"mate_id",dof.mate},{"coordinate",dof.coordinate},{"unit",dof.unit},{"value",values.at(k)},
      {"minimum",dof.minimum},{"maximum",dof.maximum},{"driven",drivers.contains(k)}};
    if (drivers.contains(k)) item["coupling_id"]=drivers.at(k).id;
    result["dofs"].push_back(std::move(item));
  }
  return result;
}
Json assembly_mechanisms(const Json& model,const std::string& assembly_id) {
  const auto tree=assembly_structure(model,assembly_id);
  std::map<std::string,const Json*> definitions;
  for (const auto& feature:model.at("features")) definitions.emplace(text_field(feature,"id"),&feature);
  std::map<std::string,Json> occurrences;
  std::vector<std::string> order;
  const auto append=[&](const std::string& id,const std::string& path) {
    if (!occurrences.contains(id)) {occurrences[id]=Json::array();order.push_back(id);}
    occurrences[id].push_back(path);
  };
  append(assembly_id,"");
  for (const auto& node:tree) if (node.at("kind")=="assembly") append(text_field(node,"input"),text_field(node,"id"));
  Json result=Json::array();
  for (const auto& id:order) {
    auto motion=assembly_motion(*definitions.at(id),model.at("parameters"));
    if (!motion.at("dofs").empty()) result.push_back({{"assembly_id",id},{"occurrences",occurrences.at(id)},{"motion",std::move(motion)}});
  }
  return result;
}
}
