#include "agentcad/bom.hpp"
#include "agentcad/model.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <string_view>
#include <vector>

namespace agentcad {
Json build_bom(const Json& model, const std::string& assembly_id) {
  validate_model(model);
  const auto selected=assembly_id.empty()?text_field(model,"output"):assembly_id;
  const Json* assembly=nullptr;
  for (const auto& feature:model.at("features"))
    if (feature.at("id")==selected) {assembly=&feature;break;}
  if (!assembly || assembly->at("type")!="assembly")
    throw Error("invalid_argument","BOM feature must name an assembly",{{"feature_id",selected}});
  std::map<std::string,std::vector<std::string>> groups;
  auto structure=assembly_structure(model,selected);
  std::map<std::string,const Json*> features;
  for (const auto& feature:model.at("features")) features.emplace(text_field(feature,"id"),&feature);
  std::map<std::string,Json> metadata;
  std::set<int> reserved;
  std::size_t total=0;bool nested=false;
  for (auto& node:structure) {
    const auto& owner=*features.at(text_field(node,"assembly_id"));
    if (owner.contains("bom")) for (const auto& item:owner.at("bom"))
      if (item.at("input")==node.at("input")) node["bom"]=item;
    if (node.at("kind")=="assembly") {nested=true;continue;}
    ++total;
    const auto input=text_field(node,"input");
    groups[input].push_back(text_field(node,"id"));
    if(const auto* source=imported_step_source(model,input);source&&source->contains("purchase")) {
      metadata[input]["purchase"]=source->at("purchase");
      if(!node.contains("bom"))node["bom"]={{"input",input}};
      node["bom"]["purchase"]=source->at("purchase");
    }
    if (owner.contains("bom")) for (const auto& item:owner.at("bom")) if (item.at("input")==input) {
      auto& data=metadata[input];
      for (const auto* key:{"part_number","description","material","purchase"}) if (item.contains(key)) {
        if (data.contains(key) && data.at(key)!=item.at(key))
          throw Error("invalid_argument","Conflicting BOM metadata for a shared source; use distinct source features for distinct parts",{{"source_feature_id",input},{"field",key}});
        data[key]=item.at(key);
      }
      // Child item numbers belong to their own BOM table. Only explicitly
      // numbered root leaves reserve numbers in this assembly's rolled-up BOM.
      if (owner.at("id")==selected && item.contains("item_number")) {
        data["item_number"]=item.at("item_number");reserved.insert(item.at("item_number").get<int>());
      }
    }
  }
  Json items=Json::array();int next=1;
  for (auto& [input,parts]:groups) {
    std::sort(parts.begin(),parts.end());
    const auto found=metadata.find(input);
    const auto data=found==metadata.end()?Json::object():found->second;
    int number;
    if (data.contains("item_number")) number=data.at("item_number").get<int>();
    else {
      while (reserved.contains(next)) ++next;
      number=next++;
    }
    Json item={{"item_number",number},{"input",input},{"quantity",parts.size()},{"part_ids",parts}};
    for (const auto* key:{"part_number","description","material","purchase"}) if (data.contains(key)) item[key]=data.at(key);
    items.push_back(std::move(item));
  }
  std::sort(items.begin(),items.end(),[](const Json& a,const Json& b){return a.at("item_number").get<int>()<b.at("item_number").get<int>();});
  Json result={{"assembly_id",selected},{"items",items},{"total_quantity",total}};
  if (nested) result["structure"]=structure;
  return result;
}

std::string bom_csv(const Json& bom) {
  auto quote=[](const std::string& value,bool text) {
    std::string result="\"";
    // Spreadsheets evaluate a cell starting with one of these as a formula
    // (CSV injection). A leading apostrophe keeps free-text cells literal.
    if (text && !value.empty() && std::string_view("=+-@\t\r").find(value.front())!=std::string_view::npos) result+='\'';
    for (const auto c:value) {if (c=='\"') result+='\"';result+=c;}
    return result+'\"';
  };
  // item_number and quantity are service-generated integers; never prefix them.
  constexpr std::array<bool,11> text_columns={false,true,false,true,true,true,true,true,true,true,true};
  std::string result="\"item_number\",\"input\",\"quantity\",\"part_number\",\"description\",\"material\",\"part_ids\",\"supplier\",\"supplier_part_number\",\"source_url\",\"artifact_sha256\"\r\n";
  for (const auto& item:bom.at("items")) {
    std::string ids;
    for (const auto& id:item.at("part_ids")) {if (!ids.empty()) ids+=';';ids+=id.get<std::string>();}
    const auto purchase=item.value("purchase",Json::object());
    const std::vector<std::string> values={std::to_string(item.at("item_number").get<int>()),text_field(item,"input"),
      std::to_string(item.at("quantity").get<int>()),item.value("part_number",std::string()),
      item.value("description",std::string()),item.value("material",std::string()),ids,purchase.value("supplier",std::string()),
      purchase.value("part_number",std::string()),purchase.value("source_url",std::string()),purchase.value("artifact_sha256",std::string())};
    for (std::size_t i=0;i<values.size();++i) {if (i) result+=',';result+=quote(values[i],text_columns[i]);}
    result+="\r\n";
  }
  return result;
}
}
