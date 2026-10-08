#include "agentcad/manufacturing.hpp"
#include "agentcad/bom.hpp"
#include "agentcad/drawing.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/fabrication.hpp"
#include "agentcad/storage.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace agentcad {
namespace {
Json object(Json properties,Json required) {
  return {{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};
}
Json bounded_text(std::size_t limit) {
  return {{"type","string"},{"maxLength",limit},{"pattern","^[^\\u0000-\\u001f\\u007f]*$"}};
}
const Json processes={"unspecified","fdm","cnc","sheet_laser","molding","purchased"};
void text(const Json& value,std::size_t limit) {
  if(!value.is_string())throw Error("invalid_argument","Manufacturing text must be a string");
  const auto& bytes=value.get_ref<const std::string&>();
  if(bytes.size()>limit||invalid_utf8_offset(bytes))throw Error("invalid_argument","Manufacturing text exceeds its UTF-8 byte limit");
  for(const unsigned char c:bytes)if(c<32||c==127)throw Error("invalid_argument","Manufacturing text must be printable");
}
void notes(const Json& value) {
  if(!value.is_array()||value.size()>16)throw Error("invalid_argument","Manufacturing notes permit at most 16 entries");
  for(const auto& note:value)text(note,240);
}
class PackageFiles {
public:
  fs::path root;Json artifacts=Json::array();std::uintmax_t total=0;
  explicit PackageFiles(fs::path path):root(std::move(path)){}
  void record(const std::string& relative) {
    const auto path=root/path_from_utf8(relative);const auto bytes=fs::file_size(path);
    if(!bytes||bytes>64*1024*1024||bytes>manufacturing_bytes_limit-total||artifacts.size()>=manufacturing_file_limit)
      throw Error("limit_exceeded","Manufacturing package exceeds its file, count or aggregate byte limit");
    total+=bytes;
    artifacts.push_back({{"path",relative},{"format",path_to_utf8(path.extension()).substr(1)},
      {"bytes",bytes},{"sha256",sha256(read_text(path,64*1024*1024))}});
  }
  void write(const std::string& relative,const std::string& content) {
    if(content.empty()||content.size()>64*1024*1024||content.size()>manufacturing_bytes_limit-total)
      throw Error("limit_exceeded","Manufacturing artifact exceeds its byte limit");
    atomic_text(root/path_from_utf8(relative),content,64*1024*1024);record(relative);
  }
};
}
Json manufacturing_options_schema() {
  const Json id={{"type","string"},{"pattern","^[A-Za-z][A-Za-z0-9_-]{0,63}$"}};
  const Json recipe={{"$ref","#/$defs/drawing_spec"}};
  const Json note_array={{"type","array"},{"items",bounded_text(240)},{"maxItems",16}};
  return object({{"formats",{{"type","array"},{"items",{{"enum",{"step","stl"}}}},{"minItems",1},{"maxItems",2},{"uniqueItems",true}}},
    {"drawings",{{"type","boolean"}}},{"part_drawing",recipe},{"assembly_drawing",recipe},{"notes",note_array},
    {"fabrication_review",{{"$ref","#/$defs/fabrication_options"}}},
    {"parts",{{"type","array"},{"maxItems",manufacturing_part_limit},{"items",object({{"feature_id",id},
      {"process",{{"enum",processes}}},{"material",bounded_text(64)},{"notes",note_array},{"drawing",recipe}}, {"feature_id"})}}}},Json::array());
}
void validate_manufacturing_options(const Json& options) {
  fields(options,{}, {"formats","drawings","part_drawing","assembly_drawing","notes","parts","fabrication_review"});
  if(options.contains("fabrication_review"))validate_fabrication_options(options.at("fabrication_review"));
  if(options.contains("formats")) {
    const auto& formats=options.at("formats");std::set<std::string> seen;
    if(!formats.is_array()||formats.empty()||formats.size()>2)throw Error("invalid_argument","Manufacturing formats require STEP, STL or both");
    for(const auto& format:formats)if(!format.is_string()||(format!="step"&&format!="stl")||!seen.insert(format.get<std::string>()).second)
      throw Error("invalid_argument","Unsupported or repeated manufacturing format");
  }
  if(options.contains("drawings")&&!options.at("drawings").is_boolean())throw Error("invalid_argument","Manufacturing drawings must be boolean");
  if(options.contains("notes"))notes(options.at("notes"));
  for(const auto* key:{"part_drawing","assembly_drawing"})if(options.contains(key)&&!options.at(key).is_object())
    throw Error("invalid_argument","Manufacturing drawing recipes must be objects");
  if(!options.value("drawings",true)&&(options.contains("part_drawing")||options.contains("assembly_drawing")))
    throw Error("invalid_argument","Drawing recipes conflict with disabled drawings");
  if(options.contains("parts")) {
    const auto& parts=options.at("parts");std::set<std::string> ids;
    if(!parts.is_array()||parts.size()>manufacturing_part_limit)throw Error("invalid_argument","Manufacturing part assumptions exceed their limit");
    for(const auto& part:parts) {
      fields(part,{"feature_id"},{"process","material","notes","drawing"});
      const auto id=text_field(part,"feature_id");model_identifier(id);
      if(!ids.insert(id).second)throw Error("invalid_argument","Duplicate manufacturing part assumptions",{{"feature_id",id}});
      if(part.contains("process")&&std::find(processes.begin(),processes.end(),part.at("process"))==processes.end())
        throw Error("invalid_argument","Unsupported manufacturing process",{{"feature_id",id}});
      if(part.contains("material"))text(part.at("material"),64);
      if(part.contains("notes"))notes(part.at("notes"));
      if(part.contains("drawing")&&(!part.at("drawing").is_object()||!options.value("drawings",true)))
        throw Error("invalid_argument","Part drawing recipe conflicts with disabled drawings or is not an object",{{"feature_id",id}});
    }
  }
}
Json manufacture(const Json& model,const BuiltModel& built,const Json& options,const Json& identity,const fs::path& stage) {
  validate_manufacturing_options(options);validate_model(model);
  const auto selected=text_field(identity,"feature_id");const auto summary=built.summary(selected);
  if(summary.at("solid_count")==0)throw Error("invalid_argument","Manufacturing requires solid output",{{"feature_id",selected}});
  Json bom,occurrences=Json::array();
  if(summary.contains("assembly")) {
    bom=build_bom(model,selected);occurrences=summary.at("assembly").at("parts");
  }else {
    bom={{"items",Json::array({{{"item_number",1},{"input",selected},{"quantity",1},{"part_ids",Json::array({selected})}}})},{"total_quantity",1}};
    if(const auto* source=imported_step_source(model,selected);source&&source->contains("purchase"))
      bom["items"][0]["purchase"]=source->at("purchase");
  }
  const auto& items=bom.at("items");
  if(items.size()>manufacturing_part_limit)throw Error("limit_exceeded","Too many unique manufacturing sources");
  std::map<std::string,Json> assumptions;
  for(const auto& part:options.value("parts",Json::array()))assumptions.emplace(text_field(part,"feature_id"),part);
  for(const auto& [id,data]:assumptions) {
    bool found=false;for(const auto& item:items)if(item.at("input")==id)found=true;
    if(!found)throw Error("invalid_argument","Manufacturing assumptions must name a selected leaf source",{{"feature_id",id}});
  }
  if(options.contains("assembly_drawing")&&!summary.contains("assembly"))
    throw Error("invalid_argument","Assembly drawing recipe requires an assembly",{{"feature_id",selected}});
  PackageFiles files(stage);directory(stage/"parts");
  Json source=identity;source["model_sha256"]=sha256(model.dump());
  auto normalized=options;
  normalized["formats"]=options.value("formats",Json({"step","stl"}));
  normalized["drawings"]=options.value("drawings",true);normalized["notes"]=options.value("notes",Json::array());
  Json manifest={{"schema_version",1},{"units","mm"},{"source",source},{"options",normalized},{"bom",bom},
    {"occurrences",occurrences},{"parts",Json::array()},{"coordinate_policy","source_features_with_saved_occurrence_transforms"},
    {"process_review",{{"status","not_evaluated"},{"reason","Package exports geometry and caller assumptions; process checks require a separate measured review."}}}};
  auto saved=identity;saved.erase("feature_id");saved["schema_version"]=1;saved["model"]=model;
  files.write("source.json",saved.dump(2)+"\n");
  if(options.contains("fabrication_review")) {
    const auto report=built.fabrication_review(options.at("fabrication_review"),selected);
    auto review_source=source;review_source["native_build"]=AGENTCAD_CACHE_BUILD;
    files.write("review.json",Json{{"schema_version",1},{"source",review_source},{"options",options.at("fabrication_review")},{"report",report}}.dump(2)+"\n");
    manifest["process_review"]={{"status",report.at("status")},{"report_path","review.json"}};
  }
  auto inventory=source;inventory["bom"]=bom;files.write("bom.json",inventory.dump(2)+"\n");files.write("bom.csv",bom_csv(bom));
  const auto drawing=[&](const std::string& feature,const std::string& folder,const Json& recipe) {
    auto scoped=model;scoped["output"]=feature;
    auto effective=recipe;if(!effective.contains("title"))effective["title"]=feature;
    const auto spec=normalize_drawing(effective,scoped);
    auto drawing_identity=identity;drawing_identity["feature_id"]=feature;
    const auto rendered=render_drawing(built.drawing({{"views",spec.at("views")},{"hidden_lines",spec.at("hidden_lines")}},nullptr,feature),spec,drawing_identity);
    std::set<std::string> names;
    for(const auto& file:rendered.at("files")) {
      const auto name=text_field(file,"name"),format=text_field(file,"format");const auto path=path_from_utf8(name);
      if(path.filename()!=path||name.empty()||name=="."||name==".."||!names.insert(name).second||
          (format!="pdf"&&format!="svg"&&format!="dxf"&&format!="json"&&format!="csv")||path.extension()!=path_from_utf8("."+format))
        throw Error("internal_error","Invalid manufacturing drawing filename");
      files.write(folder+"/"+name,text_field(file,"content"));
    }
    Json ledger={{"source",source},{"feature_id",feature},{"recipe",recipe},{"resolved_drawing",spec},
      {"dimensions",rendered.at("dimensions")},{"projection_tolerance_mm",0.02}};
    files.write(folder+"/drawing.json",ledger.dump(2)+"\n");
    return folder+"/drawing.json";
  };
  std::size_t index=0;
  for(const auto& item:items) {
    const auto feature=text_field(item,"input");
    try {
      // Sequential generated names keep even model-internal Windows device IDs portable.
      const auto folder="parts/part-"+std::to_string(++index);directory(stage/path_from_utf8(folder));
      const auto found=assumptions.find(feature);const auto data=found==assumptions.end()?Json::object():found->second;
      auto part=item;part["feature_id"]=feature;part["directory"]=folder;part["summary"]=built.summary(feature);
      part["process"]=data.value("process",std::string("unspecified"));part["notes"]=data.value("notes",Json::array());
      if(data.contains("material"))part["material"]=data.at("material");
      if(const auto* source=imported_step_source(model,feature);source&&source->contains("purchase")) {
        const auto relative=folder+"/source.step";
        files.write(relative,text_field(*source,"content"));
        part["source_artifact"]={{"path",relative},{"sha256",source->at("sha256")},{"source_feature_id",source->at("id")}};
      }
      for(const auto& format:normalized.at("formats")) {
        const auto relative=folder+"/part."+format.get<std::string>();built.export_file(stage/path_from_utf8(relative),format.get<std::string>(),feature);files.record(relative);
      }
      if(normalized.at("drawings").get<bool>())part["drawing_recipe"]=drawing(feature,folder,data.value("drawing",options.value("part_drawing",Json::object())));
      manifest["parts"].push_back(std::move(part));
    }catch(const Error& error) {auto details=error.details;details["feature_id"]=feature;throw Error(error.code,error.what(),details);}
  }
  if(summary.contains("assembly")) {
    directory(stage/"assembly");
    for(const auto& format:normalized.at("formats")) {
      const auto relative="assembly/assembly."+format.get<std::string>();built.export_file(stage/path_from_utf8(relative),format.get<std::string>(),selected);files.record(relative);
    }
    manifest["assembly"]={{"feature_id",selected},{"summary",summary}};
    if(normalized.at("drawings").get<bool>())manifest["assembly"]["drawing_recipe"]=drawing(selected,"assembly",options.value("assembly_drawing",Json::object()));
  }
  manifest["artifacts"]=files.artifacts;
  const auto content=manifest.dump(2)+"\n";
  if(content.size()>64*1024*1024||content.size()>manufacturing_bytes_limit-files.total)throw Error("limit_exceeded","Manufacturing manifest exceeds package budget");
  atomic_text(stage/"manifest.json",content,64*1024*1024);
  return {{"artifacts",files.artifacts},{"part_count",items.size()},{"artifact_count",files.artifacts.size()},
    {"bytes",files.total+content.size()},{"model_sha256",source.at("model_sha256")}};
}
}
