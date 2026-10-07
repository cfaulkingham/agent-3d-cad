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
  for (const auto& part:assembly->at("parts")) groups[text_field(part,"input")].push_back(text_field(part,"id"));
  std::map<std::string,Json> metadata;
  std::set<int> reserved;
  if (assembly->contains("bom")) for (const auto& item:assembly->at("bom")) {
    metadata.emplace(text_field(item,"input"),item);
    if (item.contains("item_number")) reserved.insert(item.at("item_number").get<int>());
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
    for (const auto* key:{"part_number","description","material"}) if (data.contains(key)) item[key]=data.at(key);
    items.push_back(std::move(item));
  }
  std::sort(items.begin(),items.end(),[](const Json& a,const Json& b){return a.at("item_number").get<int>()<b.at("item_number").get<int>();});
  return {{"assembly_id",selected},{"items",items},{"total_quantity",assembly->at("parts").size()}};
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
  constexpr std::array<bool,7> text_columns={false,true,false,true,true,true,true};
  std::string result="\"item_number\",\"input\",\"quantity\",\"part_number\",\"description\",\"material\",\"part_ids\"\r\n";
  for (const auto& item:bom.at("items")) {
    std::string ids;
    for (const auto& id:item.at("part_ids")) {if (!ids.empty()) ids+=';';ids+=id.get<std::string>();}
    const std::vector<std::string> values={std::to_string(item.at("item_number").get<int>()),text_field(item,"input"),
      std::to_string(item.at("quantity").get<int>()),item.value("part_number",std::string()),
      item.value("description",std::string()),item.value("material",std::string()),ids};
    for (std::size_t i=0;i<values.size();++i) {if (i) result+=',';result+=quote(values[i],text_columns[i]);}
    result+="\r\n";
  }
  return result;
}
}
