#include "agentcad/render_service.hpp"
#include "agentcad/artifact_review.hpp"
#include "agentcad/download.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/image.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/model.hpp"
#include "agentcad/render.hpp"
#include "agentcad/render_settings.hpp"
#include "agentcad/visual.hpp"
#include <random>

namespace agentcad {
namespace {
constexpr std::size_t png_limit=16*1024*1024,gif_limit=64*1024*1024,inline_limit=2*1024*1024;
Json object(Json p,Json required=Json::array()){return {{"type","object"},{"properties",p},{"required",required},{"additionalProperties",false}};}
Json id(){return {{"type","string"},{"pattern","^[A-Za-z][A-Za-z0-9_-]{0,63}$"}};}
Json hash(){return {{"type","string"},{"pattern","^[a-f0-9]{64}$"}};}
Json configuration_schema(){
  return object({{"joints",{{"type","object"},{"maxProperties",128},{"additionalProperties",{{"type","number"}}}}},
    {"animation",{{"type","integer"},{"minimum",0},{"maximum",63}}},
    {"time_seconds",{{"type","number"},{"minimum",0},{"maximum",3600}}}});
}
Json input_source_schema(){
  return {{"oneOf",Json::array({
    object({{"document_id",id()},{"revision",{{"type","integer"},{"minimum",1},{"maximum",9007199254740991ULL}}},{"feature_id",id()}},{"document_id","revision"}),
    object({{"review_path",{{"type","string"}}},{"expected_sha256",hash()},{"configuration",configuration_schema()}},{"review_path","expected_sha256"})
  })}};
}
Json settings_schema(){
  auto vector=[](int size,double lo,double hi){return Json{{"type","array"},{"items",{{"type","number"},{"minimum",lo},{"maximum",hi}}},{"minItems",size},{"maxItems",size}};};
  const auto unit=vector(3,-1,1);
  auto clip=object({{"normal",unit},{"offset_mm",{{"type","number"},{"minimum",-1e12},{"maximum",1e12}}},{"keep",{{"enum",{"positive","negative"}}}}},{"normal","offset_mm","keep"});
  auto explode=object({{"distance_mm",{{"type","number"},{"minimum",0},{"maximum",1e6}}},{"directions",{{"type","array"},{"maxItems",1024},{"items",object({{"part_id",occurrence_schema()},{"direction",unit}},{"part_id","direction"})}}}},{"distance_mm","directions"});
  auto schema=object({
    {"width",{{"type","integer"},{"minimum",16},{"maximum",2048}}},{"height",{{"type","integer"},{"minimum",16},{"maximum",2048}}},
    {"view",{{"enum",{"single","sheet"}}}},
    {"camera",object({{"yaw",{{"type","number"},{"minimum",-1e6},{"maximum",1e6}}},{"pitch",{{"type","number"},{"minimum",-1e6},{"maximum",1e6}}},{"zoom",{{"type","number"},{"minimum",.01},{"maximum",1000}}},{"pan",vector(2,-1e6,1e6)}},{"yaw","pitch","zoom","pan"})},
    {"render",render_settings_schema()},{"appearance",visual_appearance_schema()},
    {"presentation",object({{"clip",{{"anyOf",Json::array({Json{{"type","null"}},clip})}}},{"explode",explode}},{"clip","explode"})},
    {"hidden_part_ids",{{"type","array"},{"maxItems",1024},{"uniqueItems",true},{"items",occurrence_schema()}}}
  });
  schema["allOf"]=Json::array({Json{
    {"if",{{"required",{"view"}},{"properties",{{"view",{{"const","sheet"}}}}}}},
    {"then",{{"not",{{"required",{"camera"}}}},{"properties",{
      {"width",{{"minimum",128}}},{"height",{{"minimum",128}}},
      {"render",{{"properties",{{"projection",{{"const","orthographic"}}}}}}}
    }}}}
  }});
  return schema;
}
Json sheet_schema(){
  auto dimension=Json{{"type","integer"},{"minimum",1},{"maximum",2048}};
  auto coordinate=Json{{"type","integer"},{"minimum",0},{"maximum",2048}};
  auto view=object({{"name",{{"enum",{"front","top","right","iso"}}}},
    {"x",coordinate},{"y",coordinate},{"width",dimension},{"height",dimension}},
    {"name","x","y","width","height"});
  return object({{"projection",{{"const","orthographic"}}},
    {"pixels_per_mm",{{"type","number"},{"exclusiveMinimum",0}}},
    {"views",{{"type","array"},{"items",view},{"minItems",4},{"maxItems",4}}}},
    {"projection","pixels_per_mm","views"});
}
std::string token(){std::random_device random;return "render_"+std::to_string(random())+"_"+std::to_string(random());}
}
Json render_tool_definition(){
  auto native=object({
    {"document_id",id()},{"revision",{{"type","integer"},{"minimum",1}}},{"feature_id",id()},
    {"kernel_version",{{"const","8.0.1"}}},{"model_sha256",hash()},{"native_build",{{"type","string"}}},{"units",{{"const","mm"}}},
    {"sequence_sha256",hash()},{"sequence_name",{{"type","string"}}},{"sample_rate_hz",{{"type","number"},{"minimum",1},{"maximum",60}}}
  },{"document_id","revision","feature_id","kernel_version","model_sha256","native_build","units"});
  auto external=object({{"review_sha256",hash()},{"source",{{"$ref","#/$defs/artifact_review_source"}}},{"configuration",configuration_schema()}},{"review_sha256","source","configuration"});
  auto defs=model_definitions();defs.update(artifact_review_definitions());
  auto animation=visual_animation_schema();animation["properties"]["max_frames"]["maximum"]=256;
  auto input=object({
    {"source",input_source_schema()},{"format",{{"enum",{"png","gif"}}}},{"animation",animation},{"loop",{{"type","boolean"}}},
    {"settings",settings_schema()},{"inline_image",{{"type","boolean"}}}
  },{"source"});
  Json gif_condition={{"properties",{{"format",{{"const","gif"}}}}},{"required",{"format"}}};
  Json gif_rules={{"required",{"animation"}},{"properties",{
    {"source",{{"required",{"document_id"}}}},
    {"settings",{{"not",{{"required",{"presentation"}}}},{"properties",{{"view",{{"const","single"}}}}}}}
  }}};
  Json animation_condition={{"anyOf",Json::array({Json{{"required",{"animation"}}},Json{{"required",{"loop"}}}})}};
  Json animation_rules={{"required",{"format"}},{"properties",{{"format",{{"const","gif"}}}}}};
  input["allOf"]=Json::array({Json{{"if",gif_condition},{"then",gif_rules}},Json{{"if",animation_condition},{"then",animation_rules}}});
  input["$defs"]=defs;
  auto output=object({
    {"source",{{"oneOf",Json::array({native,external})}}},{"format",{{"enum",{"png","gif"}}}},{"path",{{"type","string"}}},
    {"width",{{"type","integer"},{"minimum",16},{"maximum",2048}}},{"height",{{"type","integer"},{"minimum",16},{"maximum",2048}}},
    {"bytes",{{"type","integer"},{"minimum",1},{"maximum",gif_limit}}},{"sha256",hash()},{"method",{{"const","native_cpu_rasterizer"}}},
    {"sheet",sheet_schema()},
    {"frame_count",{{"type","integer"},{"minimum",2},{"maximum",256}}},{"duration_s",{{"type","number"},{"exclusiveMinimum",0},{"maximum",3600}}},
    {"loop",{{"type","boolean"}}},{"palette",{{"const","RGB 3-3-2; alpha composited onto white"}}},
    {"image",object({{"type",{{"const","image"}}},{"mimeType",{{"enum",{"image/png","image/gif"}}}},{"data",{{"type","string"},{"maxLength",(inline_limit+2)/3*4}}}},{"type","mimeType","data"})},
    {"image_omitted_reason",{{"enum",{"disabled","exceeds_inline_limit"}}}},
    {"downloads",{{"type","array"},{"maxItems",1},{"items",download_link_schema()}}},
    {"download_errors",{{"type","array"},{"maxItems",1},{"items",object({{"name",{{"type","string"}}},{"code",{{"type","string"}}},{"message",{{"type","string"}}}},{"name","code","message"})}}}
  },{"source","format","path","width","height","bytes","sha256","method"});
  output["allOf"]=Json::array({Json{{"if",gif_condition},{"then",{{"required",{"frame_count","duration_s","loop","palette"}},{"properties",{{"source",{{"required",{"sequence_sha256","sequence_name","sample_rate_hz"}}}}}}}}}});
  output["$defs"]=defs;
  return {{"name","cad_render"},{"description","Render a source-qualified native revision or verified external review to PNG without an open viewer. settings.view:sheet makes labeled front/top/right/iso views at one fitted orthographic scale; omit camera, use PNG and at least 128x128. Native saved sequences also render to GIF (2–256 frames, 64 MiB); sequence presentation owns clipping and exploded placement. A bounded native worker returns image content up to 2 MiB plus an independent file. No model revision changes."},
    {"inputSchema",input},{"outputSchema",output},{"annotations",{{"readOnlyHint",true},{"destructiveHint",false},{"openWorldHint",false}}}};
}
void validate_render_arguments(const Json& args){
  fields(args,{"source"},{"settings","inline_image","format","animation","loop"});
  if(args.contains("inline_image")&&!args.at("inline_image").is_boolean())throw Error("invalid_argument","inline_image must be boolean");
  const auto format=args.value("format",std::string("png"));
  if(format!="png"&&format!="gif")throw Error("unsupported_format","Headless visual output supports PNG and native-sequence GIF");
  if(args.contains("loop")&&!args.at("loop").is_boolean())throw Error("invalid_argument","loop must be boolean");
  const auto& source=args.at("source");
  if(format=="gif"){
    if(!source.contains("document_id")||!args.contains("animation"))throw Error("invalid_argument","GIF requires a native revision and saved animation sequence");
    validate_visual_animation(args.at("animation"));
    if(args.at("animation").value("max_frames",256)>256)throw Error("limit_exceeded","GIF supports at most 256 frames");
    if(args.value("settings",Json::object()).contains("presentation"))throw Error("invalid_argument","GIF presentation is captured from its saved sequence");
  }else if(args.contains("animation")||args.contains("loop"))throw Error("invalid_argument","animation and loop require GIF output");
  if(source.contains("document_id")){
    fields(source,{"document_id","revision"},{"feature_id"});portable_identifier(text_field(source,"document_id"));revision_number(source.at("revision"));
    if(source.contains("feature_id"))model_identifier(text_field(source,"feature_id"));
  }else{
    fields(source,{"review_path","expected_sha256"},{"configuration"});
    const auto path=path_from_utf8(text_field(source,"review_path"));const auto digest=text_field(source,"expected_sha256");
    if(!path.is_absolute()||path.filename()!="review.json"||digest.size()!=64||digest.find_first_not_of("abcdef0123456789")!=std::string::npos)
      throw Error("invalid_argument","Rendering an external review needs absolute review.json and its lowercase SHA-256");
    if(source.contains("configuration")&&!source.at("configuration").is_object())throw Error("invalid_argument","External pose configuration must be an object");
  }
  const auto settings=args.value("settings",Json::object());
  fields(settings,{},{"width","height","view","camera","render","presentation","appearance","hidden_part_ids"});
  for(const auto* key:{"width","height"})if(settings.contains(key)&&(!settings.at(key).is_number_integer()||settings.at(key)<16||settings.at(key)>2048))
    throw Error("invalid_argument","Headless image dimensions must be 16 to 2048 pixels");
  normalize_render_settings(settings.value("render",Json::object()));
  if(render_is_sheet(settings)&&format!="png")throw Error("invalid_argument","Sheet view requires PNG output");
}
Json render_visual_output(Store& store,const Json& args){
  validate_render_arguments(args);
  const auto& requested=args.at("source");const auto settings=args.value("settings",Json::object());
  const auto format=args.value("format",std::string("png"));const auto file_limit=format=="gif"?gif_limit:png_limit;
  Json source,model,request;
  if(requested.contains("document_id")){
    const auto record=store.read(text_field(requested,"document_id"),revision_number(requested.at("revision")));
    const auto feature=requested.value("feature_id",text_field(record.at("model"),"output"));model=record.at("model");
    source={{"document_id",record.at("document_id")},{"revision",record.at("revision")},{"feature_id",feature},{"kernel_version","8.0.1"},
      {"model_sha256",sha256(model.dump())},{"native_build",AGENTCAD_CACHE_BUILD},{"units","mm"}};
    request={{"kind","render"},{"feature_id",feature},{"identity",source},{"options",settings}};
    if(format=="gif"){
      auto animation=args.at("animation");animation["max_frames"]=animation.value("max_frames",256)+1;
      const auto captured=capture_visual_sequence(store,animation,record,feature,true);
      source["sequence_sha256"]=captured.at("sequence_sha256");source["sequence_name"]=animation.at("sequence_name");source["sample_rate_hz"]=captured.at("sample_rate_hz");
      request["kind"]="render_animation";request["identity"]=source;
      request["options"]={{"settings",settings},{"animation",captured},{"loop",args.value("loop",false)}};
    }
  }else{
    const auto path=path_from_utf8(text_field(requested,"review_path"));const auto digest=text_field(requested,"expected_sha256");
    const auto verified=verify_external_artifact(path.parent_path(),digest);
    if(verified.at("sha256")!=digest)throw Error("artifact_mismatch","External rendering source changed");
    const auto raw=read_text(path,artifact_input_limit);
    if(sha256(raw)!=digest)throw Error("artifact_mismatch","Review bytes changed after verification");
    const auto review=parse_json(raw,artifact_input_limit);const auto configuration=requested.value("configuration",Json::object());
    source={{"review_sha256",digest},{"source",review.at("source")},{"configuration",configuration}};
    model={{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",Json::array({{{"id","placeholder"},{"type","box"},{"size",{1,1,1}}}})},{"output","placeholder"}};
    request={{"kind","external_render"},{"identity",source},{"options",settings},{"guides",review}};
  }
  const auto exports=store.root()/"exports";
  if(fs::is_symlink(fs::symlink_status(exports)))throw Error("storage_error","Render export directory cannot be a symlink");
  directory(exports);
  const auto stage=temporary_file(exports),target=exports/(token()+"."+format);request["path"]=path_to_utf8(stage);
  try{
    // PNG writes into a reserved file. The GIF encoder publishes exclusively
    // into an absent destination; release only this freshly reserved name.
    if(format=="gif"&&!fs::remove(stage))throw Error("storage_error","Cannot release reserved GIF staging path");
    const auto generated=evaluate_model(store.root(),model,request).at("render");const auto bytes=read_text(stage,file_limit);
    std::uint32_t width=0,height=0;
    if(format=="png"){const auto image=decode_png(bytes);width=image.width;height=image.height;}
    else{
      if(bytes.size()<14||bytes.substr(0,6)!="GIF89a"||static_cast<unsigned char>(bytes.back())!=0x3b)throw Error("artifact_mismatch","Native GIF output is incomplete");
      width=static_cast<unsigned char>(bytes[6])|(static_cast<unsigned char>(bytes[7])<<8);
      height=static_cast<unsigned char>(bytes[8])|(static_cast<unsigned char>(bytes[9])<<8);
    }
    if(width!=generated.at("width")||height!=generated.at("height")||bytes.size()!=generated.at("bytes")||sha256(bytes)!=text_field(generated,"sha256"))
      throw Error("artifact_mismatch","Native rendered image identity differs");
    Json result={{"source",source},{"format",format},{"path",path_to_utf8(target)},{"width",width},{"height",height},{"bytes",bytes.size()},{"sha256",sha256(bytes)},{"method","native_cpu_rasterizer"}};
    if(generated.contains("sheet"))result["sheet"]=generated.at("sheet");
    if(format=="gif")for(const auto* key:{"frame_count","duration_s","loop","palette"})result[key]=generated.at(key);
    check_job_cancelled();publish_file(stage,target);
    if(args.value("inline_image",true)&&bytes.size()<=inline_limit)result["image"]={{"type","image"},{"mimeType","image/"+format},{"data",image_base64(bytes)}};
    else result["image_omitted_reason"]=args.value("inline_image",true)?"exceeds_inline_limit":"disabled";
    if(source.contains("document_id")){
      auto link=source;link["path"]=path_to_utf8(target);attach_export_downloads(store,link);
      result["downloads"]=link.at("downloads");result["download_errors"]=link.at("download_errors");
    }
    return result;
  }catch(...){std::error_code ignored;fs::remove(stage,ignored);throw;}
}
}
