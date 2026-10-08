#include "agentcad/model.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value, const std::string& message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
void error(const std::string& code, const std::function<void()>& run, const std::string& feature="") {
  try { run(); }
  catch (const Error& e) {
    require(e.code == code, "Expected " + code + ", got " + e.code + ": " + e.what());
    if (!feature.empty()) require(e.details.value("feature_id","") == feature, "Error identifies assembly feature");
    return;
  }
  throw std::runtime_error("Expected " + code);
}
Json frame() { return {{"origin",{0,0,0}},{"normal",{0,0,1}},{"x_direction",{1,0,0}}}; }
Json mate(const std::string& id="joint", const std::string& parent="base", const std::string& child="lid") {
  return {{"id",id},{"type","rigid"},{"parent",parent},{"child",child},{"parent_frame",frame()},{"child_frame",frame()},
    {"offset",Json::array({0,0,Json{{"parameter","gap"}}})},{"angle_deg",Json{{"parameter","angle"}}}};
}
Json model() {
  return {{"schema_version",1},{"units","mm"},{"parameters",{{"gap",5},{"angle",90}}},
    {"features",Json::array({
      {{"id","block"},{"type","box"},{"size",{10,20,4}}},
      {{"id","assembly"},{"type","assembly"},{"parts",Json::array({
        {{"id","base"},{"input","block"},{"placement",{{"translation",{3,4,5}}}}},
        {{"id","lid"},{"input","block"}}})},{"mates",Json::array({mate()})}}
    })},{"output","assembly"}};
}
void validation_tests() {
  auto source=model(); validate_model(source); ++checks;
  auto independent=source; independent["features"][1].erase("mates"); validate_model(independent); ++checks;
  independent["features"][1]["parts"][1]["placement"]=Json::object(); validate_model(independent); ++checks;
  auto changed=source; changed["features"][1]["parts"][1]["id"]="base";
  error("invalid_model",[&]{validate_model(changed);},"assembly");
  changed=source; changed["features"][1]["parts"][1]["input"]="assembly";
  error("invalid_model",[&]{validate_model(changed);},"assembly");
  changed=source; changed["features"][1]["parts"][1]["placement"]=Json::object();
  error("invalid_model",[&]{validate_model(changed);},"assembly");
  changed=source; changed["features"][1]["parts"]=Json::array();
  error("invalid_model",[&]{validate_model(changed);},"assembly");
  changed=source; changed["features"][1]["mates"][0]["child"]="absent";
  error("invalid_model",[&]{validate_model(changed);},"assembly");
  changed=source; changed["features"][1]["mates"][0]["child"]="base";
  error("invalid_model",[&]{validate_model(changed);},"assembly");
  changed=source; changed["features"][1]["mates"].push_back(mate("second"));
  error("invalid_model",[&]{validate_model(changed);},"assembly");
  changed=source; changed["features"][1]["mates"].push_back(mate());
  error("invalid_model",[&]{validate_model(changed);},"assembly");
  changed=source; changed["features"][1]["mates"][0]["type"]="spherical";
  error("invalid_model",[&]{validate_model(changed);},"assembly");
  changed=source; changed["features"][1]["parts"][0].erase("placement");
  changed["features"][1]["parts"].push_back({{"id","third"},{"input","block"}});
  changed["features"][1]["mates"].push_back(mate("second","lid","third"));
  changed["features"][1]["mates"].push_back(mate("cycle","third","base"));
  error("invalid_model",[&]{validate_model(changed);},"assembly");
  changed["features"][1]["mates"].erase(0);
  validate_model(changed); ++checks; // Parts and mate edges need no topological ordering.
  for (const auto* key : {"parent_frame","child_frame"}) {
    changed=source; changed["features"][1]["mates"][0][key]["normal"]={0,0,0};
    error("invalid_model",[&]{validate_model(changed);},"assembly");
    changed=source; changed["features"][1]["mates"][0][key]["x_direction"]={1,0,1};
    error("invalid_model",[&]{validate_model(changed);},"assembly");
  }
  changed=source; changed["features"][1]["mates"][0]["angle_deg"]={{"expression",{{"op","add"},{"args",{1,2}},{"unit","mm"}}}};
  error("invalid_model",[&]{validate_model(changed);},"assembly");
  changed=source; changed["features"][1]["mates"][0]["offset"][0]={{"expression",{{"op","add"},{"args",{1,2}},{"unit","deg"}}}};
  error("invalid_model",[&]{validate_model(changed);},"assembly");
  changed=source; changed["features"][1]["parts"][0]["placement"]["rotation"]={{"origin",{0,0,0}},{"axis",{0,0,0}},{"angle_deg",45}};
  error("invalid_model",[&]{validate_model(changed);},"assembly");
  changed=source; changed["features"][1]["parts"][0]["placement"]["unknown"]=1;
  error("invalid_argument",[&]{validate_model(changed);},"assembly");
  changed=source; changed["features"].push_back({{"id","nested"},{"type","assembly"},{"parts",Json::array({{{"id","inner"},{"input","assembly"}}})}});
  error("invalid_model",[&]{validate_model(changed);},"nested");
  for (const Json feature : Json::array({
    {{"id","after"},{"type","transform"},{"input","assembly"}},
    {{"id","after"},{"type","instance"},{"input","assembly"}},
    {{"id","after"},{"type","pattern"},{"input","assembly"},{"count",2},{"step",{10,0,0}}},
    {{"id","after"},{"type","fillet"},{"input","assembly"},{"radius",1},{"edges","all"}},
    {{"id","after"},{"type","cut"},{"left","assembly"},{"right","block"}},
    {{"id","after"},{"type","fuse"},{"left","block"},{"right","assembly"}},
    {{"id","after"},{"type","hole"},{"input","assembly"},{"origin",{0,0,0}},{"axis",{0,0,1}},{"radius",1},{"depth",2}}
  })) {
    changed=source; changed["features"].push_back(feature);
    error("invalid_model",[&]{validate_model(changed);},"after");
  }
  changed=source; auto& large=changed["features"][1]; large["parts"]=Json::array(); large["mates"]=Json::array();
  for (int i=0;i<64;++i) {
    large["parts"].push_back({{"id","p"+std::to_string(i)},{"input","block"}});
    if (i) large["mates"].push_back(mate("m"+std::to_string(i),"p"+std::to_string(i-1),"p"+std::to_string(i)));
  }
  validate_model(changed); ++checks;
  auto too_large=changed; too_large["features"][1]["parts"].push_back({{"id","p64"},{"input","block"}});
  error("invalid_model",[&]{validate_model(too_large);},"assembly");
  too_large=changed; too_large["features"][1]["mates"].push_back(mate("m64","p63","p0"));
  error("invalid_model",[&]{validate_model(too_large);},"assembly");
  const auto copy=large;
  for (int i=1;i<4;++i) { auto next=copy; next["id"]="assembly"+std::to_string(i); changed["features"].push_back(next); }
  validate_model(changed); ++checks;
  auto next=copy; next["id"]="assembly4"; changed["features"].push_back(next);
  error("limit_exceeded",[&]{validate_model(changed);},"assembly4");
}
void edit_tests() {
  const auto source=model();
  auto placed=apply_operations(source,Json::array({{{"op","set_part_placement"},{"assembly_id","assembly"},{"part_id","base"},
    {"placement",{{"translation",Json::array({Json{{"parameter","gap"}},0,0})}}}}}));
  require(placed["features"][1]["parts"][0]["placement"]["translation"][0] == Json{{"parameter","gap"}},"Placement retains expression intent");
  require(source == model(),"Edits do not mutate the source");
  error("invalid_model",[&]{apply_operations(source,Json::array({{{"op","set_part_placement"},{"assembly_id","assembly"},{"part_id","lid"},{"placement",Json::object()}}}));},"assembly");
  const Json detach={{"op","remove_mate"},{"assembly_id","assembly"},{"mate_id","joint"}};
  const Json move={{"op","set_part_placement"},{"assembly_id","assembly"},{"part_id","lid"},{"placement",{{"translation",{20,0,0}}}}};
  for (const auto& ops : {Json::array({detach,move}),Json::array({move,detach})}) {
    const auto detached=apply_operations(source,ops);
    require(detached["features"][1]["mates"].empty(),"Mate removal and placement validate final batch");
  }
  auto joint=mate(); joint["offset"]={0,0,20};
  const Json update={{"op","set_mate"},{"assembly_id","assembly"},{"mate",joint}};
  auto replaced=apply_operations(source,Json::array({update}));
  require(replaced["features"][1]["mates"].size()==1 && replaced["features"][1]["mates"][0]["offset"][2]==20,"Mate upsert replaces matching ID");
  replaced=apply_operations(source,Json::array({detach,update}));
  require(replaced["features"][1]["mates"][0]==joint,"Mate upsert adds missing ID");
  auto independent=source; independent["features"][1].erase("mates");
  require(apply_operations(independent,Json::array({update}))["features"][1]["mates"][0]==joint,"Mate upsert creates mates list");
  error("invalid_argument",[&]{apply_operations(independent,Json::array({detach}));},"assembly");
  auto bad=move; bad["part_id"]="unknown";
  error("invalid_argument",[&]{apply_operations(source,Json::array({bad}));},"assembly");
  bad=move; bad["assembly_id"]="block";
  error("invalid_argument",[&]{apply_operations(source,Json::array({bad}));},"block");
  bad=move; bad["assembly_id"]="unknown";
  error("invalid_argument",[&]{apply_operations(source,Json::array({bad}));},"unknown");
  auto dimension_edit=apply_operations(source,Json::array({{{"op","set_parameter"},{"name","gap"},{"value",15}}}));
  require(dimension_edit["features"]==source["features"],"Parameter edits preserve parts/mates intent");
  auto invalid=source["features"][1]; invalid["parts"][1]["input"]="missing";
  error("invalid_model",[&]{apply_operations(source,Json::array({{{"op","replace_feature"},{"id","assembly"},{"feature",invalid}}}));},"assembly");
  require(source==model(),"Failed edits preserve original model");
}
}
int main() {
  try { validation_tests(); edit_tests(); std::cout << checks << " assembly model checks passed\n"; return 0; }
  catch (const std::exception& e) { std::cerr << "Assembly model failure: " << e.what() << '\n'; return 1; }
}
