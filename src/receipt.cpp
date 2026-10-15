#include "agentcad/receipt.hpp"
#include "agentcad/cache.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/model.hpp"
#include <set>

namespace agentcad {
namespace {
Json indexed_features(const Json& model) {
  Json result=Json::object();
  if(!model.is_null())for(const auto& feature:model.at("features"))result[text_field(feature,"id")]=feature;
  return result;
}
Json delta(const Json& before,const Json& after) {
  Json result={{"created",Json::array()},{"modified",Json::array()},{"removed",Json::array()}};
  for(const auto& item:after.items()) {
    if(!before.contains(item.key()))result["created"].push_back(item.key());
    else if(before.at(item.key())!=item.value())result["modified"].push_back(item.key());
  }
  for(const auto& item:before.items())if(!after.contains(item.key()))result["removed"].push_back(item.key());
  return result;
}
// Leaf IDs belong to the output assembly. Preserve the authored ancestor chain,
// so changing a nested placement is visible on every leaf it contains.
Json occurrences(const Json& model,const Json& features) {
  Json result=Json::object();
  if(model.is_null()||features.at(text_field(model,"output")).at("type")!="assembly")return result;
  std::function<void(const std::string&,const std::string&,Json)> visit;
  visit=[&](const std::string& assembly,const std::string& parent,Json chain) {
    for(const auto& part:features.at(assembly).at("parts")) {
      const auto id=parent+text_field(part,"id"),input=text_field(part,"input");
      auto child=chain;child.push_back({{"assembly_id",assembly},{"part",part}});
      if(features.at(input).at("type")=="assembly")visit(input,id+"/",std::move(child));
      else result[id]=std::move(child);
    }
  };
  visit(text_field(model,"output"),"",Json::array());return result;
}
Json names(const Json& model) {
  Json result=Json::array();
  if(!model.is_null())for(const auto& feature:model.at("features"))result.push_back(feature.at("id"));
  return result;
}
}
Json model_changes(const Json& before,const Json& after,const Json& base_revision) {
  const auto a=indexed_features(before),b=indexed_features(after);
  const auto old_keys=before.is_null()?Json::object():feature_cache_keys(before),new_keys=feature_cache_keys(after);
  Json affected=Json::array();std::set<std::string> affected_set;
  for(const auto& item:new_keys.items())if(!old_keys.contains(item.key())||old_keys.at(item.key())!=item.value()) {
    affected.push_back(item.key());affected_set.insert(item.key());
  }
  const auto old_occurrences=occurrences(before,a),new_occurrences=occurrences(after,b);
  Json affected_occurrences=Json::array();
  for(const auto& item:new_occurrences.items()) {
    bool changed=!old_occurrences.contains(item.key())||old_occurrences.at(item.key())!=item.value();
    for(const auto& ancestor:item.value())changed|=affected_set.contains(text_field(ancestor,"assembly_id"))||affected_set.contains(text_field(ancestor.at("part"),"input"));
    if(changed)affected_occurrences.push_back(item.key());
  }
  return {{"base_revision",base_revision},{"features",delta(a,b)},
    {"parameters",delta(before.is_null()?Json::object():before.at("parameters"),after.at("parameters"))},
    {"feature_order_changed",names(before)!=names(after)},
    {"output_changed",before.is_null()||before.at("output")!=after.at("output")},
    {"affected_features",affected},{"occurrences",delta(old_occurrences,new_occurrences)},
    {"affected_occurrences",affected_occurrences}};
}
Json changes_schema() {
  const auto list=[](Json item,std::size_t limit){return Json{{"type","array"},{"items",std::move(item)},{"maxItems",limit},{"uniqueItems",true}};};
  const auto group=[&](Json item,std::size_t limit){auto array=list(std::move(item),limit);return Json{{"type","object"},{"properties",{{"created",array},{"modified",array},{"removed",array}}},{"required",{"created","modified","removed"}},{"additionalProperties",false}};};
  const Json id={{"$ref","#/$defs/model_id"}},boolean={{"type","boolean"}};
  const Json properties={{"base_revision",{{"type",{"integer","null"}},{"minimum",1},{"maximum",9007199254740991ULL}}},
    {"features",group(id,256)},{"parameters",group(id,128)},
    {"feature_order_changed",boolean},{"output_changed",boolean},{"affected_features",list(id,256)},
    {"occurrences",group(occurrence_schema(),assembly_leaf_limit)},
    {"affected_occurrences",list(occurrence_schema(),assembly_leaf_limit)}};
  Json required=Json::array();for(const auto& item:properties.items())required.push_back(item.key());
  return {{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};
}
Json committed_record_receipt(const Store& store,Json record) {
  if(!record.contains("model_sha256")||!record.contains("changes")) {
    const auto revision=revision_number(record.at("revision"));
    const auto model=record.contains("model")?record.at("model"):store.read(text_field(record,"document_id"),revision).at("model");
    if(!record.contains("model_sha256"))record["model_sha256"]=sha256(model.dump());
    if(!record.contains("changes"))record["changes"]=model_changes(revision==1?Json():store.read(text_field(record,"document_id"),revision-1).at("model"),model,revision==1?Json():Json(revision-1));
  }
  record.erase("model");return record;
}
}
