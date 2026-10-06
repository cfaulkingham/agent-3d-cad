#include "agentcad/live.hpp"
#include "agentcad/app.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <random>

namespace agentcad {
namespace {
constexpr std::size_t max_view_bytes = 64 * 1024 * 1024;
constexpr std::size_t chunk_bytes = 128 * 1024;
constexpr std::size_t document_limit = 1000;
Json object(Json properties, Json required) {
  return {{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};
}
std::string nonce() {
  std::random_device random;
  return std::to_string(random()) + "-" + std::to_string(random()) + "-" + std::to_string(random()) + "-" + std::to_string(random());
}
std::string view_id(const Json& arguments) {
  const auto id = arguments.contains("view_id") ? text_field(arguments,"view_id") : "main";
  identifier(id); return id;
}
fs::path view_path(const fs::path& root, const std::string& id, bool create = false) {
  const auto parent = root / "views";
  if (fs::is_symlink(fs::symlink_status(parent))) throw Error("storage_error","Managed view directory cannot be a symlink");
  if (create) directory(parent);
  const auto path = parent / id;
  if (fs::is_symlink(fs::symlink_status(path))) throw Error("storage_error","Managed view directory cannot be a symlink");
  if (create) directory(path);
  return path;
}
Json empty_state(const std::string& id) {
  return {{"view_id",id},{"document_id",nullptr},{"generation",nonce()},{"context",nullptr}};
}
Json read_state(const fs::path& path) { return parse_json(read_text(path / "state.json")); }
void save_state(const fs::path& path, const Json& state) { atomic_text(path / "state.json",state.dump()); }
Json snapshot(const fs::path& root, const std::string& id) {
  const auto path = view_path(root,id);
  if (!fs::exists(path / "state.json")) throw Error("not_found","Open the requested view first");
  WorkspaceLock lock(path); return read_state(path);
}
bool same_view(const Json& a, const Json& b) {
  return a.at("generation") == b.at("generation") && a.at("document_id") == b.at("document_id");
}
Json sync_status(const Json& state, const Json& revision, const std::string& status) {
  return {{"view_id",state.at("view_id")},{"document_id",state.at("document_id")},
    {"revision",revision},{"state",status},{"changed",false}};
}
Json ready_status(const Json& state, const Json& record, const Json& arguments) {
  const auto& display = state.at("display");
  auto result = sync_status(state,display.at("revision"),"ready");
  result["evaluation_id"] = display.at("evaluation_id");
  result["summary"] = display.at("summary");
  result["feature_id"] = display.at("feature_id");
  const bool changed = arguments.value("known_evaluation_id",std::string{}) != display.at("evaluation_id").get<std::string>();
  result["changed"] = changed;
  if (changed) result["model"] = record.at("model");
  return result;
}
void write_frozen(const fs::path& path, const Json& evaluation) {
  // Byte offsets and JavaScript string offsets coincide for ASCII JSON. Unicode
  // strings are represented by JSON escapes and restored by JSON.parse.
  const auto content = evaluation.dump(-1,' ',true);
  if (content.size() > max_view_bytes) throw Error("limit_exceeded","Frozen view exceeds 64 MiB");
  const auto temporary = temporary_file(path.parent_path());
  try {
    std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
    output.write(content.data(),static_cast<std::streamsize>(content.size())); output.close();
    if (!output) throw Error("storage_error","Could not write frozen view");
    publish_file(temporary,path);
  } catch (...) { std::error_code ignored; fs::remove(temporary,ignored); throw; }
}
Json context_result(Store& store, const Json& state) {
  Json result = {{"view_id",state.at("view_id")},{"document_id",state.at("document_id")},
    {"revision",nullptr},{"evaluation_id",nullptr},{"head_revision",nullptr},{"stale",false},{"selection",nullptr}};
  if (state.at("document_id").is_null()) return result;
  try { result["head_revision"] = store.read(state.at("document_id").get<std::string>()).at("revision"); }
  catch (const Error& e) { if (e.code != "not_found") throw; }
  if (state.contains("display")) {
    result["revision"] = state.at("display").at("revision");
    result["evaluation_id"] = state.at("display").at("evaluation_id");
  }
  if (!state.at("context").is_null()) result.update(state.at("context"));
  result["stale"] = (!result.at("revision").is_null() && result.at("revision") != result.at("head_revision")) ||
    (!result.at("evaluation_id").is_null() && (!state.contains("display") ||
      result.at("evaluation_id") != state.at("display").at("evaluation_id")));
  return result;
}
void check_display(Store& store, const Json& state, const std::string& evaluation) {
  if (state.at("document_id").is_null() || !state.contains("display") ||
      state.at("display").at("evaluation_id") != evaluation)
    throw Error("stale_selection","Evaluation is no longer displayed in this view; synchronize first");
  const auto head = store.read(state.at("document_id").get<std::string>());
  if (head.at("revision") != state.at("display").at("revision"))
    throw Error("stale_selection","The document changed; synchronize the view before using its evaluation");
}
Json synchronize(Store& store, const Json& arguments) {
  const auto id = view_id(arguments);
  const auto path = view_path(store.root(),id);
  for (int attempt = 0; attempt < 3; ++attempt) {
    auto state = snapshot(store.root(),id);
    if (state.at("document_id").is_null()) {
      auto result = sync_status(state,nullptr,"empty");
      result["changed"] = arguments.contains("known_evaluation_id"); return result;
    }
    const auto document = state.at("document_id").get<std::string>();
    const auto record = store.read(document);
    const auto revision = record.at("revision");
    if (state.contains("display") && state.at("display").at("revision") == revision) {
      // Retargeting between the initial snapshot and this result cannot publish
      // an evaluation belonging to the view's former document.
      DocumentLock publication(store.root(),document);
      WorkspaceLock lock(path);
      const auto current = read_state(path);
      if (!same_view(current,state) || store.read(document).at("revision") != revision) continue;
      if (current.contains("display") && current.at("display").at("revision") == revision)
        return ready_status(current,record,arguments);
      continue;
    }
    if (state.contains("failure") && state.at("failure").at("revision") == revision) {
      auto result = sync_status(state,revision,"error"); result["error"] = state.at("failure").at("error"); return result;
    }
    {
      WorkspaceLock lock(path);
      auto current = read_state(path);
      if (!same_view(current,state)) continue;
      if (!current.contains("pending") || current.at("pending").at("revision") != revision) {
        const auto job = "live_" + sha256(current.at("generation").get<std::string>() + ":" + document + ":" + revision.dump()).substr(0,59);
        current["pending"] = {{"revision",revision},{"job_id",job}};
        current.erase("failure"); save_state(path,current);
      }
      state = std::move(current);
    }
    Json job;
    try {
      // This call only submits/inspects a process job. Neither the view lock nor
      // a document publication lock is held while entering the job dispatcher.
      job = dispatch_job(store.root(),{{"action","submit"},{"request_id",state.at("pending").at("job_id")},
        {"tool","cad_query"},{"arguments",{{"document_id",document},{"revision",revision},{"kind","mesh"}}}});
    } catch (const Error& e) {
      auto result = sync_status(state,revision,(e.code == "workspace_busy" || e.code == "queue_full") ? "loading" : "error");
      result["error"] = e.json(); return result;
    }
    const auto status = job.at("state").get<std::string>();
    if (status == "queued" || status == "running" || status == "cancelling") return sync_status(state,revision,"loading");
    if (status != "succeeded") {
      const auto error = job.value("error",Error("worker_failed","View evaluation did not complete").json());
      WorkspaceLock lock(path);
      auto current = read_state(path);
      if (!same_view(current,state) || current.value("pending",Json()) != state.at("pending")) continue;
      current["failure"] = {{"revision",revision},{"error",error}}; save_state(path,current);
      auto result = sync_status(current,revision,"error"); result["error"] = error; return result;
    }
    const auto& evaluation = job.at("result");
    if (evaluation.at("document_id") != document || evaluation.at("revision") != revision || evaluation.at("draft") != false)
      throw Error("storage_error","View job result does not match the requested committed revision");
    const auto eid = text_field(evaluation,"evaluation_id"); identifier(eid);
    const auto directory_path = path / "evaluations"; directory(directory_path);
    write_frozen(directory_path / (eid + ".json"),evaluation);
    {
      DocumentLock publication(store.root(),document);
      WorkspaceLock lock(path);
      auto current = read_state(path);
      if (!same_view(current,state) || current.value("pending",Json()) != state.at("pending") ||
          store.read(document).at("revision") != revision) continue;
      current["display"] = {{"document_id",document},{"revision",revision},{"evaluation_id",eid},
        {"feature_id",evaluation.at("feature_id")},{"summary",evaluation.at("summary")}};
      current.erase("pending"); current.erase("failure"); save_state(path,current);
      return ready_status(current,record,arguments);
    }
  }
  const auto state = snapshot(store.root(),id);
  return sync_status(state,state.at("document_id").is_null() ? Json(nullptr) : store.read(state.at("document_id").get<std::string>()).at("revision"),
    state.at("document_id").is_null() ? "empty" : "loading");
}
Json mesh_chunk(Store& store, const Json& arguments) {
  const auto id = view_id(arguments), eid = text_field(arguments,"evaluation_id"); identifier(eid);
  const auto state = snapshot(store.root(),id);
  if (state.at("document_id").is_null()) throw Error("stale_selection","View is empty");
  const auto offset_json = arguments.value("offset",Json(0));
  if (!offset_json.is_number_integer() || offset_json < 0 || offset_json > max_view_bytes)
    throw Error("invalid_argument","Mesh offset must be an integer between zero and 64 MiB");
  const auto offset = offset_json.get<std::size_t>();
  DocumentLock publication(store.root(),state.at("document_id").get<std::string>());
  const auto path = view_path(store.root(),id); WorkspaceLock lock(path);
  const auto current = read_state(path);
  if (!same_view(current,state)) throw Error("stale_selection","View changed before the mesh could be read");
  check_display(store,current,eid);
  const auto parent = path / "evaluations";
  if (fs::is_symlink(fs::symlink_status(parent))) throw Error("storage_error","Managed view evaluations cannot be symlinks");
  const auto file = parent / (eid + ".json");
  if (fs::is_symlink(fs::symlink_status(file)) || !fs::is_regular_file(file)) throw Error("storage_error","Frozen view must be a regular file");
  const auto size = fs::file_size(file);
  if (size > max_view_bytes) throw Error("limit_exceeded","Frozen view exceeds 64 MiB");
  if (offset > size) throw Error("invalid_argument","Mesh offset exceeds the frozen view size");
  const auto count = std::min<std::uintmax_t>(chunk_bytes,size-offset);
  std::string data(static_cast<std::size_t>(count),'\0');
  std::ifstream input(file,std::ios::binary); input.seekg(static_cast<std::streamoff>(offset));
  input.read(data.data(),static_cast<std::streamsize>(count));
  if (!input) throw Error("storage_error","Could not read the frozen view chunk");
  const auto next = offset+count;
  return {{"data",data},{"offset",offset},{"next_offset",next == size ? Json(nullptr) : Json(next)},{"total_bytes",size}};
}
Json publish_context(Service& service, Store& store, const Json& arguments) {
  const auto id = view_id(arguments), eid = text_field(arguments,"evaluation_id"); identifier(eid);
  auto state = snapshot(store.root(),id); check_display(store,state,eid);
  Json context = {{"revision",state.at("display").at("revision")},{"evaluation_id",eid},{"selection",arguments.at("selection")}};
  if (!state.at("context").is_null() && state.at("context").at("evaluation_id") == eid) {
    for (const auto* key : {"camera","prompt"}) if (state.at("context").contains(key)) context[key] = state.at("context").at(key);
  }
  if (!arguments.at("selection").is_null()) {
    const auto& pick = arguments.at("selection");
    fields(pick,{"document_id","revision","evaluation_id","feature_id","kind","entity_id"});
    for (const auto* key : {"document_id","revision","evaluation_id","feature_id"})
      if (pick.at(key) != state.at("display").at(key)) throw Error("stale_selection","Pick does not belong to this view's displayed evaluation");
    context["resolved_selection"] = service.call("cad_resolve_selection",pick);
  }
  if (arguments.contains("camera")) {
    const auto& camera = arguments.at("camera"); fields(camera,{"yaw","pitch","zoom","pan"});
    number(camera.at("yaw")); number(camera.at("pitch"));
    const auto zoom = number(camera.at("zoom"));
    if (zoom < 0.01 || zoom > 1000) throw Error("invalid_argument","Camera zoom must be between 0.01 and 1000");
    if (!camera.at("pan").is_array() || camera.at("pan").size() != 2) throw Error("invalid_argument","Camera pan needs two finite values");
    for (const auto& value : camera.at("pan")) number(value);
    context["camera"] = camera;
  }
  if (arguments.contains("prompt")) {
    const auto prompt = text_field(arguments,"prompt");
    if (prompt.size() > 8192) throw Error("limit_exceeded","Viewer prompt exceeds 8192 UTF-8 bytes");
    context["prompt"] = prompt;
  }
  context["updated_at_unix_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  {
    DocumentLock publication(store.root(),state.at("document_id").get<std::string>());
    const auto path = view_path(store.root(),id); WorkspaceLock lock(path);
    auto current = read_state(path);
    if (!same_view(current,state)) throw Error("stale_selection","View changed before context could be saved");
    check_display(store,current,eid);
    current["context"] = context; save_state(path,current); state = std::move(current);
  }
  return context_result(store,state);
}
}

Json live_call(Service& service, Store& store, const std::string& tool, const Json& arguments) {
  if (tool == "cad_list") {
    fields(arguments,{}); Json documents = Json::array(); bool truncated = false; std::size_t inspected = 0;
    const auto parent = store.root() / "documents";
    if (fs::is_symlink(fs::symlink_status(parent))) throw Error("storage_error","Managed document directory cannot be a symlink");
    for (const auto& entry : fs::directory_iterator(parent)) {
      if (++inspected > 10000 || documents.size() == document_limit) { truncated=true; break; }
      if (entry.is_symlink()) throw Error("storage_error","Managed document directories cannot be symlinks");
      if (!entry.is_directory() || !fs::exists(entry.path()/"HEAD.json")) continue;
      const auto id = path_to_utf8(entry.path().filename()); identifier(id);
      const auto head = parse_json(read_text(entry.path()/"HEAD.json")); fields(head,{"revision"});
      documents.push_back({{"document_id",id},{"revision",revision_number(head.at("revision"))}});
    }
    std::sort(documents.begin(),documents.end(),[](const Json& a,const Json& b){return a.at("document_id")<b.at("document_id");});
    return {{"documents",documents},{"truncated",truncated}};
  }
  const auto id = view_id(arguments);
  if (tool == "cad_open" || tool == "cad_show") {
    if (tool == "cad_open") fields(arguments,{}, {"view_id","document_id"});
    else fields(arguments,{"document_id"},{"view_id"});
    if (arguments.contains("document_id")) store.read(text_field(arguments,"document_id"));
    const auto path = view_path(store.root(),id,true); WorkspaceLock lock(path);
    auto state = fs::exists(path/"state.json") ? read_state(path) : empty_state(id);
    if (arguments.contains("document_id") && (state.at("document_id") != arguments.at("document_id") ||
        (tool == "cad_show" && state.contains("failure")))) {
      state = empty_state(id); state["document_id"] = arguments.at("document_id");
    }
    save_state(path,state);
    return {{"view_id",id},{"document_id",state.at("document_id")},{"resource_uri",viewer_app_uri}};
  }
  if (tool == "cad_context") {
    fields(arguments,{}, {"view_id"});
    const auto path = view_path(store.root(),id);
    return context_result(store,fs::exists(path/"state.json") ? snapshot(store.root(),id) : empty_state(id));
  }
  if (tool != "cad_viewer") throw Error("unknown_tool","Unknown live-view tool");
  const auto action = text_field(arguments,"action");
  if (action == "sync") {
    fields(arguments,{"action","view_id"},{"known_evaluation_id"});
    if (arguments.contains("known_evaluation_id")) identifier(text_field(arguments,"known_evaluation_id"));
    return synchronize(store,arguments);
  }
  if (action == "mesh") {
    fields(arguments,{"action","view_id","evaluation_id"},{"offset"}); return mesh_chunk(store,arguments);
  }
  if (action == "context") {
    fields(arguments,{"action","view_id","evaluation_id","selection"},{"camera","prompt"});
    return publish_context(service,store,arguments);
  }
  throw Error("invalid_argument","Viewer action must be sync, mesh or context");
}

Json live_tool_definitions() {
  const auto definitions = model_definitions();
  const Json id = {{"type","string"},{"pattern","^[A-Za-z][A-Za-z0-9_-]{0,63}$"}};
  const Json revision = {{"type","integer"},{"minimum",1},{"maximum",9007199254740991ULL}};
  const auto nullable = [](Json schema){return Json{{"anyOf",Json::array({schema,Json{{"type","null"}}})}};};
  const Json numeric = {{"type","number"},{"minimum",-1e6},{"maximum",1e6}};
  const Json pick = object({{"document_id",id},{"revision",revision},{"evaluation_id",id},{"feature_id",id},
    {"kind",{{"enum",{"face","edge"}}}},{"entity_id",id}}, {"document_id","revision","evaluation_id","feature_id","kind","entity_id"});
  const Json camera = object({{"yaw",numeric},{"pitch",numeric},{"zoom",{{"type","number"},{"minimum",0.01},{"maximum",1000}}},
    {"pan",{{"type","array"},{"items",numeric},{"minItems",2},{"maxItems",2}}}}, {"yaw","pitch","zoom","pan"});
  const Json prompt = {{"type","string"},{"maxLength",8192}};
  const Json error = object({{"code",{{"type","string"}}},{"message",{{"type","string"}}},{"details",{{"type","object"}}}}, {"code","message","details"});
  const Json context = object({{"view_id",id},{"document_id",nullable(id)},{"revision",nullable(revision)},{"evaluation_id",nullable(id)},
    {"head_revision",nullable(revision)},{"stale",{{"type","boolean"}}},{"selection",nullable(pick)},
    {"resolved_selection",object({{"reference",pick},{"geometry",{{"oneOf",Json::array({Json{{"$ref","#/$defs/face"}},Json{{"$ref","#/$defs/edge"}}})}}},
      {"selector",{{"$ref","#/$defs/selector"}}}}, {"reference","geometry"})},
    {"camera",camera},{"prompt",prompt},{"updated_at_unix_ms",{{"type","integer"}}}},
    {"view_id","document_id","revision","evaluation_id","head_revision","stale","selection"});
  const Json identity = object({{"view_id",id},{"document_id",nullable(id)},{"resource_uri",{{"const",viewer_app_uri}}}}, {"view_id","document_id","resource_uri"});
  const Json point = {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3}};
  const Json summary = object({{"valid",{{"const",true}}},{"units",{{"const","mm"}}},{"volume_mm3",{{"type","number"}}},{"area_mm2",{{"type","number"}}},
    {"center_of_mass_mm",point},{"bounds_mm",object({{"min",point},{"max",point}},{"min","max"})},
    {"solid_count",{{"type","integer"},{"minimum",0}}},{"face_count",{{"type","integer"},{"minimum",0}}},{"edge_count",{{"type","integer"},{"minimum",0}}}},
    {"valid","units","volume_mm3","area_mm2","center_of_mass_mm","bounds_mm","solid_count","face_count","edge_count"});
  const Json sync = object({{"view_id",id},{"document_id",nullable(id)},{"revision",nullable(revision)},
    {"state",{{"enum",{"empty","loading","ready","error"}}}},{"changed",{{"type","boolean"}}},{"evaluation_id",id},{"feature_id",id},
    {"summary",summary},{"model",{{"$ref","#/$defs/model"}}},{"error",error}}, {"view_id","document_id","revision","state","changed"});
  const Json offset = {{"type","integer"},{"minimum",0},{"maximum",max_view_bytes}};
  const Json chunk = object({{"data",{{"type","string"},{"maxLength",chunk_bytes}}},{"offset",offset},{"next_offset",nullable(offset)},{"total_bytes",offset}},
    {"data","offset","next_offset","total_bytes"});
  auto tool = [&](const char* name,const char* description,Json input,Json output,bool read_only) {
    input["$defs"] = definitions; output["$defs"] = definitions;
    return Json{{"name",name},{"description",description},{"inputSchema",input},{"outputSchema",output},
      {"annotations",{{"readOnlyHint",read_only},{"destructiveHint",false},{"openWorldHint",false}}}};
  };
  auto open = tool("cad_open","Open a workspace-scoped interactive CAD view. Omit document_id to retain its existing document or start empty.",
    object({{"view_id",id},{"document_id",id}},Json::array()),identity,false);
  open["_meta"] = {{"ui",{{"resourceUri",viewer_app_uri}}}};
  open["_meta"]["openai/ui"] = {{"preferredModelDisplayMode","fullscreen"}};
  auto viewer = tool("cad_viewer","App-only view synchronization, frozen mesh chunks and validated selection/camera/prompt context. Geometry is never edited through this tool.",
    {{"type","object"},{"oneOf",Json::array({
      object({{"action",{{"const","sync"}}},{"view_id",id},{"known_evaluation_id",id}},{"action","view_id"}),
      object({{"action",{{"const","mesh"}}},{"view_id",id},{"evaluation_id",id},{"offset",offset}},{"action","view_id","evaluation_id"}),
      object({{"action",{{"const","context"}}},{"view_id",id},{"evaluation_id",id},{"selection",nullable(pick)},{"camera",camera},{"prompt",prompt}},
        {"action","view_id","evaluation_id","selection"})})}},
    {{"type","object"},{"oneOf",Json::array({sync,chunk,context})}},false);
  viewer["_meta"] = {{"ui",{{"visibility",Json::array({"app"})}}}};
  return Json::array({open,
    tool("cad_show","Display a committed document in an existing or new workspace view. Showing a failed view again retries its evaluation.",
      object({{"view_id",id},{"document_id",id}},{"document_id"}),identity,false),
    tool("cad_list","List at most 1000 saved documents and committed HEAD revisions.",object(Json::object(),Json::array()),
      object({{"documents",{{"type","array"},{"maxItems",document_limit},{"items",object({{"document_id",id},{"revision",revision}},{"document_id","revision"})}}},
        {"truncated",{{"type","boolean"}}}}, {"documents","truncated"}),true),
    tool("cad_context","Read validated viewer selection, camera and prompt context. stale reports a changed HEAD or displayed evaluation.",
      object({{"view_id",id}},Json::array()),context,true),viewer});
}
}
