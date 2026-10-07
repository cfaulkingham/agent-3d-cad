#include "agentcad/service.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/viewer.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/live.hpp"
#include "agentcad/drawing.hpp"
#include "agentcad/bom.hpp"
#include <fstream>
#include <random>
#include <set>

namespace agentcad {
namespace {
Json object(Json properties, Json required, bool extra = false) {
  return {{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",extra}};
}
Json array(Json item, std::size_t limit) { return {{"type","array"},{"items",item},{"maxItems",limit}}; }
Json summary_schema() {
  const Json number={{"type","number"}}, integer={{"type","integer"},{"minimum",0}};
  const Json point={{"type","array"},{"items",number},{"minItems",3},{"maxItems",3}};
  return object({{"valid",{{"const",true}}},{"units",{{"const","mm"}}},{"volume_mm3",number},{"area_mm2",number},
    {"center_of_mass_mm",point},{"bounds_mm",object({{"min",point},{"max",point}},{"min","max"})},
    {"solid_count",integer},{"face_count",integer},{"edge_count",integer},
    {"assembly",{{"$ref","#/$defs/assembly_summary"}}}},
    {"valid","units","volume_mm3","area_mm2","center_of_mass_mm","bounds_mm","solid_count","face_count","edge_count"});
}
std::string evaluation_id() {
  std::random_device random;
  const char* hex="0123456789abcdef";
  std::string id="eval_";
  for(int i=0;i<16;++i) { const auto b=random() & 255; id+=hex[b>>4]; id+=hex[b&15]; }
  return id;
}
void write_artifact(const fs::path& path, const std::string& content) {
  if(content.size()>64*1024*1024) throw Error("limit_exceeded","Artifact exceeds 64 MiB");
  const auto temporary=temporary_file(path.parent_path());
  try {
    std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);
    stream.write(content.data(),static_cast<std::streamsize>(content.size())); stream.close();
    if(!stream) throw Error("storage_error","Could not write artifact");
    publish_file(temporary,path);
  } catch(...) { std::error_code ignored;fs::remove(temporary,ignored);throw; }
}
Json save_evaluation(const fs::path& root,const Json& record,Json result,bool draft,bool viewer) {
  result["schema_version"]=1;
  result["document_id"]=record.at("document_id"); result["revision"]=record.at("revision");
  result["kernel_version"]=kernel_version(); result["evaluation_id"]=evaluation_id();
  result["draft"]=draft; result["selection_lifetime"]="evaluation";
  result["feature_id"]=result.at("topology").at("feature_id");
  const auto dir=root/"evaluations";directory(dir);
  auto metadata=result;metadata.erase("mesh");
  const auto eid=result.at("evaluation_id").get<std::string>();
  write_artifact(dir/(eid+".json"),metadata.dump());
  if(viewer) {
    const auto prefix=record.at("document_id").get<std::string>()+"-"+eid;
    const auto path=root/"exports"/(prefix+".html");
    const auto data_path=root/"exports"/(prefix+".view.json");
    write_artifact(data_path,result.dump());
    write_artifact(path,viewer_html(result));
    result.erase("mesh");result.erase("topology");
    result["path"]=path_to_utf8(path);result["data_path"]=path_to_utf8(data_path);
  }
  return result;
}
}

Json tool_definitions() {
  const Json id={{"type","string"},{"pattern","^[A-Za-z][A-Za-z0-9_-]{0,63}$"}};
  const Json revision={{"type","integer"},{"minimum",1},{"maximum",9007199254740991ULL}};
  const Json text={{"type","string"}};
  const auto definitions=model_definitions();
  const Json operations={{"type","array"},{"minItems",1},{"maxItems",256},{"items",{{"$ref","#/$defs/operation"}}}};
  const Json reference_properties={{"document_id",id},{"revision",revision},{"evaluation_id",id},{"feature_id",id},
    {"kind",{{"type","string"},{"enum",{"face","edge"}}}},{"entity_id",id}};
  const Json reference=object(reference_properties,{"document_id","revision","evaluation_id","feature_id","kind","entity_id"});
  const Json record_properties={{"schema_version",{{"const",1}}},{"document_id",id},{"revision",revision},
    {"kernel_version",{{"const","8.0.1"}}},{"model",{{"$ref","#/$defs/model"}}},{"summary",summary_schema()}};
  const Json identity={{"schema_version",{{"const",1}}},{"document_id",id},{"revision",revision},
    {"kernel_version",{{"const","8.0.1"}}},{"evaluation_id",id},{"feature_id",id},{"draft",{{"type","boolean"}}},
    {"selection_lifetime",{{"const","evaluation"}}},{"summary",summary_schema()},
    {"topology",{{"$ref","#/$defs/topology"}}},{"mesh",{{"$ref","#/$defs/mesh"}}},{"path",text},{"data_path",text}};
  auto tool=[&](const char* name,const char* description,Json properties,Json required,Json output,bool read_only) {
    auto input=object(properties,required);input["$defs"]=definitions;
    output["$defs"]=definitions;
    return Json{{"name",name},{"description",description},{"inputSchema",input},{"outputSchema",output},
      {"annotations",{{"readOnlyHint",read_only},{"destructiveHint",false},{"openWorldHint",false}}}};
  };
  Json tools=Json::array({
    tool("cad_create","Build editable parts or an assembly with rigid placements and frame mates, and commit revision 1. Optional request_id deduplicates retries.",
      {{"document_id",id},{"model",{{"$ref","#/$defs/model"}}},{"request_id",id}}, {"document_id","model"},
      object(record_properties,{"schema_version","document_id","revision","kernel_version","model","summary"}),false),
    tool("cad_read","Read saved editable intent. Omit revision to read HEAD.",
      {{"document_id",id},{"revision",revision}},{"document_id"},
      object(record_properties,{"schema_version","document_id","revision","kernel_version","model"}),true),
    tool("cad_apply","Build an atomic semantic edit and commit only if expected_revision still matches. Failures preserve HEAD.",
      {{"document_id",id},{"expected_revision",revision},{"operations",operations},{"request_id",id}},
      {"document_id","expected_revision","operations"},object(record_properties,{"schema_version","document_id","revision","kernel_version","model","summary"}),false),
    tool("cad_restore","Rebuild a historical model as a new revision. History remains immutable; expected_revision must match HEAD.",
      {{"document_id",id},{"expected_revision",revision},{"source_revision",revision},{"request_id",id}},
      {"document_id","expected_revision","source_revision"},object(record_properties,{"schema_version","document_id","revision","kernel_version","model","summary"}),false),
    tool("cad_import","Create a document from a local STEP file up to 512 KiB. Source content and SHA-256 are saved in an opaque imported feature for reproducible rebuilds.",
      {{"document_id",id},{"path",text},{"request_id",id}}, {"document_id","path"},
      object(record_properties,{"schema_version","document_id","revision","kernel_version","model","summary"}),false),
    tool("cad_query","Query a committed revision. Topology and mesh IDs belong only to the returned evaluation. Optional feature_id scopes geometry.",
      {{"document_id",id},{"revision",revision},{"kind",{{"enum",{"summary","topology","mesh"}}}},{"feature_id",id}},
      {"document_id","revision"},object(identity,{"document_id","revision","kernel_version","feature_id","summary"}),true),
    tool("cad_export","Export a committed revision as independent STEP or binary STL using mm coordinates.",
      {{"document_id",id},{"revision",revision},{"format",{{"enum",{"step","stl"}}}}},{"document_id","revision","format"},
      object({{"document_id",id},{"revision",revision},{"format",{{"enum",{"step","stl"}}}},{"path",text},{"bytes",{{"type","integer"},{"minimum",1}}},{"units",{{"const","mm"}}}},
        {"document_id","revision","format","path","bytes","units"}),false),
    tool("cad_bom","Export a committed assembly bill of materials as JSON and CSV. Group named part instances by source feature, preserve explicit metadata and item numbers, and never infer material.",
      {{"document_id",id},{"revision",revision},{"feature_id",id}},{"document_id","revision"},
      object({{"document_id",id},{"revision",revision},{"kernel_version",{{"const","8.0.1"}}},{"units",{{"const","mm"}}},
        {"bom",{{"$ref","#/$defs/bom"}}},{"path",text},
        {"artifacts",{{"type","array"},{"minItems",2},{"maxItems",2},{"items",object({{"format",{{"enum",{"json","csv"}}}},{"path",text},{"bytes",{{"type","integer"},{"minimum",1}}}}, {"format","path","bytes"})}}}},
        {"document_id","revision","kernel_version","units","bom","artifacts","path"}),false),
    tool("cad_drawing","Generate a vector drawing of a committed revision, including exploded assembly views, bills of materials and geometry-checked numbered balloons. Measured dimensions, explicit tolerances, aligned layouts, hidden-line views, hatched sections, PDF/SVG sheets and per-view 1:1 mm DXF. Save the recipe to regenerate after edits; never changes the model.",
      {{"document_id",id},{"revision",revision},{"drawing",drawing_schema()}},{"document_id","revision"},
      object({{"document_id",id},{"revision",revision},{"kernel_version",{{"const","8.0.1"}}},{"units",{{"const","mm"}}},
        {"scale",{{"type","number"},{"exclusiveMinimum",0}}},
        {"sheet_mm",{{"type","array"},{"items",{{"type","number"},{"exclusiveMinimum",0}}},{"minItems",2},{"maxItems",2}}},
        {"layout",{{"enum",{"grid","first_angle","third_angle"}}}},
        {"view_layouts",array(object({{"view",id},
          {"origin_mm",{{"type","array"},{"items",{{"type","number"}}},{"minItems",2},{"maxItems",2}}},
          {"cell_mm",{{"type","array"},{"items",{{"type","number"}}},{"minItems",4},{"maxItems",4}}}},
          {"view","origin_mm","cell_mm"}),6)},
        {"path",text},{"recipe_path",text},{"projection_tolerance_mm",{{"const",0.02}}},
        {"artifacts",array(object({{"format",{{"enum",{"svg","pdf","dxf","json","csv"}}}},{"path",text},
          {"bytes",{{"type","integer"},{"minimum",1}}},{"view_id",id}}, {"format","path","bytes"}),10)},
        {"dimensions",array(drawing_dimension_result_schema(),32)},
        {"bom",{{"$ref","#/$defs/bom"}}},
        {"balloons",array(object({{"view",id},{"part_id",id},{"item_number",{{"type","integer"},{"minimum",1},{"maximum",999}}},
          {"anchor_mm",{{"type","array"},{"items",{{"type","number"}}},{"minItems",2},{"maxItems",2}}},
          {"label_mm",{{"type","array"},{"items",{{"type","number"}}},{"minItems",2},{"maxItems",2}}}},
          {"view","part_id","item_number","anchor_mm","label_mm"}),64)}},
        {"document_id","revision","kernel_version","units","scale","sheet_mm","layout","view_layouts","path","recipe_path","artifacts","dimensions","projection_tolerance_mm"}),false),
    tool("cad_view","Save an offline interactive HTML viewer and .view.json. Pick a face or edge and copy/save its revision-qualified reference.",
      {{"document_id",id},{"revision",revision},{"feature_id",id}},{"document_id","revision"},
      object(identity,{"schema_version","document_id","revision","evaluation_id","feature_id","draft","summary","path","data_path"}),false),
    tool("cad_preview","Build edits without committing. Returns a draft viewer; draft picks cannot be used as committed references.",
      {{"document_id",id},{"expected_revision",revision},{"operations",operations},{"feature_id",id}},
      {"document_id","expected_revision","operations"},object(identity,{"schema_version","document_id","revision","evaluation_id","feature_id","draft","summary","path","data_path"}),true),
    tool("cad_resolve_selection","Resolve a saved evaluation pick. Reject stale revisions, draft picks and mismatched evaluations. Persistent selectors are geometric rules, never pick tokens.",
      reference_properties,{"document_id","revision","evaluation_id","feature_id","kind","entity_id"},
      object({{"reference",reference},{"geometry",{{"oneOf",Json::array({Json{{"$ref","#/$defs/face"}},Json{{"$ref","#/$defs/edge"}}})}}},{"selector",{{"$ref","#/$defs/selector"}}}},{"reference","geometry"}),true),
    tool("cad_compare","Compare committed revisions by parameters, named features, output and geometric measurements.",
      {{"document_id",id},{"from_revision",revision},{"to_revision",revision}},{"document_id","from_revision","to_revision"},
      object({{"document_id",id},{"from_revision",revision},{"to_revision",revision},{"parameters",{{"type","object"}}},
        {"features",array(id,512)},{"output_changed",{{"type","boolean"}}},{"volume_delta_mm3",{{"type","number"}}},{"area_delta_mm2",{{"type","number"}}}},
        {"document_id","from_revision","to_revision","parameters","features","output_changed","volume_delta_mm3","area_delta_mm2"}),true),
    tool("cad_job","Submit, inspect, list or cancel a durable job. Submit a tool and arguments with request_id; retries return the same job. Geometry runs in bounded native workers.",
      {{"action",{{"enum",{"submit","get","cancel","list"}}}},{"request_id",id},{"job_id",id},{"tool",text},{"arguments",{{"type","object"}}},
        {"budget",object({{"timeout_ms",{{"type","integer"},{"minimum",1},{"maximum",300000}}},{"memory_mb",{{"type","integer"},{"minimum",128},{"maximum",4096}}}},Json::array())}},
      {"action"},{{"type","object"}},false)
  });
  const auto budgets=object({{"timeout_ms",{{"type","integer"},{"minimum",1},{"maximum",300000}}},
    {"memory_mb",{{"type","integer"},{"minimum",128},{"maximum",4096}}}},Json::array());
  const std::set<std::string> job_tools={"cad_create","cad_apply","cad_restore","cad_import","cad_query","cad_export","cad_bom","cad_drawing","cad_preview","cad_view"};
  Json submits=Json::array(),results=Json::array();
  for(const auto& definition:tools) {
    const auto name=definition.at("name").get<std::string>();if(!job_tools.contains(name))continue;
    auto input=definition.at("inputSchema");input.erase("$defs");
    auto output=definition.at("outputSchema");output.erase("$defs");results.push_back(output);
    submits.push_back(object({{"action",{{"const","submit"}}},{"request_id",id},{"tool",{{"const",name}}},{"arguments",input},{"budget",budgets}},
      {"action","request_id","tool","arguments"}));
  }
  submits.push_back(object({{"action",{{"enum",{"get","cancel"}}}},{"job_id",id}},{"action","job_id"}));
  submits.push_back(object({{"action",{{"const","list"}}}},{"action"}));
  tools.back()["inputSchema"]={{"type","object"},{"oneOf",submits},{"$defs",definitions}};
  const auto job=object({{"job_id",id},{"request_id",id},{"tool",text},{"budget",budgets},
    {"state",{{"enum",{"queued","running","cancelling","succeeded","failed","cancelled","interrupted"}}}},
    {"progress",{{"type","number"},{"minimum",0},{"maximum",1}}},{"submitted_at_unix_ms",{{"type","integer"}}},
    {"updated_at_unix_ms",{{"type","integer"}}},{"retried",{{"type","boolean"}}},
    {"result",{{"anyOf",results}}},{"error",object({{"code",text},{"message",text},{"details",{{"type","object"}}}},{"code","message","details"})}},
    {"job_id","request_id","tool","budget","state","progress","submitted_at_unix_ms"});
  tools.back()["outputSchema"]={{"type","object"},{"oneOf",Json::array({job,object({{"jobs",array(job,1000)},{"limit",{{"const",1000}}}},{"jobs","limit"})})},{"$defs",definitions}};
  for (auto& definition : live_tool_definitions()) tools.push_back(std::move(definition));
  return tools;
}

Json Service::call(const std::string& tool,const Json& args) {
  if(tool=="cad_open"||tool=="cad_show"||tool=="cad_list"||tool=="cad_context"||tool=="cad_viewer")
    return live_call(*this,store_,tool,args);
  if(tool=="cad_job") return dispatch_job(store_.root(),args);
  static const std::set<std::string> known={"cad_create","cad_read","cad_apply","cad_restore","cad_import","cad_query","cad_export","cad_bom","cad_drawing","cad_view","cad_preview","cad_resolve_selection","cad_compare"};
  if(!known.contains(tool)) throw Error("unknown_tool","Unknown tool: "+tool);
  if(tool=="cad_create") fields(args,{"document_id","model"},{"request_id"});
  else if(tool=="cad_read") fields(args,{"document_id"},{"revision"});
  else if(tool=="cad_apply") fields(args,{"document_id","expected_revision","operations"},{"request_id"});
  else if(tool=="cad_restore") fields(args,{"document_id","expected_revision","source_revision"},{"request_id"});
  else if(tool=="cad_import") fields(args,{"document_id","path"},{"request_id"});
  else if(tool=="cad_export") fields(args,{"document_id","revision","format"});
  else if(tool=="cad_bom") fields(args,{"document_id","revision"},{"feature_id"});
  else if(tool=="cad_drawing") fields(args,{"document_id","revision"},{"drawing"});
  else if(tool=="cad_query") fields(args,{"document_id","revision"},{"kind","feature_id"});
  else if(tool=="cad_view") fields(args,{"document_id","revision"},{"feature_id"});
  else if(tool=="cad_preview") fields(args,{"document_id","expected_revision","operations"},{"feature_id"});
  else if(tool=="cad_compare") fields(args,{"document_id","from_revision","to_revision"});
  else fields(args,{"document_id","revision","evaluation_id","feature_id","kind","entity_id"});
  const auto id=text_field(args,"document_id");identifier(id);
  if(args.contains("feature_id")) model_identifier(text_field(args,"feature_id"));
  if(tool=="cad_read") return store_.read(id,args.contains("revision")?std::optional(revision_number(args.at("revision"))):std::nullopt);
  if(tool=="cad_resolve_selection") {
    const auto revision=revision_number(args.at("revision"));
    const auto eid=text_field(args,"evaluation_id");identifier(eid);
    identifier(text_field(args,"entity_id"));
    const auto kind=text_field(args,"kind");
    if(kind!="face"&&kind!="edge") throw Error("invalid_argument","Selection kind must be face or edge");
    if(store_.read(id).at("revision")!=revision) throw Error("stale_selection","The document has changed; query or view its current revision");
    Json evaluation;
    const auto evaluation_dir=store_.root()/"evaluations";
    if(fs::is_symlink(fs::symlink_status(evaluation_dir)))throw Error("storage_error","Managed evaluation directory cannot be a symlink");
    try { evaluation=parse_json(read_text(evaluation_dir/(eid+".json"),64*1024*1024),64*1024*1024); }
    catch(const Error& e) { if(e.code=="not_found") throw Error("stale_selection","Evaluation is missing; request a fresh view");throw; }
    if(evaluation.at("draft")==true) throw Error("draft_selection","Draft picks cannot resolve committed design references");
    for(const auto* key:{"document_id","revision","evaluation_id","feature_id"})
      if(evaluation.at(key)!=args.at(key)) throw Error("stale_selection","Selection does not belong to this evaluation");
    for(const auto& entity:evaluation.at("topology").at(kind=="face"?"faces":"edges")) if(entity.at("id")==args.at("entity_id")) {
      Json result={{"reference",args},{"geometry",entity}};
      if(entity.contains("selector")) result["selector"]=entity.at("selector");
      return result;
    }
    throw Error("selection_missing","Entity is absent from the named evaluation");
  }
  if(tool=="cad_create"||tool=="cad_apply"||tool=="cad_restore"||tool=="cad_import") {
    const bool create=tool=="cad_create"||tool=="cad_import";
    const auto request_id=args.contains("request_id")?text_field(args,"request_id"):std::string();
    if(args.contains("request_id")) identifier(request_id);
    const auto fingerprint=request_id.empty()?std::string():request_fingerprint(tool,args);
    const auto expected=create?0:revision_number(args.at("expected_revision"));
    Json model;
    auto precondition=[&]() -> std::optional<Json> {
      if(!request_id.empty()) if(auto replay=store_.request_replay(id,request_id,fingerprint)) return replay;
      if(create) {
        try {store_.read(id);throw Error("already_exists","Document already exists: "+id);}
        catch(const Error& e) {if(e.code!="not_found")throw;}
      } else {
        const auto current=store_.read(id);
        if(current.at("revision")!=expected) throw Error("revision_conflict","Document changed; read its current revision",{{"expected_revision",expected},{"current_revision",current.at("revision")}});
      }
      return std::nullopt;
    };
    { DocumentLock lock(store_.root(),id);if(auto replay=precondition())return *replay;
      if(tool=="cad_create") model=args.at("model");
      else if(tool=="cad_restore") model=store_.read(id,revision_number(args.at("source_revision"))).at("model");
      else if(tool=="cad_apply") model=apply_operations(store_.read(id).at("model"),args.at("operations"));
      else {
        const auto content=read_text(path_from_utf8(text_field(args,"path")));
        if(content.size()>512*1024)throw Error("limit_exceeded","Embedded STEP imports are limited to 512 KiB");
        // Documents embed STEP as a JSON (UTF-8) string; transcoding would change
        // the bytes and SHA-256 that define the imported feature.
        if(const auto offset=invalid_utf8_offset(content))
          throw Error("invalid_argument","STEP file is not valid UTF-8 at byte "+std::to_string(*offset)+
            "; cad_import embeds the file unchanged as UTF-8 text (ISO 10303-21 encodes other text with \\X2\\ escapes)",{{"byte_offset",*offset}});
        model={{"schema_version",1},{"units","mm"},{"parameters",Json::object()},
          {"features",Json::array({{{"id","imported"},{"type","import_step"},{"content",content},{"sha256",sha256(content)}}})},{"output","imported"}};
      }
    }
    validate_model(model);
    const auto evaluated=evaluate_model(store_.root(),model,{{"kind","summary"}});
    DocumentLock lock(store_.root(),id);if(auto replay=precondition())return *replay;
    check_job_cancelled();
    Json receipt=Json::object();
    if(!request_id.empty()) receipt={{"request_id",request_id},{"fingerprint",fingerprint},{"result",{{"summary",evaluated.at("summary")}}}};
    auto record=store_.commit(id,model,create,receipt);record["summary"]=evaluated.at("summary");return record;
  }
  if(tool=="cad_compare") {
    const auto from=store_.read(id,revision_number(args.at("from_revision"))),to=store_.read(id,revision_number(args.at("to_revision")));
    const auto a=evaluate_model(store_.root(),from.at("model")),b=evaluate_model(store_.root(),to.at("model"));
    Json parameters=Json::object(),features=Json::array();std::set<std::string> names;
    for(const auto& [name,value]:from.at("model").at("parameters").items()){(void)value;names.insert(name);}
    for(const auto& [name,value]:to.at("model").at("parameters").items()){(void)value;names.insert(name);}
    for(const auto& name:names){auto x=from.at("model").at("parameters").value(name,Json()),y=to.at("model").at("parameters").value(name,Json());if(x!=y)parameters[name]={{"from",x},{"to",y}};}
    Json af=Json::object(),bf=Json::object();names.clear();
    for(const auto& f:from.at("model").at("features")){const auto n=text_field(f,"id");af[n]=f;names.insert(n);}
    for(const auto& f:to.at("model").at("features")){const auto n=text_field(f,"id");bf[n]=f;names.insert(n);}
    for(const auto& name:names)if(af.value(name,Json())!=bf.value(name,Json()))features.push_back(name);
    return {{"document_id",id},{"from_revision",from.at("revision")},{"to_revision",to.at("revision")},{"parameters",parameters},{"features",features},
      {"output_changed",from.at("model").at("output")!=to.at("model").at("output")},
      {"volume_delta_mm3",b.at("summary").at("volume_mm3").get<double>()-a.at("summary").at("volume_mm3").get<double>()},
      {"area_delta_mm2",b.at("summary").at("area_mm2").get<double>()-a.at("summary").at("area_mm2").get<double>()}};
  }
  if(tool=="cad_preview") {
    const auto expected=revision_number(args.at("expected_revision"));const auto record=store_.read(id);
    if(record.at("revision")!=expected) throw Error("revision_conflict","Preview base revision changed",{{"expected_revision",expected},{"current_revision",record.at("revision")}});
    const auto candidate=apply_operations(record.at("model"),args.at("operations"));
    Json request={{"kind","view"}};if(args.contains("feature_id"))request["feature_id"]=args.at("feature_id");
    return save_evaluation(store_.root(),record,evaluate_model(store_.root(),candidate,request),true,true);
  }
  const auto revision=revision_number(args.at("revision"));const auto record=store_.read(id,revision);
  if(tool=="cad_bom") {
    const auto bom=build_bom(record.at("model"),args.value("feature_id",std::string()));
    const Json identity={{"document_id",id},{"revision",revision},{"kernel_version",record.at("kernel_version")},{"units","mm"}};
    auto exported=identity;exported["schema_version"]=1;exported["bom"]=bom;
    const auto exports=store_.root()/"exports";directory(exports);
    const auto destination=exports/(id+"-r"+std::to_string(revision)+"-bom-"+evaluation_id());
    directory(destination);
    try {
      auto result=identity;result["bom"]=bom;result["artifacts"]=Json::array();result["path"]=path_to_utf8(destination/"manifest.json");
      for(const auto* format:{"json","csv"}) {
        const auto content=std::string(format)=="json"?exported.dump(2)+"\n":bom_csv(bom);
        const auto target=destination/(std::string("bom.")+format);
        check_job_cancelled();write_artifact(target,content);
        result["artifacts"].push_back({{"format",format},{"path",path_to_utf8(target)},{"bytes",content.size()}});
      }
      DocumentLock lock(store_.root(),id);check_job_cancelled();
      write_artifact(destination/"manifest.json",result.dump(2)+"\n");
      return result;
    } catch(...) {std::error_code ignored;fs::remove_all(destination,ignored);throw;}
  }
  if(tool=="cad_drawing") {
    const auto recipe=args.value("drawing",Json::object());
    const auto spec=normalize_drawing(recipe,record.at("model"));
    const Json identity={{"document_id",id},{"revision",revision},{"kernel_version",kernel_version()}};
    const auto evaluated=evaluate_model(store_.root(),record.at("model"),
      {{"kind","drawing"},{"drawing",spec},{"identity",identity}}).at("drawing");
    const auto exports=store_.root()/"exports";directory(exports);
    // A generation owns a fresh directory. Only its final manifest publishes the
    // set; failed/cancelled generation cannot overwrite a prior drawing or HEAD.
    const auto destination=exports/(id+"-r"+std::to_string(revision)+"-drawing-"+evaluation_id());
    directory(destination);
    try {
      Json result=identity;result["units"]="mm";result["scale"]=evaluated.at("scale");
      result["sheet_mm"]=evaluated.at("sheet_mm");result["dimensions"]=evaluated.at("dimensions");
      result["layout"]=evaluated.at("layout");result["view_layouts"]=evaluated.at("view_layouts");
      for(const auto* field:{"bom","balloons"}) if(evaluated.contains(field)) result[field]=evaluated.at(field);
      result["projection_tolerance_mm"]=0.02;
      result["path"]=path_to_utf8(destination/"manifest.json");
      result["recipe_path"]=path_to_utf8(destination/"drawing.json");
      result["artifacts"]=Json::array();
      std::set<std::string> names;std::size_t total=0;
      for(const auto& file:evaluated.at("files")) {
        const auto name=text_field(file,"name"),format=text_field(file,"format");
        const auto content=text_field(file,"content");
        const auto path=path_from_utf8(name);
        if(path.filename()!=path||name.empty()||name=="."||name==".."||!names.insert(name).second||
           (format!="svg"&&format!="pdf"&&format!="dxf"&&format!="json"&&format!="csv")||path.extension()!=path_from_utf8("."+format)||
           ((format=="json"||format=="csv")&&name!="bom."+format))
          throw Error("internal_error","Invalid drawing artifact filename");
        total+=content.size();
        if(content.empty()||total>64*1024*1024||names.size()>10)throw Error("limit_exceeded","Drawing artifacts exceed output limits");
        check_job_cancelled();write_artifact(destination/path,content);
        Json artifact={{"format",format},{"path",path_to_utf8(destination/path)},{"bytes",content.size()}};
        if(format=="dxf")artifact["view_id"]=path_to_utf8(path.stem());
        result["artifacts"].push_back(artifact);
      }
      auto saved=identity;saved["schema_version"]=1;saved["drawing"]=recipe;saved["resolved_drawing"]=spec;
      saved["model_sha256"]=sha256(record.at("model").dump());
      write_artifact(destination/"drawing.json",saved.dump(2)+"\n");
      DocumentLock lock(store_.root(),id);check_job_cancelled();
      write_artifact(destination/"manifest.json",result.dump(2)+"\n");
      return result;
    } catch(...) {std::error_code ignored;fs::remove_all(destination,ignored);throw;}
  }
  if(tool=="cad_query"||tool=="cad_view") {
    const auto kind=tool=="cad_view"?std::string("view"):(args.contains("kind")?text_field(args,"kind"):std::string("summary"));
    if(kind!="summary"&&kind!="topology"&&kind!="mesh"&&!(kind=="view"&&tool=="cad_view"))throw Error("invalid_argument","Query kind must be summary, topology or mesh");
    Json request={{"kind",kind=="mesh"?"view":kind}};if(args.contains("feature_id"))request["feature_id"]=args.at("feature_id");
    auto result=evaluate_model(store_.root(),record.at("model"),request);
    if(kind!="summary") return save_evaluation(store_.root(),record,result,false,kind=="view");
    result["document_id"]=id;result["revision"]=revision;result["kernel_version"]=kernel_version();
    result["feature_id"]=args.value("feature_id",record.at("model").at("output"));return result;
  }
  const auto format=text_field(args,"format");
  if(format!="step"&&format!="stl")throw Error("invalid_argument","Export format must be step or stl");
  const auto exports=store_.root()/"exports";directory(exports);
  const auto target=exports/(id+"-r"+std::to_string(revision)+"."+format),temporary=temporary_file(exports);
  try {
    evaluate_model(store_.root(),record.at("model"),{{"kind","export"},{"format",format},{"path",path_to_utf8(temporary)}});
    DocumentLock lock(store_.root(),id);check_job_cancelled();publish_file(temporary,target);
  }catch(...){std::error_code ignored;fs::remove(temporary,ignored);throw;}
  return {{"document_id",id},{"revision",revision},{"format",format},{"path",path_to_utf8(target)},{"bytes",fs::file_size(target)},{"units","mm"}};
}
}
