#include "agentcad/bom.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include <algorithm>
#include <functional>
#include <iostream>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message) {++checks;if (!value) throw std::runtime_error(message);}
void fails(const std::string& code,const std::function<void()>& action) {
  try {action();} catch (const Error& e) {require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what());return;}
  throw std::runtime_error("Expected "+code);
}
struct Temp {
  fs::path path=temporary_file(fs::temp_directory_path());
  Temp() {fs::remove(path);directory(path);}
  ~Temp() {std::error_code ignored;fs::remove_all(path,ignored);}
};
Json fixture() {return parse_json(R"({"schema_version":1,"units":"mm","parameters":{},"features":[
 {"id":"zeta","type":"box","size":[2,3,4]},
 {"id":"alpha","type":"box","size":[5,6,7]},
 {"id":"multi","type":"pattern","input":"zeta","count":2,"step":[10,0,0]},
 {"id":"fixture","type":"assembly","parts":[
  {"id":"z2","input":"zeta","placement":{"translation":[20,0,0]}},
  {"id":"a1","input":"alpha"},
  {"id":"z1","input":"zeta"},
  {"id":"m1","input":"multi","placement":{"translation":[0,20,0]}}
 ]}],"output":"fixture"})");}
Json metadata() {return {{"input","zeta"},{"item_number",1},{"part_number","Z-\"1\",rev A"},{"description","Block, \"small\""},{"material","Steel"}};}
void model_tests() {
  const auto source=fixture();const auto original=build_bom(source);
  require(original.at("assembly_id")=="fixture"&&original.at("total_quantity")==4,"Output assembly and total instances");
  require(original.at("items").size()==3,"Repeated input becomes one BOM group");
  require(original.at("items")[0].at("input")=="alpha"&&original.at("items")[0].at("item_number")==1,"Automatic items sorted by input");
  require(original.at("items")[1].at("input")=="multi"&&original.at("items")[1].at("quantity")==1,"Multisolid source counts as one instance");
  require(original.at("items")[2].at("quantity")==2&&original.at("items")[2].at("part_ids")==Json::array({"z1","z2"}),"Instance IDs sorted and counted");
  for (const auto& item:original.at("items"))
    require(!item.contains("part_number")&&!item.contains("description")&&!item.contains("material"),"Metadata is never guessed");
  auto model=source;std::reverse(model["features"][3]["parts"].begin(),model["features"][3]["parts"].end());
  require(build_bom(model)==original,"Part ordering does not affect BOM");
  model=source;model["features"][3]["bom"]=Json::array({metadata(),{{"input","multi"},{"item_number",3}}});
  const auto numbered=build_bom(model);
  require(numbered.at("items")[0].at("input")=="zeta"&&numbered.at("items")[1].at("input")=="alpha"&&numbered.at("items")[1].at("item_number")==2,"Automatic numbering skips all explicit reservations");
  require(numbered.at("items")[0].at("part_number")==metadata().at("part_number"),"Explicit metadata is preserved exactly");
  const auto csv=bom_csv(numbered);
  require(csv.find("\"Z-\"\"1\"\",rev A\"")!=std::string::npos&&csv.find("\"Block, \"\"small\"\"\"")!=std::string::npos,"CSV doubles embedded quotes and encloses commas");
  require(csv.find("\"z1;z2\"\r\n")!=std::string::npos&&csv.ends_with("\r\n"),"CSV identifiers and records are deterministic");
  model["features"][3]["bom"][1]["item_number"]=999;
  require(build_bom(model).at("items").back().at("item_number")==999,"Explicit maximum item number preserved");
  auto invalid=model;invalid["features"][3]["bom"][1]["item_number"]=1;
  fails("invalid_model",[&]{validate_model(invalid);});
  for (const Json number:Json::array({0,1000,1.5,"1"})) {
    invalid=model;invalid["features"][3]["bom"][0]["item_number"]=number;
    fails("invalid_model",[&]{validate_model(invalid);});
  }
  invalid=model;invalid["features"][3]["bom"].push_back(metadata());
  fails("invalid_model",[&]{validate_model(invalid);});
  invalid=model;invalid["features"][3]["bom"][0]["input"]="unused";
  fails("invalid_model",[&]{validate_model(invalid);});
  invalid=model;invalid["features"][3]["bom"][0]["unknown"]=1;
  fails("invalid_argument",[&]{validate_model(invalid);});
  for (const auto* field:{"part_number","description","material"}) {
    const auto maximum=std::string(field)=="description"?120:64;
    auto bounded=model;bounded["features"][3]["bom"][0][field]=std::string(maximum,'x');validate_model(bounded);++checks;
    bounded["features"][3]["bom"][0][field]=std::string(maximum+1,'x');fails("invalid_model",[&]{validate_model(bounded);});
    for (const auto& text:{std::string("line\n"),std::string("tab\t"),std::string("\x7f"),std::string("\xc3\xa9")}) {
      bounded=model;bounded["features"][3]["bom"][0][field]=text;fails("invalid_model",[&]{validate_model(bounded);});
    }
    bounded["features"][3]["bom"][0][field]="";validate_model(bounded);++checks;
  }
  fails("invalid_argument",[&]{build_bom(source,"zeta");});
  fails("invalid_argument",[&]{build_bom(source,"missing");});
  model=source;model["output"]="zeta";
  fails("invalid_argument",[&]{build_bom(model);});
  require(build_bom(model,"fixture")==original,"Explicit assembly scope works when output is a part");
  const Json set={{"op","set_bom_item"},{"assembly_id","fixture"},{"item",metadata()}};
  model=apply_operations(source,Json::array({set}));
  require(model["features"][3]["bom"][0]==metadata(),"Metadata semantic edit adds item");
  auto update=set;update["item"]={{"input","zeta"},{"description","Replacement"}};
  const auto replaced=apply_operations(model,Json::array({update}));
  require(replaced["features"][3]["bom"].size()==1&&!replaced["features"][3]["bom"][0].contains("material"),"Upsert fully replaces old metadata");
  const Json remove={{"op","remove_bom_item"},{"assembly_id","fixture"},{"input","zeta"}};
  require(build_bom(apply_operations(model,Json::array({remove})))==original,"Removal restores automatic group metadata");
  fails("invalid_argument",[&]{apply_operations(source,Json::array({remove}));});
  update=set;update["item"]["input"]="unused";
  fails("invalid_model",[&]{apply_operations(source,Json::array({update}));});
  require(source==fixture(),"Failed and successful edits preserve source value");
  auto swapped=model;swapped["features"][3]["bom"].push_back({{"input","alpha"},{"item_number",2}});
  auto first=set;first["item"]["item_number"]=2;
  const Json second={{"op","set_bom_item"},{"assembly_id","fixture"},{"item",{{"input","alpha"},{"item_number",1}}}};
  require(build_bom(apply_operations(swapped,Json::array({first,second}))).at("items")[0].at("input")=="alpha","Final-state validation permits atomic number swap");
}
void service_tests() {
  set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));Temp temp;Service service(temp.path);
  auto model=fixture();model["features"][3]["bom"]=Json::array({metadata()});
  const auto created=service.call("cad_create",{{"document_id","inventory"},{"model",model}});
  const auto first=service.call("cad_bom",{{"document_id","inventory"},{"revision",1}});
  require(first.at("bom")==build_bom(model)&&first.at("artifacts").size()==2,"Standalone export returns grouped metadata and both artifacts");
  require(parse_json(read_text(path_from_utf8(first.at("path"))))==first,"Manifest publishes exact result");
  for (const auto& artifact:first.at("artifacts")) {
    const auto content=read_text(path_from_utf8(artifact.at("path")));
    require(content.size()==artifact.at("bytes").get<std::size_t>(),"Artifact byte count matches file");
    if (artifact.at("format")=="json") {
      const auto saved=parse_json(content);
      require(saved.at("bom")==first.at("bom")&&saved.at("schema_version")==1&&saved.at("revision")==1&&saved.at("document_id")=="inventory","JSON export records inventory source revision");
    } else require(content==bom_csv(first.at("bom")),"CSV export is independent exact content");
  }
  const auto another=service.call("cad_bom",{{"document_id","inventory"},{"revision",1}});
  require(another.at("path")!=first.at("path")&&fs::exists(path_from_utf8(first.at("path"))),"Repeated exports preserve prior artifacts");
  require(service.call("cad_read",{{"document_id","inventory"}}).at("model")==model,"Exports do not change editable intent");
  fails("invalid_argument",[&]{service.call("cad_bom",{{"document_id","inventory"},{"revision",1},{"feature_id","alpha"}});});
  const auto edited=service.call("cad_apply",{{"document_id","inventory"},{"expected_revision",1},{"operations",Json::array({
    {{"op","set_bom_item"},{"assembly_id","fixture"},{"item",{{"input","zeta"},{"item_number",8},{"description","New metadata"}}}}})}});
  require(edited.at("revision")==2&&edited.at("summary").at("volume_mm3")==created.at("summary").at("volume_mm3"),"Metadata edit commits without changing geometry");
  const auto history=service.call("cad_bom",{{"document_id","inventory"},{"revision",1}});
  require(history.at("bom")==first.at("bom"),"Historical BOM remains unchanged after metadata edit");
  const auto current=service.call("cad_bom",{{"document_id","inventory"},{"revision",2}});
  require(current.at("bom").at("items").back().at("item_number")==8,"New revision uses changed metadata");
  const auto before=service.call("cad_read",{{"document_id","inventory"}});
  fails("invalid_model",[&]{service.call("cad_apply",{{"document_id","inventory"},{"expected_revision",2},{"operations",Json::array({
    {{"op","set_bom_item"},{"assembly_id","fixture"},{"item",{{"input","unused"},{"material","Unknown"}}}}})}});});
  require(service.call("cad_read",{{"document_id","inventory"}})==before,"Invalid BOM edit preserves HEAD and record");
}
}
int main() {try {model_tests();service_tests();std::cout<<checks<<" BOM checks passed\n";return 0;}
  catch(const std::exception& e) {std::cerr<<"BOM failure: "<<e.what()<<'\n';return 1;}}
