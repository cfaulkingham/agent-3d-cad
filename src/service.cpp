#include "agentcad/service.hpp"
#include "agentcad/authoring.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/print_export.hpp"
#include "agentcad/model.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/viewer.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/live.hpp"
#include "agentcad/drawing.hpp"
#include "agentcad/bom.hpp"
#include "agentcad/robot.hpp"
#include "agentcad/manufacturing.hpp"
#include "agentcad/fabrication.hpp"
#include "agentcad/gcode.hpp"
#include "agentcad/printer.hpp"
#include "agentcad/slicer.hpp"
#include "agentcad/measurement.hpp"
#include "agentcad/artifact_review.hpp"
#include <fstream>
#include <cctype>
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
    {"assembly",{{"$ref","#/$defs/assembly_summary"}}},
    {"components",array({{"$ref","#/$defs/component_status"}},64)}},
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
  metadata["_native_build"]=AGENTCAD_CACHE_BUILD;metadata["_model_sha256"]=sha256(record.at("model").dump());
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
  const Json id={{"$ref","#/$defs/model_id"}};
  const Json revision={{"$ref","#/$defs/revision"}};
  const Json text={{"type","string"}};
  auto definitions=model_definitions();
  definitions["authoring_feature"]=object({{"id",{{"$ref","#/$defs/model_id"}}},{"type",{{"const","sketch"}}},
    {"workplane",{{"$ref","#/$defs/workplane"}}},
    {"profile",{{"oneOf",authoring_profile_schemas({{"$ref","#/$defs/scalar"}})}}}}, {"id","type","workplane","profile"});
  definitions["revision"]={{"type","integer"},{"minimum",1},{"maximum",9007199254740991ULL}};
  definitions["drawing_spec"]=drawing_schema();
  definitions["manufacturing_options"]=manufacturing_options_schema();
  definitions.update(fabrication_definitions());
  definitions.update(gcode_definitions());
  definitions.update(printer_definitions());
  definitions.update(slicer_definitions());
  definitions.update(measurement_definitions());
  definitions.update(artifact_review_definitions());
  definitions["artifact_arguments"]={{"type","object"},{"oneOf",Json::array({
    Json{{"$ref","#/$defs/artifact_review_arguments"}},
    object({{"action",{{"const","verify"}}},{"review_path",{{"type","string"}}},{"expected_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}}},{"action","review_path","expected_sha256"})})}};
  const Json hash={{"type","string"},{"pattern","^[a-f0-9]{64}$"}},bytes={{"type","integer"},{"minimum",1},{"maximum",gcode_bytes_limit}};
  definitions["gcode_result"]=object({{"document_id",id},{"revision",revision},{"feature_id",id},
    {"kernel_version",{{"const","8.0.1"}}},{"model_sha256",hash},{"native_build",text},
    {"gcode_sha256",hash},{"gcode_bytes",{{"type","integer"},{"minimum",0},{"maximum",gcode_bytes_limit}}},{"path",text},{"artifact_path",text},{"source_path",text},
    {"report_sha256",hash},{"report_bytes",bytes},{"report",{{"$ref","#/$defs/gcode_report"}}}},
    {"document_id","revision","feature_id","kernel_version","model_sha256","native_build","gcode_sha256","gcode_bytes","path","artifact_path","source_path","report_sha256","report_bytes","report"});
  const Json slice_identity={{"document_id",id},{"revision",revision},{"feature_id",id},{"kernel_version",{{"const","8.0.1"}}},
    {"model_sha256",hash},{"native_build",text},{"directory",text},{"path",text},{"physical_print_started",{{"const",false}}}};
  auto slice_plan=slice_identity;slice_plan["action"]={{"const","plan"}};slice_plan["sha256"]=hash;slice_plan["execution"]={{"type","object"}};
  auto slice_run=slice_identity;slice_run["action"]={{"const","run"}};slice_run["gcode_path"]=text;slice_run["gcode_sha256"]=hash;
  slice_run["gcode_bytes"]=bytes;slice_run["plan_sha256"]=hash;slice_run["report"]={{"$ref","#/$defs/gcode_report"}};
  auto closed_required=[](const Json& properties){Json required=Json::array();for(const auto& item:properties.items())required.push_back(item.key());return object(properties,required);};
  definitions["slice_result"]={{"type","object"},{"oneOf",Json::array({closed_required(slice_plan),closed_required(slice_run)})}};
  definitions["fabrication_result"]=object({{"document_id",id},{"revision",revision},{"feature_id",id},
    {"kernel_version",{{"const","8.0.1"}}},{"units",{{"const","mm"}}},
    {"model_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},{"native_build",text},
    {"report",{{"$ref","#/$defs/fabrication_report"}}},{"path",text},
    {"sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},{"bytes",{{"type","integer"},{"minimum",1},{"maximum",67108864}}}},
    {"document_id","revision","feature_id","kernel_version","units","model_sha256","native_build","report","path","sha256","bytes"});
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
  auto preview_output=object(identity,{"schema_version","document_id","revision","kernel_version","evaluation_id","feature_id","draft","selection_lifetime","summary"});
  preview_output["properties"]["draft"]={{"const",true}};
  preview_output["oneOf"]=Json::array({Json{{"required",{"path","data_path"}}},Json{{"required",{"mesh","topology"}}}});
  Json tools=Json::array({
    tool("cad_create","Build editable parts or an assembly with rigid or articulated frame mates, and commit revision 1. Optional request_id deduplicates retries.",
      {{"document_id",id},{"model",{{"$ref","#/$defs/model"}}},{"request_id",id}}, {"document_id","model"},
      object(record_properties,{"schema_version","document_id","revision","kernel_version","model","summary"}),false),
    tool("cad_read","Read saved editable intent. Omit revision to read HEAD.",
      {{"document_id",id},{"revision",revision}},{"document_id"},
      object(record_properties,{"schema_version","document_id","revision","kernel_version","model"}),true),
    tool("cad_apply","Build atomic semantic edits, including revision-pinned editable components, and commit only if expected_revision still matches. Failures preserve HEAD.",
      {{"document_id",id},{"expected_revision",revision},{"operations",operations},{"request_id",id}},
      {"document_id","expected_revision","operations"},object(record_properties,{"schema_version","document_id","revision","kernel_version","model","summary"}),false),
    tool("cad_restore","Rebuild a historical model as a new revision. History remains immutable; expected_revision must match HEAD.",
      {{"document_id",id},{"expected_revision",revision},{"source_revision",revision},{"request_id",id}},
      {"document_id","expected_revision","source_revision"},object(record_properties,{"schema_version","document_id","revision","kernel_version","model","summary"}),false),
    tool("cad_inspect_step","Inspect a local STEP before importing, including invalid geometry. Reports per-solid validity, tessellation, bounds and diagnostic entities. Source solid indices are scoped to the returned SHA-256 and pinned kernel; use that hash with cad_import solid_indices. No repair, document or revision is created.",
      {{"path",text},{"expected_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}}}, {"path"},
      object({{"source_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},{"kernel_version",text},{"index_lifetime",{{"const","source_sha256_and_kernel"}}},
        {"valid",{{"type","boolean"}}},{"meshable",{{"type","boolean"}}},{"solid_count",{{"type","integer"},{"minimum",0},{"maximum",4096}}},
        {"errors",array(object({{"code",text},{"message",text},{"details",{{"type","object"}}}},{"code","message","details"}),64)},
        {"solids",array(object({{"index",{{"type","integer"},{"minimum",1},{"maximum",4096}}},{"valid",{{"type","boolean"}}},{"meshable",{{"type","boolean"}}},
          {"bounds_mm",summary_schema().at("properties").at("bounds_mm")},{"volume_mm3",{{"type",{"number","null"}}}},
          {"errors",array(object({{"code",text},{"message",text},{"details",{{"type","object"}}}},{"code","message","details"}),64)}},
          {"index","valid","meshable","bounds_mm","volume_mm3","errors"}),4096)}},
        {"source_sha256","kernel_version","index_lifetime","valid","meshable","solid_count","solids","errors"}),true),
    tool("cad_import_sketch","Capture a local font, SVG or ASCII DXF and atomically append its exact editable sketch to an existing document. Requires expected_revision; validates every source contour in a bounded worker before publication. Returns compact revision/source identity; cad_read retrieves embedded bytes. Then extrude, cut or thicken by feature_id through ordinary cad_apply. Optional request_id deduplicates retries and source deletion does not affect rebuilds.",
      {{"document_id",id},{"expected_revision",revision},{"request_id",id},{"format",{{"enum",{"text","svg","dxf"}}}},{"path",text},{"feature_id",id},{"workplane",{{"$ref","#/$defs/workplane"}}},{"expected_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},{"text",{{"type","string"},{"minLength",1},{"maxLength",256}}},{"height",{{"$ref","#/$defs/scalar"}}},{"spacing",{{"$ref","#/$defs/scalar"}}},{"scale",{{"$ref","#/$defs/scalar"}}},{"face_index",{{"type","integer"},{"minimum",0},{"maximum",31}}}}, {"document_id","expected_revision","format","path","feature_id","workplane"},
      object({{"schema_version",{{"const",1}}},{"document_id",id},{"revision",revision},{"kernel_version",{{"const","8.0.1"}}},{"feature_id",id},{"source_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},{"summary",summary_schema()}},{"schema_version","document_id","revision","kernel_version","feature_id","source_sha256","summary"}),false),
    tool("cad_capture_sketch","Capture a local font, SVG or ASCII DXF as a portable editable sketch feature. Embeds exact source bytes and SHA-256, validates closed exact planar geometry in a bounded native worker, and returns a feature for cad_create/add_feature. Text uses captured unhinted Unicode font outlines with editable text/height; SVG/DXF support documented filled planar curves and reject unsupported entities. No document is committed.",
      {{"format",{{"enum",{"text","svg","dxf"}}}},{"path",text},{"feature_id",id},{"workplane",{{"$ref","#/$defs/workplane"}}},{"expected_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},{"text",{{"type","string"},{"minLength",1},{"maxLength",256}}},{"height",{{"type","number"},{"minimum",0.00001},{"maximum",100000}}},{"spacing",{{"type","number"},{"minimum",-1000000},{"maximum",1000000}}},{"scale",{{"type","number"},{"minimum",0.000001},{"maximum",1000000}}},{"face_index",{{"type","integer"},{"minimum",0},{"maximum",31}}}}, {"format","path","feature_id","workplane"},
      object({{"feature",{{"$ref","#/$defs/authoring_feature"}}},{"source_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},{"contour_count",{{"type","integer"},{"minimum",1},{"maximum",128}}},{"segment_count",{{"type","integer"},{"minimum",1},{"maximum",8192}}}}, {"feature","source_sha256","contour_count","segment_count"}),true),
    tool("cad_import","Create a document from a local STEP file without a fixed source byte limit; geometry runs within the job memory/time budget. Preserves exact source bytes and SHA-256. Optional expected_sha256 verifies the downloaded artifact; purchase binds caller supplier/part/source identity to those bytes for assemblies and packages. Optional solid_indices explicitly extracts a subset discovered by cad_inspect_step and requires expected_sha256. Does not fetch URLs or infer editable history.",
      {{"document_id",id},{"path",text},{"request_id",id},{"expected_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},{"purchase",{{"$ref","#/$defs/purchase"}}},{"solid_indices",{{"type","array"},{"items",{{"type","integer"},{"minimum",1},{"maximum",4096}}},{"minItems",1},{"maxItems",4096},{"uniqueItems",true}}}}, {"document_id","path"},
      object(record_properties,{"schema_version","document_id","revision","kernel_version","model","summary"}),false),
    tool("cad_query","Query a committed revision. Topology and mesh IDs belong only to the returned evaluation. Optional feature_id scopes geometry.",
      {{"document_id",id},{"revision",revision},{"kind",{{"enum",{"summary","topology","mesh"}}}},{"feature_id",id}},
      {"document_id","revision"},object(identity,{"document_id","revision","kernel_version","feature_id","summary"}),true),
    tool("cad_measure","Measure committed B-reps: face/edge/leaf pair distances, analytic angles, common material volume, or planar section curves and material caps. Clearance queries cover at most 23 leaves. Section queries use an explicit displayed-world plane and optional exploded leaf offsets; distance queries always use the saved source pose. Reject stale/draft/build-mismatched references and ambiguous recovery.",
      {{"document_id",id},{"revision",revision},{"evaluation_id",id},{"feature_id",id},{"query",{{"$ref","#/$defs/measurement_query"}}}},
      {"document_id","revision","evaluation_id","feature_id","query"},definitions.at("measurement_result"),true),
    tool("cad_export","Export a committed revision or feature as STEP, binary STL or separate-solid 3MF. Optional 3MF layout packs a rectangular bed across plates or reuses complete explicit placements. Geometry-only files retain mm units; no slicing or hardware actions.",
      {{"document_id",id},{"revision",revision},{"format",{{"enum",{"step","stl","3mf"}}}},{"feature_id",id},{"layout",print_layout_schema()}},{"document_id","revision","format"},
      object({{"document_id",id},{"revision",revision},{"format",{{"enum",{"step","stl","3mf"}}}},{"path",text},{"bytes",{{"type","integer"},{"minimum",1}}},{"units",{{"const","mm"}}},
        {"layout_path",text},{"plates",array(object({{"plate",{{"type","integer"},{"minimum",1},{"maximum",64}}},{"path",text},{"bytes",{{"type","integer"},{"minimum",1}}},{"sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},{"source_ids",array(text,4096)}},{"plate","path","bytes","sha256","source_ids"}),64)}},
        {"document_id","revision","format","path","bytes","units"}),false),
    tool("cad_manufacture","Export a revision-qualified manufacturing package with editable source, unique leaf STEP/STL/drawings, saved assembly placement, BOM/purchasing data, explicit process assumptions and a portable SHA-256 manifest. Defaults to both solid formats and native drawings. Does not perform process certification, slicing or physical printing.",
      {{"document_id",id},{"revision",revision},{"feature_id",id},{"options",{{"$ref","#/$defs/manufacturing_options"}}}}, {"document_id","revision"},
      object({{"document_id",id},{"revision",revision},{"kernel_version",{{"const","8.0.1"}}},{"feature_id",id},{"units",{{"const","mm"}}},
        {"directory",text},{"path",text},{"model_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},
        {"part_count",{{"type","integer"},{"minimum",1},{"maximum",manufacturing_part_limit}}},
        {"artifact_count",{{"type","integer"},{"minimum",3},{"maximum",manufacturing_file_limit}}},
        {"bytes",{{"type","integer"},{"minimum",1},{"maximum",manufacturing_bytes_limit}}},
        {"artifacts",array(object({{"format",{{"enum",{"step","stl","pdf","svg","dxf","json","csv"}}}},
          {"path",text},{"bytes",{{"type","integer"},{"minimum",1}}},{"sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}}},
          {"format","path","bytes","sha256"}),manufacturing_file_limit)}},
        {"document_id","revision","kernel_version","feature_id","units","directory","path","model_sha256","part_count","artifact_count","bytes","artifacts"}),false),
    tool("cad_fabrication_review","Measure a committed revision against explicit FDM, CNC, sheet/laser or molding inputs. Reports exact or sampled evidence, unknown unsupported checks, saved-pose interference/clearance and a hashed JSON artifact. Assembly parts are reviewed in source coordinates. Does not certify production or start hardware.",
      {{"document_id",id},{"revision",revision},{"feature_id",id},{"options",{{"$ref","#/$defs/fabrication_options"}}}},{"document_id","revision","options"},
      {{"type","object"},{"$ref","#/$defs/fabrication_result"}},false),
    tool("cad_gcode_review","Inspect checksummed local plain G-code against explicit machine/material/initial state. Preserve original bytes and a native static report; firmware unknowns remain unknown. The caller declares its CAD association. Never slices, executes G-code or starts hardware.",
      {{"document_id",id},{"revision",revision},{"feature_id",id},{"path",text},{"expected_sha256",hash},{"options",{{"$ref","#/$defs/gcode_options"}}}},
      {"document_id","revision","path","expected_sha256","options"},{{"type","object"},{"$ref","#/$defs/gcode_result"}},false),
    tool("cad_printer_handoff","Prepare or re-verify a checksummed offline printer handoff package from plain G-code, explicit machine/profile/material inputs and a caller-declared committed CAD association. Recompute native static findings; physical readiness remains unknown. Never contacts, uploads to or starts hardware.",
      {},{},{{"type","object"},{"$ref","#/$defs/printer_result"}},false),
    tool("cad_slice","Plan then execute installed OrcaSlicer 2.4.2 for one committed source solid. Explicit checksummed executable and resolved native machine/process/filament profiles, fixed argv, private settings, bounded cancellable native processes, actual G-code review and portable provenance. Run requires the reviewed plan hash. Never contacts printers.",
      {{"document_id",id},{"revision",revision},{"action",{{"enum",{"plan","run"}}}},{"feature_id",id},{"options",{{"$ref","#/$defs/slice_options"}}},{"plan_path",text},{"expected_sha256",hash}},
      {"document_id","revision","action"},{{"type","object"},{"$ref","#/$defs/slice_result"}},false),
    tool("cad_robot_export","Export a committed assembly as URDF with paired SRDF, or SDF 1.12, plus local STL meshes and a frame/coordinate ledger. Exported zero reproduces the saved pose; limits, named poses and couplings are converted to SI. Requires explicit effort/velocity for every moving coordinate. SDF additionally requires inertials for every part and cylindrical carrier. No physical properties or planning configuration are inferred.",
      {{"document_id",id},{"revision",revision},{"feature_id",id},{"robot",robot_options_schema()}},{"document_id","revision","robot"},
      object({{"document_id",id},{"revision",revision},{"kernel_version",{{"const","8.0.1"}}},{"feature_id",id},
        {"format",{{"enum",{"urdf","srdf","sdf"}}}},{"path",text},{"directory",text},{"manifest_path",text},
        {"artifacts",array(object({{"format",{{"enum",{"urdf","srdf","sdf","stl","json"}}}},{"path",text},
          {"bytes",{{"type","integer"},{"minimum",1}}},{"sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}}},
          {"format","path","bytes","sha256"}),67)}},
        {"document_id","revision","kernel_version","feature_id","format","path","directory","manifest_path","artifacts"}),false),
    tool("cad_bom","Export a committed assembly bill of materials as JSON and CSV. Group named part instances by source feature, preserve explicit metadata and item numbers, and never infer material.",
      {{"document_id",id},{"revision",revision},{"feature_id",id}},{"document_id","revision"},
      object({{"document_id",id},{"revision",revision},{"kernel_version",{{"const","8.0.1"}}},{"units",{{"const","mm"}}},
        {"bom",{{"$ref","#/$defs/bom"}}},{"path",text},
        {"artifacts",{{"type","array"},{"minItems",2},{"maxItems",2},{"items",object({{"format",{{"enum",{"json","csv"}}}},{"path",text},{"bytes",{{"type","integer"},{"minimum",1}}}}, {"format","path","bytes"})}}}},
        {"document_id","revision","kernel_version","units","bom","artifacts","path"}),false),
    tool("cad_drawing","Generate a vector drawing of a committed revision, including exploded assembly views, bills of materials and geometry-checked numbered balloons. Measured dimensions, explicit tolerances, aligned layouts, hidden-line views, hatched sections, PDF/SVG sheets and per-view 1:1 mm DXF. Save the recipe to regenerate after edits; never changes the model.",
      {{"document_id",id},{"revision",revision},{"drawing",{{"$ref","#/$defs/drawing_spec"}}}},{"document_id","revision"},
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
        {"balloons",array(object({{"view",id},{"part_id",occurrence_schema()},{"item_number",{{"type","integer"},{"minimum",1},{"maximum",999}}},
          {"anchor_mm",{{"type","array"},{"items",{{"type","number"}}},{"minItems",2},{"maxItems",2}}},
          {"label_mm",{{"type","array"},{"items",{{"type","number"}}},{"minItems",2},{"maxItems",2}}}},
          {"view","part_id","item_number","anchor_mm","label_mm"}),64)}},
        {"document_id","revision","kernel_version","units","scale","sheet_mm","layout","view_layouts","path","recipe_path","artifacts","dimensions","projection_tolerance_mm"}),false),
    tool("cad_view","Save an offline interactive HTML viewer and .view.json. Pick a face or edge and copy/save its revision-qualified reference.",
      {{"document_id",id},{"revision",revision},{"feature_id",id}},{"document_id","revision"},
      object(identity,{"schema_version","document_id","revision","evaluation_id","feature_id","draft","summary","path","data_path"}),false),
    tool("cad_preview","Build edits without committing. Returns a draft viewer; draft picks cannot be used as committed references.",
      {{"document_id",id},{"expected_revision",revision},{"operations",operations},{"feature_id",id},{"kind",{{"enum",{"view","mesh"}}}}},
      {"document_id","expected_revision","operations"},preview_output,true),
    tool("cad_resolve_selection","Resolve a saved evaluation pick. Reject stale revisions, draft picks and mismatched evaluations. Persistent selectors are geometric rules, never pick tokens.",
      reference_properties,{"document_id","revision","evaluation_id","feature_id","kind","entity_id"},
      object({{"reference",reference},{"geometry",{{"oneOf",Json::array({Json{{"$ref","#/$defs/face"}},Json{{"$ref","#/$defs/edge"}}})}}},{"selector",{{"oneOf",Json::array({Json{{"$ref","#/$defs/selector"}},Json{{"$ref","#/$defs/face_selector"}}})}}}},{"reference","geometry"}),true),
    tool("cad_compare","Compare committed revisions by parameters, named features, output and geometric measurements.",
      {{"document_id",id},{"from_revision",revision},{"to_revision",revision}},{"document_id","from_revision","to_revision"},
      object({{"document_id",id},{"from_revision",revision},{"to_revision",revision},{"parameters",{{"type","object"}}},
        {"features",array(id,512)},{"output_changed",{{"type","boolean"}}},{"volume_delta_mm3",{{"type","number"}}},{"area_delta_mm2",{{"type","number"}}}},
        {"document_id","from_revision","to_revision","parameters","features","output_changed","volume_delta_mm3","area_delta_mm2"}),true),
    tool("cad_artifact","Review original STEP/STL/3MF/GLB/DXF/URDF/SDF/SRDF as bounded, source-hashed read-only geometry/data, or verify a portable captured review. Explicit units and reference hashes are required. No editable document/history, original face selectors or executed artifact code. Submit review through cad_job for large inputs.",
      {{"action",{{"type","string"}}}},{"action"},{{"type","object"},{"$ref","#/$defs/artifact_review_result"}},false),
    tool("cad_job","Submit, inspect, list or cancel a durable job. Submit a tool and arguments with request_id; retries return the same job. Geometry runs in bounded native workers.",
      {{"action",{{"enum",{"submit","get","cancel","list"}}}},{"request_id",id},{"job_id",id},{"tool",text},{"arguments",{{"type","object"}}},
        {"budget",object({{"timeout_ms",{{"type","integer"},{"minimum",1},{"maximum",300000}}},{"memory_mb",{{"type","integer"},{"minimum",128},{"maximum",std::numeric_limits<int>::max()}}}},Json::array())}},
      {"action"},{{"type","object"}},false)
  });
  const auto budgets=object({{"timeout_ms",{{"type","integer"},{"minimum",1},{"maximum",300000}}},
    {"memory_mb",{{"type","integer"},{"minimum",128},{"maximum",std::numeric_limits<int>::max()}}}},Json::array());
  for(auto& definition:tools)if(definition.at("name")=="cad_artifact") {
    definition["inputSchema"]={{"type","object"},{"$ref","#/$defs/artifact_arguments"},{"$defs",definitions}};
  }
  for(auto& definition:tools)if(definition.at("name")=="cad_slice") {
    definition["inputSchema"]={{"type","object"},{"oneOf",Json::array({
      object({{"document_id",id},{"revision",revision},{"action",{{"const","plan"}}},{"feature_id",id},{"options",{{"$ref","#/$defs/slice_options"}}}},{"document_id","revision","action","options"}),
      object({{"document_id",id},{"revision",revision},{"action",{{"const","run"}}},{"plan_path",text},{"expected_sha256",hash}},{"document_id","revision","action","plan_path","expected_sha256"})})},{"$defs",definitions}};
  }
  for(auto& definition:tools)if(definition.at("name")=="cad_printer_handoff")
    definition["inputSchema"]={{"type","object"},{"$ref","#/$defs/printer_arguments"},{"$defs",definitions}};
  for(auto& tool:tools) {
    if(tool.at("name")=="cad_import"){tool["inputSchema"]["dependentRequired"]={{"solid_indices",{"expected_sha256"}}};tool["inputSchema"]["not"]={{"required",{"solid_indices","purchase"}}};}
    if(tool.at("name")=="cad_import_sketch"||tool.at("name")=="cad_capture_sketch")tool["inputSchema"]["allOf"]=Json::array({{{"if",{{"properties",{{"format",{{"const","text"}}}}}}},{"then",{{"required",{"text","height"}},{"not",{{"required",{"scale"}}}}}},{"else",{{"not",{{"anyOf",Json::array({Json{{"required",{"text"}}},Json{{"required",{"height"}}},Json{{"required",{"spacing"}}},Json{{"required",{"face_index"}}}})}}}}}}});
    if(tool.at("name")=="cad_export")tool["inputSchema"]["allOf"]=Json::array({{{"if",{{"required",{"layout"}}}},{"then",{{"properties",{{"format",{{"const","3mf"}}}}}}}}});
  }
  const std::set<std::string> job_tools={"cad_import_sketch","cad_capture_sketch","cad_inspect_step","cad_artifact","cad_create","cad_apply","cad_restore","cad_import","cad_query","cad_measure","cad_export","cad_manufacture","cad_fabrication_review","cad_gcode_review","cad_printer_handoff","cad_slice","cad_robot_export","cad_bom","cad_drawing","cad_preview","cad_view"};
  Json submits=Json::array(),results=Json::array();
  std::set<std::string> result_contracts;
  for(const auto& definition:tools) {
    const auto name=definition.at("name").get<std::string>();if(!job_tools.contains(name))continue;
    auto input=definition.at("inputSchema");input.erase("$defs");
    auto output=definition.at("outputSchema");output.erase("$defs");
    // Mutations share an identical committed-record contract. An anyOf only
    // needs each distinct result once; retain every tool's input discriminator.
    if(result_contracts.insert(output.dump()).second)results.push_back(output);
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
  // Get/submit/cancel and list return the same job contract. Reference it once
  // so adding a document capability does not duplicate every nested result.
  auto job_definitions=definitions;job_definitions["job"]=job;
  const Json job_reference={{"$ref","#/$defs/job"}};
  tools.back()["outputSchema"]={{"type","object"},{"oneOf",Json::array({job_reference,
    object({{"jobs",array(job_reference,1000)},{"limit",{{"const",1000}}}},{"jobs","limit"})})},{"$defs",job_definitions}};
  for (auto& definition : live_tool_definitions()) tools.push_back(std::move(definition));
  // Each standalone schema keeps only the model definitions it references.
  for(auto& definition:tools)for(const auto* key:{"inputSchema","outputSchema"}) {
    auto& schema=definition[key];prune_definitions(schema);
    // Common object constraints apply once around a union. A closed branch
    // that does not declare a property already forbids it, so it need not
    // repeat the constraint used by the other branches. Empty declarations
    // retain each branch's exact allowed field set and evaluation annotations.
    bool hoist_scoped=false;
    const auto schema_children=[](auto& node,const auto& apply) {
      for(auto& item:node.items()) {
        const auto& name=item.key();
        if((name=="$defs"||name=="properties"||name=="patternProperties"||name=="dependentSchemas")&&item.value().is_object())
          for(auto& child:item.value().items())apply(child.value());
        else if(name!="const"&&name!="enum"&&name!="default")apply(item.value());
      }
    };
    std::function<void(const Json&)> hoist_scope=[&](const Json& node) {
      if(node.is_array()){for(const auto& child:node)hoist_scope(child);return;}
      if(!node.is_object())return;
      for(const auto* key:{"$id","$anchor","$dynamicAnchor","$dynamicRef","$recursiveAnchor","$recursiveRef"})if(node.contains(key))hoist_scoped=true;
      schema_children(node,hoist_scope);
    };
    hoist_scope(schema);
    std::function<void(Json&)> hoist=[&](Json& node) {
      if(node.is_array()){for(auto& child:node)hoist(child);return;}
      if(!node.is_object())return;
      schema_children(node,hoist);
      if(node.contains("type")&&node.at("type")!="object")return;
      if(node.contains("unevaluatedProperties"))return;
      for(const auto* union_key:{"oneOf","anyOf"}) {
        if(!node.contains(union_key)||!node.at(union_key).is_array()||node.at(union_key).size()<2)continue;
        auto& branches=node[union_key];
        bool objects=true;
        for(const auto& branch:branches)if(!branch.is_object()||branch.value("type",Json())!="object"){objects=false;break;}
        if(!objects)continue;
        std::set<std::string> required;
        if(branches.front().contains("required"))for(const auto& name:branches.front().at("required"))required.insert(name.get<std::string>());
        for(const auto& branch:branches)for(auto it=required.begin();it!=required.end();) {
          if(!branch.contains("required")||std::find(branch.at("required").begin(),branch.at("required").end(),Json(*it))==branch.at("required").end())it=required.erase(it);else ++it;
        }
        std::set<std::string> candidates;
        for(const auto& branch:branches)if(branch.contains("properties"))for(const auto& property:branch.at("properties").items())candidates.insert(property.key());
        Json common=Json::object();
        for(const auto& name:candidates) {
          Json constraint;std::size_t copies=0;bool identical=true;
          for(const auto& branch:branches) {
            if(branch.contains("properties")&&branch.at("properties").contains(name)) {
              const auto& value=branch.at("properties").at(name);
              if(copies&&value!=constraint){identical=false;break;}
              constraint=value;++copies;
            } else if(branch.value("additionalProperties",Json())!=false||branch.contains("patternProperties")) {
              identical=false;break;
            }
          }
          if(!identical||copies<2||constraint==Json::object())continue;
          const bool declared=node.contains("properties")&&node.at("properties").contains(name);
          if((declared&&node.at("properties").at(name)!=constraint)||
             (!declared&&node.contains("additionalProperties")&&node.at("additionalProperties")!=true))continue;
          // Hoist only when the actual serialized property declarations shrink.
          const auto size=constraint.dump().size();
          if(!declared&&copies*(size-2)<=size+name.size()+4)continue;
          common[name]=std::move(constraint);
        }
        node["type"]="object";
        for(const auto& property:common.items())node["properties"][property.key()]=property.value();
        if(!required.empty()) {
          if(!node.contains("required"))node["required"]=Json::array();
          for(const auto& name:required)if(std::find(node.at("required").begin(),node.at("required").end(),Json(name))==node.at("required").end())node["required"].push_back(name);
        }
        for(auto& branch:branches) {
          branch.erase("type");
          for(const auto& property:common.items())if(branch.contains("properties")&&branch.at("properties").contains(property.key()))branch["properties"][property.key()]=Json::object();
          if(branch.contains("required")) {
            Json remaining=Json::array();for(const auto& name:branch.at("required"))if(!required.contains(name.get<std::string>()))remaining.push_back(name);
            if(remaining.empty())branch.erase("required");else branch["required"]=std::move(remaining);
          }
        }
      }
    };
    if(!hoist_scoped)hoist(schema);
    // Intern profitable repeated schema subtrees within each standalone schema.
    // Every constraint is retained; only structurally identical nodes share a
    // reference. This keeps discovery bounded as fabrication contracts grow.
    std::map<std::string,std::pair<Json,std::size_t>> repeated;
    const auto eligible=[](const Json& value) {
      if(!value.is_object()||value.contains("$defs")||value.dump().size()<24)return false;
      if(value.contains("type")&&(value.at("type").is_string()||value.at("type").is_array()))return true;
      if(value.contains("properties")&&value.at("properties").is_object()&&
        ((value.contains("required")&&value.at("required").is_array())||
         (value.contains("additionalProperties")&&value.at("additionalProperties").is_boolean())))return true;
      for(const auto* keyword:{"oneOf","anyOf","allOf","enum"})if(value.contains(keyword)&&value.at(keyword).is_array())return true;
      return false;
    };
    std::function<void(const Json&)> count=[&](const Json& value) {
      if(eligible(value)){auto& item=repeated[value.dump()];item.first=value;++item.second;}
      if(value.is_array())for(const auto& child:value)count(child);
      else if(value.is_object())for(const auto& item:value.items())if(item.key()!="const"&&item.key()!="enum"&&item.key()!="default")count(item.value());
    };
    count(schema);std::map<std::string,std::string> names;Json shared=Json::object();
    const auto alias=[](std::size_t ordinal) {
      constexpr std::string_view alphabet="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
      std::string result;
      do{result.insert(result.begin(),alphabet[ordinal%alphabet.size()]);ordinal/=alphabet.size();}while(ordinal);
      return result;
    };
    for(const auto& [raw,item]:repeated){
      auto name=alias(names.size());
      while(schema.contains("$defs")&&schema.at("$defs").contains(name))name+="_";
      const Json reference={{"$ref","#/$defs/"+name}};
      const auto reference_bytes=reference.dump().size();
      if(raw.size()>reference_bytes&&item.second>(raw.size()+name.size()+4)/(raw.size()-reference_bytes)) {
        names[raw]=name;shared[name]=item.first;
      }
    }
    std::function<void(Json&,bool)> replace=[&](Json& value,bool root) {
      if(!root&&eligible(value))if(const auto found=names.find(value.dump());found!=names.end()){value={{"$ref","#/$defs/"+found->second}};return;}
      if(value.is_array())for(auto& child:value)replace(child,false);
      else if(value.is_object())for(auto& item:value.items())if(item.key()!="const"&&item.key()!="enum"&&item.key()!="default")replace(item.value(),false);
    };
    replace(schema,true);
    for(auto& item:shared.items()){replace(item.value(),true);schema["$defs"][item.key()]=std::move(item.value());}
    prune_definitions(schema);
    // A parent alias can absorb occurrences counted before replacement. Price
    // each generated alias from its actual stored references and final body,
    // inlining the smallest unprofitable body before recounting. Original named
    // definitions and const/enum/default literals are never renamed or edited.
    for(;;) {
      std::map<std::string,std::size_t> references;
      std::function<void(const Json&)> count_references=[&](const Json& value) {
        if(value.is_array())for(const auto& child:value)count_references(child);
        else if(value.is_object()) {
          if(value.contains("$ref")&&value.at("$ref").is_string()) {
            const auto& ref=value.at("$ref").get_ref<const std::string&>();
            if(ref.starts_with("#/$defs/"))++references[ref.substr(8)];
          }
          for(const auto& item:value.items())if(item.key()!="const"&&item.key()!="enum"&&item.key()!="default")count_references(item.value());
        }
      };
      count_references(schema);
      std::string remove;std::size_t smallest=std::string::npos;
      for(const auto& item:shared.items())if(schema.contains("$defs")&&schema.at("$defs").contains(item.key())) {
        const auto body_bytes=schema.at("$defs").at(item.key()).dump().size();
        const auto reference_bytes=Json{{"$ref","#/$defs/"+item.key()}}.dump().size();
        const bool profitable=body_bytes>reference_bytes&&references[item.key()]>
          (body_bytes+item.key().size()+4)/(body_bytes-reference_bytes);
        if(!profitable&&body_bytes<smallest){remove=item.key();smallest=body_bytes;}
      }
      if(remove.empty())break;
      const auto body=schema.at("$defs").at(remove);schema["$defs"].erase(remove);shared.erase(remove);
      const Json reference={{"$ref","#/$defs/"+remove}};
      std::function<void(Json&)> inline_reference=[&](Json& value) {
        if(value==reference){value=body;return;}
        if(value.is_array())for(auto& child:value)inline_reference(child);
        else if(value.is_object())for(auto& item:value.items())if(item.key()!="const"&&item.key()!="enum"&&item.key()!="default")inline_reference(item.value());
      };
      inline_reference(schema);prune_definitions(schema);
    }
    // Every standalone definition name is local. Apply one bijection after
    // sharing, preserving the public model/operation roots and all constraints.
    if(schema.contains("$defs")) {
      std::map<std::string,std::string> aliases;std::size_t ordinal=0;
      for(const auto& item:schema.at("$defs").items())if(item.key()!="model"&&item.key()!="operation") {
        auto name=alias(ordinal++);while(name=="model"||name=="operation")name+="_";
        aliases.emplace(item.key(),std::move(name));
      }
      std::function<void(Json&)> rewrite=[&](Json& value) {
        if(value.is_array())for(auto& child:value)rewrite(child);
        else if(value.is_object()) {
          if(value.contains("$ref")&&value.at("$ref").is_string()) {
            const auto& ref=value.at("$ref").get_ref<const std::string&>();
            if(ref.starts_with("#/$defs/"))if(const auto found=aliases.find(ref.substr(8));found!=aliases.end())value["$ref"]="#/$defs/"+found->second;
          }
          for(auto& item:value.items()) {
            const auto& key=item.key();
            if((key=="$defs"||key=="properties"||key=="patternProperties"||key=="dependentSchemas")&&item.value().is_object())
              for(auto& child:item.value().items())rewrite(child.value());
            else if(key!="const"&&key!="enum"&&key!="default")rewrite(item.value());
          }
        }
      };
      rewrite(schema);Json renamed=Json::object();
      for(auto& item:schema["$defs"].items())renamed[aliases.contains(item.key())?aliases.at(item.key()):item.key()]=std::move(item.value());
      schema["$defs"]=std::move(renamed);
      // Final aliases make the cost of every private definition comparable.
      // Inline only pure, whole-definition references when their stored body is
      // cheaper than the entry plus references. Preserve named public roots,
      // scoped schemas, reference siblings, pointer targets and recursive graphs.
      bool scoped=false;
      std::function<void(const Json&,const std::function<void(const Json&)>&)> visit=
        [&](const Json& value,const std::function<void(const Json&)>& inspect) {
          if(value.is_array())for(const auto& child:value)visit(child,inspect);
          else if(value.is_object()) {
            inspect(value);
            for(const auto& item:value.items()) {
              const auto& key=item.key();
              if((key=="$defs"||key=="properties"||key=="patternProperties"||key=="dependentSchemas")&&item.value().is_object())
                for(const auto& child:item.value().items())visit(child.value(),inspect);
              else if(key!="const"&&key!="enum"&&key!="default")visit(item.value(),inspect);
            }
          }
        };
      visit(schema,[&](const Json& value) {
        for(const auto* key:{"$id","$anchor","$dynamicAnchor","$dynamicRef","$recursiveAnchor","$recursiveRef"})if(value.contains(key))scoped=true;
      });
      while(!scoped&&schema.contains("$defs")) {
        std::map<std::string,std::size_t> references,pure;
        std::map<std::string,std::set<std::string>> edges;
        const auto collect=[&](const Json& value,std::map<std::string,std::size_t>& counts,std::map<std::string,std::size_t>* exact) {
          visit(value,[&](const Json& node) {
            if(!node.contains("$ref")||!node.at("$ref").is_string())return;
            const auto& ref=node.at("$ref").get_ref<const std::string&>();
            if(!ref.starts_with("#/$defs/"))return;
            const auto name=ref.substr(8,ref.find('/',8)-8);++counts[name];
            if(exact&&node.size()==1&&ref=="#/$defs/"+name)++(*exact)[name];
          });
        };
        collect(schema,references,&pure);
        for(const auto& item:schema.at("$defs").items()) {
          std::map<std::string,std::size_t> children;collect(item.value(),children,nullptr);
          for(const auto& [name,count]:children)edges[item.key()].insert(name);
        }
        std::string remove;std::size_t smallest=std::string::npos;
        for(const auto& item:schema.at("$defs").items()) {
          const auto& name=item.key();
          if(name=="model"||name=="operation"||!references[name]||references[name]!=pure[name])continue;
          std::set<std::string> seen;std::vector<std::string> pending={name};bool recursive=false;
          while(!pending.empty()&&!recursive) {
            auto current=std::move(pending.back());pending.pop_back();
            for(const auto& next:edges[current]) {
              if(next==name){recursive=true;break;}
              if(seen.insert(next).second)pending.push_back(next);
            }
          }
          if(recursive)continue;
          const auto body_bytes=item.value().dump().size();
          const auto reference_bytes=Json{{"$ref","#/$defs/"+name}}.dump().size();
          const bool profitable=body_bytes>reference_bytes&&references[name]>
            (body_bytes+name.size()+4)/(body_bytes-reference_bytes);
          if(!profitable&&body_bytes<smallest){remove=name;smallest=body_bytes;}
        }
        if(remove.empty())break;
        const auto body=schema.at("$defs").at(remove);schema["$defs"].erase(remove);
        const Json reference={{"$ref","#/$defs/"+remove}};
        std::function<void(Json&)> inline_reference=[&](Json& value) {
          if(value==reference){value=body;return;}
          if(value.is_array())for(auto& child:value)inline_reference(child);
          else if(value.is_object())for(auto& item:value.items()) {
            const auto& key=item.key();
            if((key=="$defs"||key=="properties"||key=="patternProperties"||key=="dependentSchemas")&&item.value().is_object())
              for(auto& child:item.value().items())inline_reference(child.value());
            else if(key!="const"&&key!="enum"&&key!="default")inline_reference(item.value());
          }
        };
        inline_reference(schema);prune_definitions(schema);
      }
    }
  }
  return tools;
}

void validate_tool_arguments(const std::string& tool,const Json& args) {
  if(tool=="cad_import_sketch") {
    fields(args,{"document_id","expected_revision","format","path","feature_id","workplane"},{"request_id","expected_sha256","text","height","spacing","scale","face_index"});identifier(text_field(args,"document_id"));revision_number(args.at("expected_revision"));identifier(text_field(args,"feature_id"));text_field(args,"path");if(args.contains("request_id"))identifier(text_field(args,"request_id"));const auto format=text_field(args,"format");if(format!="text"&&format!="svg"&&format!="dxf")throw Error("invalid_argument","Import format must be text, svg or dxf");
    if(format=="text"){if(!args.contains("text")||!args.contains("height"))throw Error("invalid_argument","Text import requires text and height");text_field(args,"text");if(args.contains("scale"))throw Error("invalid_argument","Text import does not accept scale");if(args.contains("face_index")&&(!args.at("face_index").is_number_integer()||args.at("face_index")<0||args.at("face_index")>31))throw Error("invalid_argument","Font face_index requires 0..31");}else for(auto key:{"text","height","spacing","face_index"})if(args.contains(key))throw Error("invalid_argument","SVG/DXF import does not accept text/font options");
    if(args.contains("expected_sha256")){const auto hash=text_field(args,"expected_sha256");if(hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos)throw Error("invalid_argument","Invalid expected source SHA-256");}return;
  }
  if(tool=="cad_capture_sketch") {
    fields(args,{"format","path","feature_id","workplane"},{"expected_sha256","text","height","spacing","scale","face_index"});const auto format=text_field(args,"format");text_field(args,"path");identifier(text_field(args,"feature_id"));
    if(format!="text"&&format!="svg"&&format!="dxf")throw Error("invalid_argument","Capture format must be text, svg or dxf");
    if(format=="text"){if(!args.contains("text")||!args.contains("height"))throw Error("invalid_argument","Text capture requires text and height");text_field(args,"text");const auto h=number(args.at("height"));if(h<1e-5||h>1e5)throw Error("invalid_argument","Text height is outside supported bounds");if(args.contains("scale"))throw Error("invalid_argument","Text capture does not accept scale");if(args.contains("spacing"))number(args.at("spacing"));if(args.contains("face_index")&&(!args.at("face_index").is_number_integer()||args.at("face_index")<0||args.at("face_index")>31))throw Error("invalid_argument","Font face_index requires 0..31");}
    else {for(auto key:{"text","height","spacing","face_index"})if(args.contains(key))throw Error("invalid_argument","SVG/DXF capture does not accept text/font options");if(args.contains("scale")&&number(args.at("scale"))<1e-6)throw Error("invalid_argument","Authoring scale must be positive");}
    if(args.contains("expected_sha256")){const auto hash=text_field(args,"expected_sha256");if(hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos)throw Error("invalid_argument","Invalid expected source SHA-256");}
    return;
  }
  if(tool=="cad_inspect_step") {
    fields(args,{"path"},{"expected_sha256"});text_field(args,"path");
    if(args.contains("expected_sha256")){const auto hash=text_field(args,"expected_sha256");if(hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos)throw Error("invalid_argument","Invalid expected STEP SHA-256");}
    return;
  }
  if(tool=="cad_artifact") {
    if(text_field(args,"action")=="review")validate_artifact_review_arguments(args);
    else if(text_field(args,"action")=="verify") {
      fields(args,{"action","review_path","expected_sha256"});
      const auto path=path_from_utf8(text_field(args,"review_path"));const auto hash=text_field(args,"expected_sha256");
      if(!path.is_absolute()||path.filename()!="review.json"||hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos)throw Error("invalid_argument","Verification requires absolute review.json and lowercase SHA-256");
    }else throw Error("invalid_argument","Artifact action must be review or verify");
    return;
  }
  static const std::set<std::string> known={"cad_create","cad_read","cad_apply","cad_restore","cad_import","cad_query","cad_measure","cad_export","cad_manufacture","cad_fabrication_review","cad_gcode_review","cad_printer_handoff","cad_slice","cad_robot_export","cad_bom","cad_drawing","cad_view","cad_preview","cad_resolve_selection","cad_compare"};
  if(!known.contains(tool)) throw Error("unknown_tool","Unknown tool: "+tool);
  if(tool=="cad_create") fields(args,{"document_id","model"},{"request_id"});
  else if(tool=="cad_read") fields(args,{"document_id"},{"revision"});
  else if(tool=="cad_apply") fields(args,{"document_id","expected_revision","operations"},{"request_id"});
  else if(tool=="cad_restore") fields(args,{"document_id","expected_revision","source_revision"},{"request_id"});
  else if(tool=="cad_import") {
    fields(args,{"document_id","path"},{"request_id","expected_sha256","purchase","solid_indices"});
    if(args.contains("solid_indices")){if(args.contains("purchase"))throw Error("invalid_argument","Subset imports cannot claim an unchanged purchased artifact");validate_step_solid_indices(args.at("solid_indices"));if(!args.contains("expected_sha256"))throw Error("invalid_argument","STEP subset import requires expected_sha256 from inspection");}
    if(args.contains("expected_sha256")) {
      const auto hash=text_field(args,"expected_sha256");
      if(hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos)throw Error("invalid_argument","Invalid expected STEP SHA-256");
    }
    if(args.contains("purchase"))try{validate_purchase(args.at("purchase"));}
      catch(const Error& error){throw Error("invalid_argument",error.what(),error.details);}
  }
  else if(tool=="cad_export") {
    fields(args,{"document_id","revision","format"},{"feature_id","layout"});const auto format=text_field(args,"format");
    if(format!="step"&&format!="stl"&&format!="3mf")throw Error("invalid_argument","Export format must be step, stl or 3mf");
    if(args.contains("layout")){if(format!="3mf")throw Error("invalid_argument","Print layout requires 3mf format");validate_print_layout(args.at("layout"));}
  }
  else if(tool=="cad_manufacture") {fields(args,{"document_id","revision"},{"feature_id","options"});validate_manufacturing_options(args.value("options",Json::object()));}
  else if(tool=="cad_fabrication_review") {fields(args,{"document_id","revision","options"},{"feature_id"});validate_fabrication_options(args.at("options"));}
  else if(tool=="cad_gcode_review") {
    fields(args,{"document_id","revision","path","expected_sha256","options"},{"feature_id"});validate_gcode_options(args.at("options"));
    const auto hash=text_field(args,"expected_sha256");
    if(hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos)
      throw Error("invalid_argument","Expected G-code SHA-256 must be 64 lowercase hexadecimal digits");
  }
  else if(tool=="cad_printer_handoff")validate_printer_arguments(args);
  else if(tool=="cad_slice")validate_slice_arguments(args);
  else if(tool=="cad_robot_export") {fields(args,{"document_id","revision","robot"},{"feature_id"});validate_robot_options(args.at("robot"));}
  else if(tool=="cad_bom") fields(args,{"document_id","revision"},{"feature_id"});
  else if(tool=="cad_drawing") fields(args,{"document_id","revision"},{"drawing"});
  else if(tool=="cad_query") fields(args,{"document_id","revision"},{"kind","feature_id"});
  else if(tool=="cad_measure") {fields(args,{"document_id","revision","evaluation_id","feature_id","query"});identifier(text_field(args,"evaluation_id"));validate_measurement_query(args.at("query"));}
  else if(tool=="cad_view") fields(args,{"document_id","revision"},{"feature_id"});
  else if(tool=="cad_preview") fields(args,{"document_id","expected_revision","operations"},{"feature_id","kind"});
  else if(tool=="cad_compare") fields(args,{"document_id","from_revision","to_revision"});
  else fields(args,{"document_id","revision","evaluation_id","feature_id","kind","entity_id"});
  identifier(text_field(args,"document_id"));
  // Only a new document's name becomes a directory; existing documents keep working.
  if(tool=="cad_create"||tool=="cad_import") portable_identifier(text_field(args,"document_id"));
  if(args.contains("feature_id")) model_identifier(text_field(args,"feature_id"));
  if(args.contains("request_id")) identifier(text_field(args,"request_id"));
  for(const auto* key:{"revision","expected_revision","source_revision","from_revision","to_revision"})
    if(args.contains(key)) revision_number(args.at(key));
}

Error service_error(std::exception_ptr error) {
  try { std::rethrow_exception(error); }
  catch (const Error& e) { return e; }
  // Unvalidated JSON shapes surface as nlohmann access/type errors.
  catch (const Json::exception& e) { return Error("invalid_argument", std::string("Malformed JSON value: ") + e.what()); }
  catch (const fs::filesystem_error& e) {
    Json details = Json::object();
    if (!e.path1().empty()) details["path"] = path_to_utf8(e.path1());
    return Error("storage_error", e.what(), details);
  }
  catch (const std::exception& e) { return Error("internal_error", e.what()); }
  catch (...) { return Error("internal_error", "Unknown failure"); }
}

Json Service::call(const std::string& tool,const Json& args) {
  try { return execute(tool,args); }
  catch (const Error&) { throw; }
  catch (...) { throw service_error(std::current_exception()); }
}

Json Service::execute(const std::string& tool,const Json& args) {
  if(tool=="cad_artifact_show"||tool=="cad_open"||tool=="cad_show"||tool=="cad_list"||tool=="cad_context"||tool=="cad_viewer")
    return live_call(*this,store_,tool,args);
  if(tool=="cad_job") return dispatch_job(store_.root(),args);
  validate_tool_arguments(tool,args);
  if(tool=="cad_artifact") {
    if(args.at("action")=="review")return review_external_artifact(store_.root(),args,AGENTCAD_CACHE_BUILD);
    return verify_external_artifact(path_from_utf8(text_field(args,"review_path")).parent_path(),text_field(args,"expected_sha256"));
  }
  if(tool=="cad_capture_sketch") {
    auto result=capture_sketch_source(args);const auto id=text_field(args,"feature_id");const auto base=id=="capture_base"?"capture_base_other":"capture_base";
    const Json model={{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",Json::array({{{"id",base},{"type","box"},{"size",{1,1,1}}},result.at("feature")})},{"output",base}};
    const auto evaluated=evaluate_model(store_.root(),model,{{"kind","capture_sketch"},{"feature_id",id}});result.update(evaluated);return result;
  }
  if(tool=="cad_inspect_step") {
    const auto content=read_text(path_from_utf8(text_field(args,"path")),unlimited_bytes),digest=sha256(content);
    if(invalid_utf8_offset(content))throw Error("invalid_argument","STEP content must be UTF-8");
    if(args.contains("expected_sha256")&&args.at("expected_sha256")!=digest)throw Error("artifact_mismatch","STEP bytes do not match expected_sha256",{{"actual_sha256",digest}});
    const Json model={{"schema_version",1},{"units","mm"},{"parameters",Json::object()},
      {"features",Json::array({{{"id","source"},{"type","import_step"},{"content",content},{"sha256",digest}}})},{"output","source"}};
    auto result=evaluate_model(store_.root(),model,{{"kind","inspect_step"}});
    result["source_sha256"]=digest;result["kernel_version"]=kernel_version();result["index_lifetime"]="source_sha256_and_kernel";return result;
  }
  const auto id=text_field(args,"document_id");
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
  if(tool=="cad_create"||tool=="cad_apply"||tool=="cad_restore"||tool=="cad_import"||tool=="cad_import_sketch") {
    const bool create=tool=="cad_create"||tool=="cad_import";
    const auto request_id=args.contains("request_id")?text_field(args,"request_id"):std::string();
    if(args.contains("request_id")) identifier(request_id);
    const auto fingerprint=request_id.empty()?std::string():request_fingerprint(tool,args);
    const auto expected=create?0:revision_number(args.at("expected_revision"));
    Json model;
    auto precondition=[&]() -> std::optional<Json> {
      if(!request_id.empty()) if(auto replay=store_.request_replay(id,request_id,fingerprint)){if(tool=="cad_import_sketch")replay->erase("model");return replay;}
      if(create) {
        try {store_.read(id);throw Error("already_exists","Document already exists: "+id);}
        catch(const Error& e) {if(e.code!="not_found")throw;}
      } else {
        const auto current=store_.read(id);
        if(current.at("revision")!=expected) throw Error("revision_conflict","Document changed; read its current revision",{{"expected_revision",expected},{"current_revision",current.at("revision")}});
      }
      return std::nullopt;
    };
    // Mutation admission and publication wait (boundedly) for transient holders
    // such as viewer sync; expected_revision is rechecked once the lock is held.
    { DocumentLock lock(store_.root(),id,LockWait::publication);if(auto replay=precondition())return *replay;
      if(tool=="cad_create") model=args.at("model");
      else if(tool=="cad_restore") model=store_.read(id,revision_number(args.at("source_revision"))).at("model");
      else if(tool=="cad_apply") model=apply_operations(store_.read(id).at("model"),args.at("operations"),
        [&](const std::string& source,std::uint64_t revision){return store_.read(source,revision);});
      else if(tool=="cad_import_sketch") {
        const auto current=store_.read(id);auto capture=args;for(auto key:{"document_id","expected_revision","request_id"})capture.erase(key);const auto captured=capture_sketch_source(capture,current.at("model").at("parameters"));model=apply_operations(current.at("model"),Json::array({{{"op","add_feature"},{"feature",captured.at("feature")}}}));
      }
      else {
        const auto content=read_text(path_from_utf8(text_field(args,"path")),unlimited_bytes);
        // Documents embed STEP as a JSON (UTF-8) string; transcoding would change
        // the bytes and SHA-256 that define the imported feature.
        if(const auto offset=invalid_utf8_offset(content))
          throw Error("invalid_argument","STEP file is not valid UTF-8 at byte "+std::to_string(*offset)+
            "; cad_import embeds the file unchanged as UTF-8 text (ISO 10303-21 encodes other text with \\X2\\ escapes)",{{"byte_offset",*offset}});
        const auto digest=sha256(content);
        if(args.contains("expected_sha256")&&args.at("expected_sha256")!=digest)
          throw Error("artifact_mismatch","STEP bytes do not match expected_sha256",{{"expected_sha256",args.at("expected_sha256")},{"actual_sha256",digest}});
        Json feature={{"id","imported"},{"type","import_step"},{"content",content},{"sha256",digest}};
        if(args.contains("solid_indices"))feature["solid_indices"]=args.at("solid_indices");
        if(args.contains("purchase")) {
          auto purchase=args.at("purchase");
          if(purchase.contains("artifact_sha256")&&purchase.at("artifact_sha256")!=digest)
            throw Error("artifact_mismatch","Purchasing artifact SHA-256 differs from the STEP bytes",{{"expected_sha256",purchase.at("artifact_sha256")},{"actual_sha256",digest}});
          purchase["artifact_sha256"]=digest;feature["purchase"]=std::move(purchase);
        }
        model={{"schema_version",1},{"units","mm"},{"parameters",Json::object()},
          {"features",Json::array({std::move(feature)})},{"output","imported"}};
      }
    }
    validate_model(model);
    const auto evaluated=evaluate_model(store_.root(),model,{{"kind","summary"}});
    DocumentLock lock(store_.root(),id,LockWait::publication);if(auto replay=precondition())return *replay;
    check_job_cancelled();
    Json receipt=Json::object();
    Json result_fields={{"summary",evaluated.at("summary")}};
    if(tool=="cad_import_sketch"){result_fields["feature_id"]=args.at("feature_id");for(const auto& feature:model.at("features"))if(feature.at("id")==args.at("feature_id")){const auto& profile=feature.at("profile");result_fields["source_sha256"]=profile.at("type")=="text"?profile.at("font").at("sha256"):profile.at("sha256");}}
    if(!request_id.empty()) receipt={{"request_id",request_id},{"fingerprint",fingerprint},{"result",result_fields}};
    auto record=store_.commit(id,model,create,receipt);record.update(result_fields);if(tool=="cad_import_sketch")record.erase("model");return record;
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
    const auto candidate=apply_operations(record.at("model"),args.at("operations"),
      [&](const std::string& source,std::uint64_t revision){return store_.read(source,revision);});
    const auto kind=args.value("kind",std::string("view"));
    if(kind!="view" && kind!="mesh") throw Error("invalid_argument","Preview kind must be view or mesh");
    Json request={{"kind","view"}};if(args.contains("feature_id"))request["feature_id"]=args.at("feature_id");
    return save_evaluation(store_.root(),record,evaluate_model(store_.root(),candidate,request),true,kind=="view");
  }
  const auto revision=revision_number(args.at("revision"));const auto record=store_.read(id,revision);
  if(tool=="cad_measure") {
    const auto eid=text_field(args,"evaluation_id"),feature=text_field(args,"feature_id"),hash=sha256(record.at("model").dump());
    if(store_.read(id).at("revision")!=revision)throw Error("stale_selection","Measurement requires the current committed revision");
    const auto parent=store_.root()/"evaluations",path=parent/(eid+".json");
    if(fs::is_symlink(fs::symlink_status(parent))||fs::is_symlink(fs::symlink_status(path)))throw Error("storage_error","Measurement evaluation cannot be a symlink");
    Json evaluation;
    try{evaluation=parse_json(read_text(path,64*1024*1024),64*1024*1024);}
    catch(const Error& error){if(error.code=="not_found")throw Error("stale_selection","Measurement evaluation is missing; request a fresh view");throw;}
    if(evaluation.at("draft")==true)throw Error("draft_selection","Save or reset the displayed pose before measuring committed geometry");
    for(const auto* key:{"document_id","revision","evaluation_id","feature_id"})if(evaluation.at(key)!=args.at(key))throw Error("stale_selection","Measurement does not belong to this evaluation");
    if(evaluation.value("_native_build",std::string{})!=AGENTCAD_CACHE_BUILD||evaluation.value("_model_sha256",std::string{})!=hash)
      throw Error("stale_selection","Measurement evaluation source/build differs; request a fresh view");
    auto report=evaluate_model(store_.root(),record.at("model"),{{"kind","measure"},{"feature_id",feature},{"options",args.at("query")},{"topology",evaluation.at("topology")}}).at("measurement");
    DocumentLock lock(store_.root(),id,LockWait::publication);check_job_cancelled();
    if(store_.read(id).at("revision")!=revision)throw Error("stale_selection","Source changed while measuring; request its current evaluation");
    return {{"document_id",id},{"revision",revision},{"evaluation_id",eid},{"feature_id",feature},{"kernel_version",kernel_version()},
      {"model_sha256",hash},{"native_build",AGENTCAD_CACHE_BUILD},{"report",report}};
  }
  if(tool=="cad_printer_handoff") {
    return args.at("action")=="plan"?plan_printer_handoff(store_.root(),record,args,AGENTCAD_CACHE_BUILD):
      verify_printer_handoff(store_.root(),record,args,AGENTCAD_CACHE_BUILD);
  }
  if(tool=="cad_slice") {
    if(args.at("action")=="plan")return plan_slice(store_.root(),record,args.at("options"),args.value("feature_id",text_field(record.at("model"),"output")),AGENTCAD_CACHE_BUILD);
    return run_slice(store_.root(),record,args,AGENTCAD_CACHE_BUILD);
  }
  if(tool=="cad_gcode_review") {
    const auto feature=args.value("feature_id",text_field(record.at("model"),"output"));
    bool found=false;for(const auto& item:record.at("model").at("features"))if(item.at("id")==feature)found=true;
    if(!found)throw Error("invalid_argument","G-code association names a missing source feature",{{"feature_id",feature}});
    const auto input=path_from_utf8(text_field(args,"path"));
    auto suffix=path_to_utf8(input.extension());for(auto& c:suffix)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if(!input.is_absolute()||suffix!=".gcode"||!fs::is_regular_file(input))
      throw Error("invalid_argument","G-code review requires an absolute existing plain .gcode file");
    const auto original=read_text(input,gcode_bytes_limit);const auto digest=sha256(original);
    if(digest!=text_field(args,"expected_sha256"))throw Error("artifact_mismatch","G-code bytes do not match expected SHA-256");
    const Json source={{"document_id",id},{"revision",revision},{"kernel_version",kernel_version()},{"feature_id",feature},
      {"model_sha256",sha256(record.at("model").dump())},{"native_build",AGENTCAD_CACHE_BUILD}};
    const auto exports=store_.root()/"exports";directory(exports);const auto stage=temporary_directory(exports);
    const auto destination=exports/(id+"-r"+std::to_string(revision)+"-gcode-"+evaluation_id());
    try {
      check_job_cancelled();atomic_text(stage/"original.gcode",original,gcode_bytes_limit);
      const auto report=evaluate_model(store_.root(),record.at("model"),{{"kind","gcode_review"},{"path",path_to_utf8(stage/"original.gcode")},{"options",args.at("options")}}).at("gcode_report");
      const auto content=Json{{"schema_version",1},{"source",source},{"source_association","caller_declared_not_geometry_verified"},
        {"gcode",{{"path","original.gcode"},{"sha256",digest},{"bytes",original.size()}}},{"options",args.at("options")},{"report",report}}.dump(2)+"\n";
      atomic_text(stage/"review.json",content,gcode_bytes_limit);atomic_payload_json(stage/"source.json",record);
      Json ledger=Json::array();for(const auto* name:{"original.gcode","review.json","source.json"}) {
        const auto raw=read_text(stage/name,gcode_bytes_limit);ledger.push_back({{"path",name},{"sha256",sha256(raw)},{"bytes",raw.size()}});
      }
      atomic_text(stage/"manifest.json",Json{{"schema_version",1},{"source",source},{"artifacts",ledger},
        {"physical_print_started",false},{"process_approval","not_evaluated"}}.dump(2)+"\n");
      auto result=source;result["gcode_sha256"]=digest;result["gcode_bytes"]=original.size();result["report"]=report;
      result["path"]=path_to_utf8(destination/"review.json");result["artifact_path"]=path_to_utf8(destination/"original.gcode");
      result["source_path"]=path_to_utf8(destination/"source.json");result["report_sha256"]=sha256(content);result["report_bytes"]=content.size();
      DocumentLock lock(store_.root(),id,LockWait::publication);check_job_cancelled();fs::rename(stage,destination);return result;
    }catch(...){std::error_code ignored;fs::remove_all(stage,ignored);throw;}
  }
  if(tool=="cad_fabrication_review") {
    const auto feature=args.value("feature_id",text_field(record.at("model"),"output"));
    const Json source={{"document_id",id},{"revision",revision},{"kernel_version",kernel_version()},{"feature_id",feature},
      {"units","mm"},{"model_sha256",sha256(record.at("model").dump())},{"native_build",AGENTCAD_CACHE_BUILD}};
    const auto report=evaluate_model(store_.root(),record.at("model"),{{"kind","fabrication"},{"feature_id",feature},{"options",args.at("options")}}).at("fabrication");
    const auto exports=store_.root()/"exports";directory(exports);const auto stage=temporary_file(exports);
    const auto destination=exports/(id+"-r"+std::to_string(revision)+"-"+evaluation_id()+".review.json");
    const auto content=Json{{"schema_version",1},{"source",source},{"options",args.at("options")},{"report",report}}.dump(2)+"\n";
    try {
      atomic_text(stage,content,64*1024*1024);auto result=source;result["report"]=report;result["path"]=path_to_utf8(destination);
      result["sha256"]=sha256(content);result["bytes"]=content.size();
      DocumentLock lock(store_.root(),id,LockWait::publication);check_job_cancelled();publish_file(stage,destination);return result;
    }catch(...){std::error_code ignored;fs::remove(stage,ignored);throw;}
  }
  if(tool=="cad_manufacture") {
    const auto feature=args.value("feature_id",text_field(record.at("model"),"output"));
    const Json identity={{"document_id",id},{"revision",revision},{"kernel_version",kernel_version()},{"feature_id",feature},{"units","mm"}};
    const auto exports=store_.root()/"exports";directory(exports);
    const auto stage=temporary_directory(exports),destination=exports/(id+"-r"+std::to_string(revision)+"-manufacturing-"+evaluation_id());
    try {
      auto result=evaluate_model(store_.root(),record.at("model"),{{"kind","manufacturing"},{"feature_id",feature},
        {"options",args.value("options",Json::object())},{"identity",identity},{"path",path_to_utf8(stage)}}).at("manufacturing");
      for(const auto& item:identity.items())result[item.key()]=item.value();
      result["directory"]=path_to_utf8(destination);result["path"]=path_to_utf8(destination/"manifest.json");
      for(auto& artifact:result["artifacts"])artifact["path"]=path_to_utf8(destination/path_from_utf8(text_field(artifact,"path")));
      DocumentLock lock(store_.root(),id,LockWait::publication);check_job_cancelled();fs::rename(stage,destination);return result;
    }catch(...) {std::error_code ignored;fs::remove_all(stage,ignored);throw;}
  }
  if(tool=="cad_robot_export") {
    const auto feature=args.value("feature_id",text_field(record.at("model"),"output"));
    const auto exports=store_.root()/"exports";directory(exports);
    const auto stage=temporary_directory(exports),destination=exports/(id+"-r"+std::to_string(revision)+"-robot-"+evaluation_id());
    try {
      auto generated=evaluate_model(store_.root(),record.at("model"),{{"kind","robot"},{"feature_id",feature},{"robot",args.at("robot")},{"path",path_to_utf8(stage)}}).at("robot");
      const Json identity={{"document_id",id},{"revision",revision},{"kernel_version",kernel_version()},{"feature_id",feature}};
      generated["ledger"]["source"]=identity;generated["ledger"]["source"]["model_sha256"]=sha256(record.at("model").dump());
      write_artifact(stage/"robot.json",generated.at("ledger").dump(2)+"\n");
      std::vector<std::string> files={"robot.json"};
      for(const auto& [name,content]:generated.at("files").items()) {write_artifact(stage/path_from_utf8(name),content.get<std::string>());files.push_back(name);}
      for(const auto& item:generated.at("mesh_sources")) files.push_back(text_field(item,"path"));
      auto result=identity;result["format"]=args.at("robot").at("format");result["directory"]=path_to_utf8(destination);
      result["path"]=path_to_utf8(destination/path_from_utf8(text_field(generated,"path")));result["manifest_path"]=path_to_utf8(destination/"manifest.json");result["artifacts"]=Json::array();
      auto manifest=identity;manifest["schema_version"]=1;manifest["format"]=result.at("format");manifest["path"]=generated.at("path");manifest["artifacts"]=Json::array();
      std::uintmax_t total=0;
      for(const auto& file:files) {
        check_job_cancelled();const auto path=stage/path_from_utf8(file);const auto bytes=fs::file_size(path);total+=bytes;
        if(bytes==0 || bytes>64*1024*1024 || total>256*1024*1024) throw Error("limit_exceeded","Robot bundle exceeds its 64 MiB file or 256 MiB total limit");
        const auto extension=path_to_utf8(path.extension()).substr(1);
        Json artifact={{"format",extension},{"path",file},{"bytes",bytes},{"sha256",sha256(read_text(path,64*1024*1024))}};
        manifest["artifacts"].push_back(artifact);artifact["path"]=path_to_utf8(destination/path_from_utf8(file));result["artifacts"].push_back(artifact);
      }
      write_artifact(stage/"manifest.json",manifest.dump(2)+"\n");
      DocumentLock lock(store_.root(),id,LockWait::publication);check_job_cancelled();fs::rename(stage,destination);
      return result;
    } catch(const Error& error) {std::error_code ignored;fs::remove_all(stage,ignored);auto details=error.details;details["feature_id"]=feature;throw Error(error.code,error.what(),details);}
    catch(...) {std::error_code ignored;fs::remove_all(stage,ignored);throw;}
  }
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
      DocumentLock lock(store_.root(),id,LockWait::publication);check_job_cancelled();
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
      DocumentLock lock(store_.root(),id,LockWait::publication);check_job_cancelled();
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

  const auto exports=store_.root()/"exports";directory(exports);
  if(format=="3mf") {
    const auto stage=temporary_directory(exports);
    try {
      const Json identity={{"document_id",id},{"revision",revision},{"feature_id",args.value("feature_id",record.at("model").at("output"))},
        {"model_sha256",sha256(record.at("model").dump())},{"kernel_version",kernel_version()}};
      const auto report=evaluate_model(store_.root(),record.at("model"),{{"kind","export"},{"format",format},{"feature_id",identity.at("feature_id")},
        {"path",path_to_utf8(stage)},{"options",args.value("layout",Json())},{"identity",identity}}).at("print_export");
      const auto target=exports/(id+"-r"+std::to_string(revision)+"-3mf-"+sha256(report.dump()).substr(0,24));
      {DocumentLock lock(store_.root(),id,LockWait::publication);check_job_cancelled();
        if(fs::is_symlink(fs::symlink_status(target)))throw Error("storage_error","Print package cannot be a symlink");
        if(fs::exists(target)) {
          if(parse_json(read_text(target/"layout.json"))!=report)throw Error("artifact_mismatch","Existing print package differs from its source");
          for(const auto& plate:report.at("plates"))if(sha256_file(target/path_from_utf8(text_field(plate,"path")),128*1024*1024,check_job_cancelled)!=text_field(plate,"sha256"))throw Error("artifact_mismatch","Existing print plate bytes changed");
        }else fs::rename(stage,target);
      }
      Json plates=report.at("plates");for(auto& plate:plates)plate["path"]=path_to_utf8(target/path_from_utf8(text_field(plate,"path")));
      std::error_code ignored;fs::remove_all(stage,ignored);
      return {{"document_id",id},{"revision",revision},{"format",format},{"path",plates[0].at("path")},{"bytes",plates[0].at("bytes")},
        {"units","mm"},{"layout_path",path_to_utf8(target/"layout.json")},{"plates",plates}};
    }catch(...){std::error_code ignored;fs::remove_all(stage,ignored);throw;}
  }
  const auto target=exports/(id+(args.contains("feature_id")?"-"+text_field(args,"feature_id"):"")+"-r"+std::to_string(revision)+"."+format),temporary=temporary_file(exports);
  std::uintmax_t bytes=0;
  try {
    evaluate_model(store_.root(),record.at("model"),{{"kind","export"},{"format",format},{"path",path_to_utf8(temporary)},{"feature_id",args.value("feature_id",record.at("model").at("output"))}});
    std::error_code size_error;bytes=fs::file_size(temporary,size_error);
    if(size_error)throw Error("storage_error","Cannot measure export: "+size_error.message());
    DocumentLock lock(store_.root(),id,LockWait::publication);check_job_cancelled();publish_file(temporary,target);
  }catch(...){std::error_code ignored;fs::remove(temporary,ignored);throw;}
  return {{"document_id",id},{"revision",revision},{"format",format},{"path",path_to_utf8(target)},{"bytes",bytes},{"units","mm"}};
}
}
