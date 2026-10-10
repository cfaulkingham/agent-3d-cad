#include "agentcad/component.hpp"
#include "agentcad/authoring.hpp"
#include "agentcad/model.hpp"
#include "agentcad/hash.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace agentcad {
namespace {
Json object(Json properties,Json required) {
  return {{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};
}
std::string local_name(const std::string& kind,const std::string& component,const std::string& source) {
  const auto name=kind+"_"+std::to_string(component.size())+"_"+component+"_"+std::to_string(source.size())+"_"+source;
  if(name.size()<=64)return name;
  return kind+"_"+component.substr(0,12)+"_"+source.substr(0,12)+"_"+sha256(name).substr(0,32);
}
std::set<std::string> dependencies(const Json& feature) {
  std::set<std::string> result;
  for(const auto* key:{"input","left","right","target"})if(feature.contains(key))result.insert(text_field(feature,key));
  if(feature.contains("sections"))for(const auto& id:feature.at("sections"))result.insert(id.get<std::string>());
  if(feature.at("type")=="assembly")for(const auto& part:feature.at("parts"))result.insert(text_field(part,"input"));
  return result;
}
void parameter_names(const Json& value,std::set<std::string>& names) {
  if(value.is_object()) {
    if(value.contains("parameter"))names.insert(text_field(value,"parameter"));
    else for(const auto& item:value.items())parameter_names(item.value(),names);
  } else if(value.is_array())for(const auto& item:value)parameter_names(item,names);
}
Json rewrite_parameters(const Json& value,const Json& replacements,std::size_t& remaining) {
  const auto charge=[&](std::size_t size){if(size>remaining)throw Error("limit_exceeded","Materialized component exceeds the document byte budget");remaining-=size;};
  if(value.is_object() && value.contains("parameter")) {
    const auto& result=replacements.at(text_field(value,"parameter"));charge(result.dump().size());return result;
  }
  if(value.is_object()) {
    charge(2);Json result=Json::object();
    for(const auto& item:value.items()) {
      charge(item.key().size()+4);
      if(((value.value("type",Json())=="import_step" && item.key()=="content")||authoring_source_field(value,item.key())) && item.value().is_string()) {
        charge(2);result[item.key()]=item.value();
      } else result[item.key()]=rewrite_parameters(item.value(),replacements,remaining);
    }
    return result;
  }
  if(value.is_array()) {
    charge(2+value.size());Json result=Json::array();
    for(const auto& item:value)result.push_back(rewrite_parameters(item,replacements,remaining));return result;
  }
  charge(value.dump().size());return value;
}
void check_bindings(const Json& bindings,const Json& parameters) {
  if(!bindings.is_object()||bindings.size()>128)throw Error("invalid_argument","Component bindings must be an object with at most 128 entries");
  for(const auto& item:bindings.items()) {
    model_identifier(item.key());const auto& value=item.value();
    const auto unit=value.is_object()&&value.contains("expression")?text_field(value.at("expression"),"unit"):"mm";
    if(unit!="mm"&&unit!="deg"&&unit!="rad"&&unit!="dimensionless")throw Error("invalid_model","Unknown component binding unit");
    scalar(value,parameters,unit);
  }
}
struct Materialized {
  Json features=Json::array(),parameters=Json::object(),feature_map=Json::object(),parameter_map=Json::object();
};
Materialized materialize(const Json& component) {
  const auto id=text_field(component,"id"),output=text_field(component.at("source"),"feature_id");
  const auto& snapshot=component.at("snapshot");const auto& bindings=component.at("bindings");
  if(!bindings.is_object() || bindings.size()>128)throw Error("invalid_model","Component bindings must be an object with at most 128 entries");
  std::map<std::string,const Json*> source;
  for(const auto& feature:snapshot.at("features"))source.emplace(text_field(feature,"id"),&feature);
  if(!source.contains(output)||is_sketch_feature_type(source.at(output)->at("type").get<std::string>()))
    throw Error("invalid_model","Component source feature must be solid geometry or an assembly",{{"component_id",id},{"source_feature_id",output}});
  std::set<std::string> selected,parameters;
  std::function<void(const std::string&)> visit=[&](const std::string& name) {
    if(!selected.insert(name).second)return;
    for(const auto& input:dependencies(*source.at(name)))visit(input);
    parameter_names(*source.at(name),parameters);
  };
  visit(output);Materialized result;Json replacements=Json::object();
  for(const auto& name:selected)result.feature_map[name]=name==output?id:local_name("cf",id,name);
  for(const auto& binding:bindings.items())if(!parameters.contains(binding.key()))
    throw Error("invalid_argument","Binding does not name a parameter used by the selected source feature",{{"component_id",id},{"parameter",binding.key()}});
  for(const auto& name:parameters) {
    if(bindings.contains(name))replacements[name]=bindings.at(name);
    else {
      const auto local=local_name("cp",id,name);result.parameter_map[name]=local;
      result.parameters[local]=snapshot.at("parameters").at(name);replacements[name]={{"parameter",local}};
    }
  }
  std::size_t remaining=max_json_bytes;
  for(const auto& feature:snapshot.at("features")) {
    const auto original=text_field(feature,"id");if(!selected.contains(original))continue;
    auto copy=rewrite_parameters(feature,replacements,remaining);copy["id"]=result.feature_map.at(original);
    for(const auto* key:{"input","left","right","target"})if(copy.contains(key))copy[key]=result.feature_map.at(text_field(copy,key));
    if(copy.contains("sections"))for(auto& section:copy["sections"])section=result.feature_map.at(section.get<std::string>());
    for(const auto* key:{"edges","vertices"})if(copy.contains(key)&&copy.at(key).is_object())copy[key]["feature_id"]=result.feature_map.at(text_field(copy.at(key),"feature_id"));
    if(copy.contains("faces")) {
      auto rewrite_face=[&](Json& selector){selector["feature_id"]=result.feature_map.at(text_field(selector,"feature_id"));};
      if(copy["faces"].is_array())for(auto& selector:copy["faces"])rewrite_face(selector);
      else rewrite_face(copy["faces"]);
    }
    if(copy.at("type")=="assembly") {
      for(auto& part:copy["parts"])part["input"]=result.feature_map.at(text_field(part,"input"));
      if(copy.contains("bom"))for(auto& item:copy["bom"])item["input"]=result.feature_map.at(text_field(item,"input"));
    }
    result.features.push_back(std::move(copy));
  }
  return result;
}
Json changes(const Json& model,const Materialized& baseline) {
  Json result={{"features",Json::array()},{"parameters",Json::array()}};
  std::map<std::string,const Json*> current;
  for(const auto& feature:model.at("features"))current.emplace(text_field(feature,"id"),&feature);
  for(const auto& feature:baseline.features) {
    const auto id=text_field(feature,"id");
    if(!current.contains(id)||*current.at(id)!=feature)result["features"].push_back(id);
  }
  for(const auto& parameter:baseline.parameters.items())
    if(!model.at("parameters").contains(parameter.key())||model.at("parameters").at(parameter.key())!=parameter.value())result["parameters"].push_back(parameter.key());
  return result;
}
bool modified(const Json& changed) {return !changed.at("features").empty()||!changed.at("parameters").empty();}
}

Json component_definitions() {
  const Json id={{"type","string"},{"pattern","^[A-Za-z][A-Za-z0-9_-]{0,63}$"}};
  const Json revision={{"type","integer"},{"minimum",1},{"maximum",9007199254740991ULL}};
  const Json bindings={{"type","object"},{"propertyNames",id},{"additionalProperties",{{"$ref","#/$defs/scalar"}}},{"maxProperties",128}};
  const Json mapping={{"type","object"},{"propertyNames",id},{"additionalProperties",id},{"maxProperties",256}};
  const auto source=object({{"document_id",id},{"revision",revision},{"feature_id",id},{"kernel_version",{{"const","8.0.1"}}}},
    {"document_id","revision","feature_id","kernel_version"});
  const auto component=object({{"id",id},{"source",source},{"snapshot",{{"$ref","#/$defs/model"}}},
    {"sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},{"bindings",bindings},{"feature_map",mapping},{"parameter_map",mapping}},
    {"id","source","snapshot","sha256","bindings","feature_map","parameter_map"});
  const auto set=object({{"op",{{"const","set_component"}}},{"id",id},{"source_document_id",id},{"source_revision",revision},
    {"source_feature_id",id},{"bindings",bindings},{"discard_local_changes",{{"type","boolean"}}}},
    {"op","id","source_document_id","source_revision"});
  const auto status=object({{"id",id},{"source",source},{"sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},
    {"modified",{{"type","boolean"}}},{"changes",object({{"features",{{"type","array"},{"items",id},{"maxItems",256}}},
      {"parameters",{{"type","array"},{"items",id},{"maxItems",128}}}},{"features","parameters"})}},
    {"id","source","sha256","modified","changes"});
  return {{"component",component},{"component_status",status},{"set_component_operation",set},
    {"detach_component_operation",object({{"op",{{"const","detach_component"}}},{"id",id}},{"op","id"})},
    {"remove_component_operation",object({{"op",{{"const","remove_component"}}},{"id",id}},{"op","id"})}};
}

void validate_components(const Json& model) {
  if(!model.contains("components"))return;
  const auto& components=model.at("components");
  if(!components.is_array()||components.size()>64)throw Error("invalid_model","A document permits at most 64 pinned components");
  if(components.empty())return;
  // The input byte budget and this depth cap bound recursive provenance work.
  static thread_local unsigned depth=0;
  if(depth>=4)throw Error("limit_exceeded","Component snapshot provenance exceeds four levels");
  ++depth;struct Depth{unsigned& value;~Depth(){--value;}} reset{depth};
  std::set<std::string> ids,owned_features,owned_parameters,features;
  for(const auto& feature:model.at("features"))features.insert(text_field(feature,"id"));
  for(const auto& component:components) {
    const auto id=text_field(component,"id");
    try {
      fields(component,{"id","source","snapshot","sha256","bindings","feature_map","parameter_map"});
      model_identifier(id);if(!ids.insert(id).second)throw Error("invalid_model","Duplicate component ID");
      const auto& source=component.at("source");fields(source,{"document_id","revision","feature_id","kernel_version"});
      identifier(text_field(source,"document_id"));revision_number(source.at("revision"));model_identifier(text_field(source,"feature_id"));
      if(text_field(source,"kernel_version")!="8.0.1")throw Error("kernel_mismatch","Component source requires OpenCascade 8.0.1");
      if(sha256(component.at("snapshot").dump())!=text_field(component,"sha256"))throw Error("invalid_model","Component snapshot checksum mismatch");
      validate_model(component.at("snapshot"));
      const auto baseline=materialize(component);
      if(component.at("feature_map")!=baseline.feature_map||component.at("parameter_map")!=baseline.parameter_map)
        throw Error("invalid_model","Component identity maps do not match the captured source");
      for(const auto& name:baseline.feature_map.items()) {
        const auto target=name.value().get<std::string>();
        if(!features.contains(target)||!owned_features.insert(target).second)throw Error("invalid_model","Component features must exist and have one owner",{{"feature_id",target}});
      }
      for(const auto& name:baseline.parameter_map.items()) {
        const auto target=name.value().get<std::string>();
        if(!model.at("parameters").contains(target)||!owned_parameters.insert(target).second)throw Error("invalid_model","Component parameters must exist and have one owner",{{"parameter",target}});
      }
      // Bindings must remain well-formed even after local feature edits remove
      // every occurrence of a parameter. Actual usage also validates its unit.
      check_bindings(component.at("bindings"),model.at("parameters"));
    } catch(const Error& e) {auto details=e.details;details["component_id"]=id;throw Error(e.code,e.what(),details);}
  }
}

Json component_status(const Json& model) {
  Json result=Json::array();
  for(const auto& component:model.value("components",Json::array())) {
    const auto changed=changes(model,materialize(component));
    result.push_back({{"id",component.at("id")},{"source",component.at("source")},{"sha256",component.at("sha256")},
      {"modified",modified(changed)},{"changes",changed}});
  }
  return result;
}

bool apply_component_operation(Json& candidate,const Json& operation,const ComponentResolver& resolve) {
  const auto op=text_field(operation,"op");
  if(op!="set_component"&&op!="detach_component"&&op!="remove_component")return false;
  if(op=="set_component")fields(operation,{"op","id","source_document_id","source_revision"},{"source_feature_id","bindings","discard_local_changes"});
  else fields(operation,{"op","id"});
  const auto id=text_field(operation,"id");model_identifier(id);
  if(!candidate.contains("components"))candidate["components"]=Json::array();
  auto& components=candidate["components"];
  auto found=std::find_if(components.begin(),components.end(),[&](const Json& item){return item.at("id")==id;});
  const Json previous=found==components.end()?Json():*found;
  if(op!="set_component"&&previous.is_null())throw Error("invalid_argument","Unknown component",{{"component_id",id}});
  if(op=="detach_component") {components.erase(found);return true;}
  Json replacement;Materialized imported;
  if(op=="set_component") {
    if(operation.contains("discard_local_changes")&&!operation.at("discard_local_changes").is_boolean())throw Error("invalid_argument","discard_local_changes must be boolean");
    if(!previous.is_null()) {
      const auto changed=changes(candidate,materialize(previous));
      if(modified(changed)&&!operation.value("discard_local_changes",false))
        throw Error("component_modified","Component has local edits; preserve them or explicitly discard them when updating",{{"component_id",id},{"changes",changed}});
    }
    if(!resolve)throw Error("invalid_argument","Capturing a component requires the service's saved-document resolver");
    const auto document=text_field(operation,"source_document_id");identifier(document);
    const auto revision=revision_number(operation.at("source_revision"));
    const auto record=resolve(document,revision);
    if(record.at("document_id")!=document||record.at("revision")!=revision)throw Error("storage_error","Component source did not match its requested revision");
    const auto& snapshot=record.at("model");validate_model(snapshot);
    replacement={{"id",id},{"source",{{"document_id",document},{"revision",revision},{"kernel_version",record.at("kernel_version")},
      {"feature_id",operation.value("source_feature_id",text_field(snapshot,"output"))}}},{"snapshot",snapshot},{"sha256",sha256(snapshot.dump())},
      {"bindings",operation.value("bindings",previous.is_null()?Json::object():previous.at("bindings"))}};
    imported=materialize(replacement);replacement["feature_map"]=imported.feature_map;replacement["parameter_map"]=imported.parameter_map;
  }
  std::set<std::string> old_features,old_parameters;
  if(!previous.is_null()) {
    for(const auto& item:previous.at("feature_map").items())old_features.insert(item.value().get<std::string>());
    for(const auto& item:previous.at("parameter_map").items())old_parameters.insert(item.value().get<std::string>());
  }
  std::size_t insertion=candidate.at("features").size();Json retained=Json::array();
  std::set<std::string> remaining;
  for(const auto& feature:candidate.at("features")) {
    const auto name=text_field(feature,"id");
    if(old_features.contains(name))insertion=std::min(insertion,retained.size());
    else {retained.push_back(feature);remaining.insert(name);}
  }
  for(const auto& name:old_parameters)candidate["parameters"].erase(name);
  for(const auto& feature:imported.features)if(!remaining.insert(text_field(feature,"id")).second)
    throw Error("invalid_argument","Component feature identity collides with an existing feature",{{"component_id",id},{"feature_id",feature.at("id")}});
  for(const auto& item:imported.parameters.items()) {
    if(candidate.at("parameters").contains(item.key()))throw Error("invalid_argument","Component parameter identity collides with an existing parameter",{{"component_id",id},{"parameter",item.key()}});
    candidate["parameters"][item.key()]=item.value();
  }
  insertion=std::min(insertion,retained.size());
  retained.insert(retained.begin()+static_cast<Json::difference_type>(insertion),imported.features.begin(),imported.features.end());
  candidate["features"]=std::move(retained);
  if(op=="remove_component")components.erase(found);
  else if(found==components.end())components.push_back(std::move(replacement));
  else *found=std::move(replacement);
  return true;
}
}
