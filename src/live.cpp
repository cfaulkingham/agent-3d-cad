#include "agentcad/live.hpp"
#include "agentcad/app.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <vector>

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
  portable_identifier(id); return id;  // views are workspace-scoped UI state, created on first use
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
  return {{"view_id",id},{"document_id",nullptr},{"generation",nonce()},{"context",nullptr},{"hidden_part_ids",Json::array()}};
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
bool current_pose(const Json& state) {
  if (!state.contains("display")) return false;
  const auto& display=state.at("display");
  if (!state.contains("motion")) return !display.value("draft",false);
  const auto& motion=state.at("motion");
  return !motion.value("saving",false) && display.value("motion_id",Json())==motion.at("id");
}
std::set<std::string> display_part_ids(const Json& state) {
  std::set<std::string> ids;
  if (state.contains("display") && state.at("display").at("summary").contains("assembly"))
    for (const auto& part:state.at("display").at("summary").at("assembly").at("parts")) ids.insert(text_field(part,"id"));
  return ids;
}
Json validate_hidden_parts(const Json& hidden,const Json& state) {
  if (!hidden.is_array() || hidden.size()>64) throw Error("invalid_argument","hidden_part_ids must contain at most 64 unique assembly part IDs");
  const auto available=display_part_ids(state);std::set<std::string> seen;
  for (const auto& part:hidden) {
    if (!part.is_string()) throw Error("invalid_argument","hidden_part_ids entries must be assembly part IDs");
    const auto id=part.get<std::string>();model_identifier(id);
    if (!available.contains(id)) throw Error("invalid_argument","Hidden part is absent from the displayed assembly",{{"part_id",id}});
    if (!seen.insert(id).second) throw Error("invalid_argument","hidden_part_ids must be unique",{{"part_id",id}});
  }
  return hidden;
}
void prune_hidden_parts(Json& state) {
  const auto available=display_part_ids(state);Json retained=Json::array();
  for (const auto& id:state.value("hidden_part_ids",Json::array()))
    if (available.contains(id.get<std::string>())) retained.push_back(id);
  state["hidden_part_ids"]=std::move(retained);
}
Json sync_status(const Json& state, const Json& revision, const std::string& status) {
  return {{"view_id",state.at("view_id")},{"document_id",state.at("document_id")},
    {"revision",revision},{"state",status},{"changed",false},
    {"saving",state.contains("motion") && state.at("motion").value("saving",false)}};
}
Json ready_status(const Json& state, const Json& record, const Json& arguments) {
  const auto& display = state.at("display");
  auto result = sync_status(state,display.at("revision"),"ready");
  result["evaluation_id"] = display.at("evaluation_id");
  result["summary"] = display.at("summary");
  result["feature_id"] = display.at("feature_id");
  result["draft"] = display.value("draft",false);
  if (result.at("draft")==true) result["preview_operations"]=state.at("motion").at("operations");
  result["hidden_part_ids"] = state.value("hidden_part_ids",Json::array());
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
// Retention. A view's frozen copy is readable only while it is the displayed
// evaluation, so every other frozen file is deleted under the view lock (mesh
// readers hold the same lock). Global metadata (evaluations/<id>.json) is read
// only by cad_resolve_selection, which rejects a pick before reading it unless
// HEAD still equals the pick's revision; superseded metadata is therefore
// unusable. Losing a race with deletion yields explicit stale_selection.
constexpr auto sweep_interval = std::chrono::seconds(60);
constexpr auto sweep_grace = std::chrono::seconds(60);
constexpr std::size_t sweep_limit = 4096;
bool evaluation_name(const fs::path& file, std::string& id) {
  if (file.extension() != ".json") return false;
  id = path_to_utf8(file.stem());
  try { identifier(id); return true; } catch (const Error&) { return false; }
}
void remove_quietly(const fs::path& path) { std::error_code ignored; fs::remove(path, ignored); }
// Caller holds the view lock. Keeps only `keep` (empty keeps nothing).
void prune_frozen(const fs::path& view, const std::string& keep) {
  const auto parent = view / "evaluations";
  if (fs::is_symlink(fs::symlink_status(parent)) || !fs::is_directory(parent)) return;
  std::vector<fs::path> stale; std::error_code error;
  for (fs::directory_iterator entry(parent, error), end; !error && entry != end; entry.increment(error)) {
    std::string id;
    if (evaluation_name(entry->path(), id) && id != keep) stale.push_back(entry->path());
  }
  for (const auto& path : stale) remove_quietly(path);
}
// Reads only the top-level document_id and revision of stored metadata,
// stopping as soon as both are known (nlohmann orders object keys).
struct EvaluationIdentity : nlohmann::json_sax<Json> {
  int depth = 0; std::string field; std::optional<std::string> document; std::optional<std::uint64_t> revision;
  bool more() const { return !document || !revision; }
  bool null() override { return more(); }
  bool boolean(bool) override { return more(); }
  bool number_integer(number_integer_t value) override {
    if (depth == 1 && field == "revision" && value > 0) revision = static_cast<std::uint64_t>(value);
    return more();
  }
  bool number_unsigned(number_unsigned_t value) override { if (depth == 1 && field == "revision") revision = value; return more(); }
  bool number_float(number_float_t, const string_t&) override { return more(); }
  bool string(string_t& value) override { if (depth == 1 && field == "document_id") document = value; return more(); }
  bool binary(binary_t&) override { return false; }
  bool start_object(std::size_t) override { return ++depth <= 64; }
  bool key(string_t& value) override { if (depth == 1) field = value; return true; }
  bool end_object() override { --depth; return more(); }
  bool start_array(std::size_t) override { return ++depth <= 64; }
  bool end_array() override { --depth; return more(); }
  bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override { return false; }
};
std::set<std::string> displayed_evaluations(const fs::path& root) {
  std::set<std::string> ids;
  const auto views = root / "views";
  if (!fs::exists(views)) return ids;
  if (fs::is_symlink(fs::symlink_status(views))) throw Error("storage_error", "Managed view directory cannot be a symlink");
  for (const auto& entry : fs::directory_iterator(views)) {
    if (entry.is_symlink() || !entry.is_directory() || !fs::exists(entry.path() / "state.json")) continue;
    // A damaged record cannot display anything; it must not stop the sweep.
    try {
      const auto state = read_state(entry.path());
      if (state.contains("display")) ids.insert(state.at("display").at("evaluation_id").get<std::string>());
    } catch (const std::exception&) { continue; }
  }
  return ids;
}
// Best effort, at most once per sweep_interval per workspace: delete global
// metadata at least sweep_grace old that no view displays and whose document
// HEAD has moved past its revision. Metadata for the current revision is never
// deleted by age (offline cad_view picks stay resolvable). Any doubt keeps it.
void sweep_evaluations(Store& store) {
  try {
    const auto parent = store.root() / "evaluations", marker = parent / ".retention";
    if (fs::is_symlink(fs::symlink_status(parent)) || !fs::is_directory(parent)) return;
    const auto now = fs::file_time_type::clock::now();
    std::error_code error;
    // The marker is a managed path: never read, create or touch it through a link.
    if (fs::is_symlink(fs::symlink_status(marker, error))) return;
    const auto last = fs::last_write_time(marker, error);
    if (!error && now - last < sweep_interval) return;
    if (error) { std::ofstream touch(marker, std::ios::binary | std::ios::app); }
    fs::last_write_time(marker, now, error);
    const auto displayed = displayed_evaluations(store.root());
    std::vector<fs::path> candidates;
    for (fs::directory_iterator entry(parent, error), end; !error && entry != end && candidates.size() < sweep_limit; entry.increment(error)) {
      std::string id; std::error_code status;
      if (!evaluation_name(entry->path(), id) || displayed.contains(id) || entry->is_symlink(status) || !entry->is_regular_file(status)) continue;
      const auto modified = fs::last_write_time(entry->path(), status);
      if (!status && now - modified >= sweep_grace) candidates.push_back(entry->path());
    }
    std::map<std::string, std::optional<Json>> heads; // nullopt: HEAD unknown, keep.
    for (const auto& path : candidates) {
      EvaluationIdentity identity;
      { std::ifstream input(path, std::ios::binary); Json::sax_parse(input, &identity); }
      if (!identity.document || !identity.revision) continue;
      if (!heads.contains(*identity.document)) {
        try { heads[*identity.document] = store.read(*identity.document).at("revision"); }
        catch (const Error& e) { heads[*identity.document] = e.code == "not_found" ? std::optional<Json>(nullptr) : std::nullopt; }
      }
      const auto& head = heads.at(*identity.document);
      if (head && *head != Json(*identity.revision)) remove_quietly(path);
    }
  } catch (const std::exception&) {
    // Retention never changes a sync result; a failed sweep retries later.
  }
}
Json context_result(Store& store, const Json& state) {
  Json result = {{"view_id",state.at("view_id")},{"document_id",state.at("document_id")},
    {"revision",nullptr},{"evaluation_id",nullptr},{"head_revision",nullptr},{"stale",false},{"selection",nullptr},
    {"hidden_part_ids",state.value("hidden_part_ids",Json::array())}};
  if (state.at("document_id").is_null()) return result;
  try { result["head_revision"] = store.read(state.at("document_id").get<std::string>()).at("revision"); }
  catch (const Error& e) { if (e.code != "not_found") throw; }
  if (state.contains("display")) {
    result["revision"] = state.at("display").at("revision");
    result["evaluation_id"] = state.at("display").at("evaluation_id");
    result["draft"] = state.at("display").value("draft",false);
    if (result.at("draft")==true && current_pose(state)) result["preview_operations"]=state.at("motion").at("operations");
  }
  if (!state.at("context").is_null()) result.update(state.at("context"));
  // Visibility describes the current display, even when the saved pick belongs
  // to an older evaluation. It is never restored from that stale context.
  result["hidden_part_ids"]=state.value("hidden_part_ids",Json::array());
  result["stale"] = (!result.at("revision").is_null() && result.at("revision") != result.at("head_revision")) ||
    (!result.at("evaluation_id").is_null() && (!state.contains("display") ||
      result.at("evaluation_id") != state.at("display").at("evaluation_id") || !current_pose(state)));
  return result;
}
void check_display(Store& store, const Json& state, const std::string& evaluation, bool allow_pending=false) {
  if (state.at("document_id").is_null() || !state.contains("display") ||
      state.at("display").at("evaluation_id") != evaluation || (!allow_pending && !current_pose(state)))
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
    if (state.contains("motion") && state.at("motion").at("revision")!=revision) {
      DocumentLock publication(store.root(),document); WorkspaceLock lock(path);
      auto current=read_state(path);
      if (!same_view(current,state) || store.read(document).at("revision")!=revision) continue;
      current.erase("motion");current.erase("pending");current.erase("failure");
      current["generation"]=nonce();save_state(path,current);continue;
    }
    if (current_pose(state) && state.at("display").at("revision") == revision) {
      // Retargeting between the initial snapshot and this result cannot publish
      // an evaluation belonging to the view's former document.
      DocumentLock publication(store.root(),document);
      WorkspaceLock lock(path);
      const auto current = read_state(path);
      if (!same_view(current,state) || store.read(document).at("revision") != revision) continue;
      if (current_pose(current) && current.at("display").at("revision") == revision)
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
      std::string tool="cad_query";Json request={{"document_id",document},{"revision",revision},{"kind","mesh"}};
      if (state.contains("motion")) {
        const auto& motion=state.at("motion");const bool saving=motion.value("saving",false);
        tool=saving?"cad_apply":"cad_preview";
        request={{"document_id",document},{"expected_revision",revision},
          {"operations",motion.at(saving?"save_operations":"operations")}};
        if (saving) request["request_id"]=state.at("pending").at("job_id");
        else {request["kind"]="mesh";request["feature_id"]=motion.at("assembly_id");}
      }
      job = dispatch_job(store.root(),{{"action","submit"},{"request_id",state.at("pending").at("job_id")},
        {"tool",tool},{"arguments",request}});
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
    // A successful save is already committed by Service. The next iteration
    // observes HEAD, retires the draft, and schedules its committed evaluation.
    if (state.contains("motion") && state.at("motion").value("saving",false)) continue;
    const auto& evaluation = job.at("result");
    const bool draft=state.contains("motion");
    if (evaluation.at("document_id") != document || evaluation.at("revision") != revision || evaluation.at("draft") != draft)
      throw Error("storage_error","View job result does not match the requested revision and draft state");
    const auto eid = text_field(evaluation,"evaluation_id"); identifier(eid);
    const auto directory_path = path / "evaluations"; directory(directory_path);
    write_frozen(directory_path / (eid + ".json"),evaluation);
    Json published;
    {
      DocumentLock publication(store.root(),document);
      WorkspaceLock lock(path);
      auto current = read_state(path);
      if (!same_view(current,state) || current.value("pending",Json()) != state.at("pending") ||
          store.read(document).at("revision") != revision) {
        // This result can never publish: the generation, pending revision or
        // HEAD has moved on. Only a sync sharing its job could display it.
        if (!current.contains("display") || current.at("display").at("evaluation_id") != eid) remove_quietly(directory_path / (eid + ".json"));
        continue;
      }
      // A display in the same generation is an older revision of this document,
      // so its metadata is superseded and its frozen copy is no longer readable.
      if (current.contains("display") && current.at("display").at("evaluation_id") != eid &&
          (current.at("display").at("revision") != revision || current.at("display").value("draft",false)))
        remove_quietly(store.root() / "evaluations" / (current.at("display").at("evaluation_id").get<std::string>() + ".json"));
      current["display"] = {{"document_id",document},{"revision",revision},{"evaluation_id",eid},
        {"feature_id",evaluation.at("feature_id")},{"summary",evaluation.at("summary")},{"draft",draft}};
      if (draft) current["display"]["motion_id"]=current.at("motion").at("id");
      prune_hidden_parts(current);
      current.erase("pending"); current.erase("failure"); save_state(path,current);
      prune_frozen(path,eid);
      published = ready_status(current,record,arguments);
    }
    sweep_evaluations(store);
    return published;
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
    if (state.at("display").value("draft",false)) throw Error("stale_selection","Save or reset a motion preview before selecting geometry");
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
    const auto hidden=validate_hidden_parts(arguments.contains("hidden_part_ids")?arguments.at("hidden_part_ids"):
      current.value("hidden_part_ids",Json::array()),current);
    if (context.contains("resolved_selection")) {
      const auto& geometry=context.at("resolved_selection").at("geometry");
      if (geometry.contains("part_id") && std::find(hidden.begin(),hidden.end(),geometry.at("part_id"))!=hidden.end())
        throw Error("invalid_argument","Selection belongs to a hidden assembly part",{{"part_id",geometry.at("part_id")}});
    }
    current["hidden_part_ids"]=hidden;
    current["context"] = context; save_state(path,current); state = std::move(current);
  }
  return context_result(store,state);
}
Json motion_action(Store& store,const Json& arguments) {
  const auto id=view_id(arguments),action=text_field(arguments,"action"),eid=text_field(arguments,"evaluation_id");identifier(eid);
  const auto initial=snapshot(store.root(),id);
  if (initial.at("document_id").is_null()) throw Error("invalid_argument","Open an articulated assembly first");
  const auto document=initial.at("document_id").get<std::string>();const auto path=view_path(store.root(),id);
  {
    DocumentLock publication(store.root(),document);WorkspaceLock lock(path);
    auto state=read_state(path);
    if (!same_view(state,initial)) throw Error("stale_selection","The view changed before the motion request");
    const bool reset_source=action=="motion_reset" && state.contains("motion") && state.at("motion").value("source_evaluation_id",Json())==eid;
    check_display(store,state,reset_source?text_field(state.at("display"),"evaluation_id"):eid,action=="motion_reset");
    if (state.contains("motion") && state.at("motion").value("saving",false) && !state.contains("failure"))
      throw Error("invalid_argument","Wait for the pose save to finish");
    const auto record=store.read(document);const auto assembly_id=text_field(state.at("display"),"feature_id");
    const auto& model=record.at("model");const Json* assembly=nullptr;
    for (const auto& feature:model.at("features")) if (feature.at("id")==assembly_id && feature.at("type")=="assembly") assembly=&feature;
    if (!assembly) throw Error("invalid_argument","The displayed feature is not an articulated assembly");
    const auto motion=assembly_motion(*assembly,model.at("parameters"));
    if (motion.at("dofs").empty()) throw Error("invalid_argument","The displayed assembly has no moving coordinates");
    if (action=="motion_reset") {
      state.erase("motion");
    } else if (action=="motion_save") {
      if (!state.contains("motion") || !state.at("display").value("draft",false))
        throw Error("invalid_argument","Preview a pose before saving it");
      auto operations=state.at("motion").at("operations");
      if (arguments.contains("pose_id")) {
        const auto name=text_field(arguments,"pose_id");model_identifier(name);Json values=Json::array();
        for (const auto& dof:state.at("display").at("summary").at("assembly").at("motion").at("dofs"))
          if (!dof.at("driven").get<bool>()) values.push_back({{"mate_id",dof.at("mate_id")},{"coordinate",dof.at("coordinate")},{"value",dof.at("value")}});
        operations.push_back({{"op","set_pose"},{"assembly_id",assembly_id},{"pose",{{"id",name},{"values",values}}}});
      }
      apply_operations(model,operations);
      state["motion"]["saving"]=true;state["motion"]["save_operations"]=std::move(operations);
    } else {
      Json operations=Json::array();
      if (arguments.contains("pose_id")) {
        const auto pose=text_field(arguments,"pose_id");model_identifier(pose);
        operations.push_back({{"op","apply_pose"},{"assembly_id",assembly_id},{"pose_id",pose}});
      } else {
        const auto& values=arguments.at("values");
        if (!values.is_array() || values.empty() || values.size()>126) throw Error("invalid_argument","Supply every independent motion coordinate, at most 126 values");
        std::set<std::string> expected,seen;
        for (const auto& dof:motion.at("dofs")) if (!dof.at("driven").get<bool>()) expected.insert(text_field(dof,"mate_id")+"."+text_field(dof,"coordinate"));
        for (const auto& value:values) {
          fields(value,{"mate_id","coordinate","value"});number(value.at("value"));
          const auto key=text_field(value,"mate_id")+"."+text_field(value,"coordinate");
          if (!expected.contains(key) || !seen.insert(key).second) throw Error("invalid_argument","Preview values must name each independent coordinate exactly once");
          auto operation=value;operation["op"]="set_joint_value";operation["assembly_id"]=assembly_id;operations.push_back(std::move(operation));
        }
        if (seen!=expected) throw Error("invalid_argument","Preview must include every independent coordinate");
      }
      apply_operations(model,operations); // Validate limits and couplings before changing view state.
      state["motion"]={{"id","motion_"+sha256(nonce()).substr(0,48)},{"revision",record.at("revision")},
        {"assembly_id",assembly_id},{"source_evaluation_id",eid},{"operations",std::move(operations)}};
    }
    // The generation makes any in-flight transfer/build from the former pose
    // unable to publish, even when both poses share the same saved revision.
    state["generation"]=nonce();state.erase("pending");state.erase("failure");
    if (!state.at("context").is_null()) {
      state["context"]["selection"]=nullptr;state["context"].erase("resolved_selection");
    }
    save_state(path,state);
  }
  return synchronize(store,{{"view_id",id}});
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
      // A directory whose name is no longer a valid identifier (for example a
      // POSIX document named after a Windows device) cannot be addressed by any
      // tool; it must not make the whole listing fail.
      const auto id = path_to_utf8(entry.path().filename());
      try { identifier(id); } catch (const Error&) { continue; }
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
    const bool reset = arguments.contains("document_id") && (state.at("document_id") != arguments.at("document_id") ||
        (tool == "cad_show" && state.contains("failure")));
    if (reset) {
      const auto hidden=state.at("document_id")==arguments.at("document_id")?state.value("hidden_part_ids",Json::array()):Json::array();
      state = empty_state(id); state["document_id"] = arguments.at("document_id");
      state["hidden_part_ids"]=hidden;
    }
    save_state(path,state);
    // A new generation has no display, so no frozen copy is readable any more.
    // Its global metadata stays resolvable until superseded (see sweep_evaluations).
    if (reset) prune_frozen(path,"");
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
    fields(arguments,{"action","view_id","evaluation_id","selection"},{"camera","prompt","hidden_part_ids"});
    return publish_context(service,store,arguments);
  }
  if (action=="motion_preview") {
    fields(arguments,{"action","view_id","evaluation_id"},{"values","pose_id"});
    if (arguments.contains("values")==arguments.contains("pose_id")) throw Error("invalid_argument","Preview requires either values or pose_id");
    return motion_action(store,arguments);
  }
  if (action=="motion_reset" || action=="motion_save") {
    if (action=="motion_save") fields(arguments,{"action","view_id","evaluation_id"},{"pose_id"});
    else fields(arguments,{"action","view_id","evaluation_id"});
    return motion_action(store,arguments);
  }
  throw Error("invalid_argument","Unknown viewer action");
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
  const Json hidden_parts={{"type","array"},{"items",id},{"maxItems",64},{"uniqueItems",true}};
  // Preview context cannot carry arbitrary modeling edits. Keeping its two
  // actual operation shapes here also avoids dragging the entire feature
  // vocabulary into every standalone context schema.
  const Json operations={{"type","array"},{"minItems",1},{"maxItems",126},{"items",{{"oneOf",Json::array({
    object({{"op",{{"const","set_joint_value"}}},{"assembly_id",id},{"mate_id",id},
      {"coordinate",{{"enum",{"angle_deg","travel_mm"}}}},{"value",numeric}},{"op","assembly_id","mate_id","coordinate","value"}),
    object({{"op",{{"const","apply_pose"}}},{"assembly_id",id},{"pose_id",id}},{"op","assembly_id","pose_id"})})}}}};
  const Json values={{"type","array"},{"minItems",1},{"maxItems",126},{"items",object({{"mate_id",id},
    {"coordinate",{{"enum",{"angle_deg","travel_mm"}}}},{"value",numeric}},{"mate_id","coordinate","value"})}};
  const Json error = object({{"code",{{"type","string"}}},{"message",{{"type","string"}}},{"details",{{"type","object"}}}}, {"code","message","details"});
  const Json context = object({{"view_id",id},{"document_id",nullable(id)},{"revision",nullable(revision)},{"evaluation_id",nullable(id)},
    {"head_revision",nullable(revision)},{"stale",{{"type","boolean"}}},{"selection",nullable(pick)},
    {"resolved_selection",object({{"reference",pick},{"geometry",{{"oneOf",Json::array({Json{{"$ref","#/$defs/face"}},Json{{"$ref","#/$defs/edge"}}})}}},
      {"selector",{{"$ref","#/$defs/selector"}}}}, {"reference","geometry"})},
    {"camera",camera},{"prompt",prompt},{"hidden_part_ids",hidden_parts},{"updated_at_unix_ms",{{"type","integer"}}},
    {"draft",{{"type","boolean"}}},{"preview_operations",operations}},
    {"view_id","document_id","revision","evaluation_id","head_revision","stale","selection","hidden_part_ids"});
  const Json identity = object({{"view_id",id},{"document_id",nullable(id)},{"resource_uri",{{"const",viewer_app_uri}}}}, {"view_id","document_id","resource_uri"});
  const Json point = {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3}};
  const Json summary = object({{"valid",{{"const",true}}},{"units",{{"const","mm"}}},{"volume_mm3",{{"type","number"}}},{"area_mm2",{{"type","number"}}},
    {"center_of_mass_mm",point},{"bounds_mm",object({{"min",point},{"max",point}},{"min","max"})},
    {"solid_count",{{"type","integer"},{"minimum",0}}},{"face_count",{{"type","integer"},{"minimum",0}}},{"edge_count",{{"type","integer"},{"minimum",0}}},
    {"assembly",{{"$ref","#/$defs/assembly_summary"}}}},
    {"valid","units","volume_mm3","area_mm2","center_of_mass_mm","bounds_mm","solid_count","face_count","edge_count"});
  const Json sync = object({{"view_id",id},{"document_id",nullable(id)},{"revision",nullable(revision)},
    {"state",{{"enum",{"empty","loading","ready","error"}}}},{"changed",{{"type","boolean"}}},{"evaluation_id",id},{"feature_id",id},
    {"summary",summary},{"model",{{"$ref","#/$defs/model"}}},{"error",error},{"hidden_part_ids",hidden_parts},
    {"draft",{{"type","boolean"}}},{"saving",{{"type","boolean"}}},{"preview_operations",operations}}, {"view_id","document_id","revision","state","changed"});
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
  auto viewer = tool("cad_viewer","App-only view synchronization, frozen mesh chunks, assembly visibility and validated context. Motion controls preview native poses, reset, or explicitly save through atomic revision-checked edits.",
    {{"type","object"},{"oneOf",Json::array({
      object({{"action",{{"const","sync"}}},{"view_id",id},{"known_evaluation_id",id}},{"action","view_id"}),
      object({{"action",{{"const","mesh"}}},{"view_id",id},{"evaluation_id",id},{"offset",offset}},{"action","view_id","evaluation_id"}),
      object({{"action",{{"const","motion_preview"}}},{"view_id",id},{"evaluation_id",id},{"values",values}},{"action","view_id","evaluation_id","values"}),
      object({{"action",{{"const","motion_preview"}}},{"view_id",id},{"evaluation_id",id},{"pose_id",id}},{"action","view_id","evaluation_id","pose_id"}),
      object({{"action",{{"const","motion_reset"}}},{"view_id",id},{"evaluation_id",id}},{"action","view_id","evaluation_id"}),
      object({{"action",{{"const","motion_save"}}},{"view_id",id},{"evaluation_id",id},{"pose_id",id}},{"action","view_id","evaluation_id"}),
      object({{"action",{{"const","context"}}},{"view_id",id},{"evaluation_id",id},{"selection",nullable(pick)},{"camera",camera},{"prompt",prompt},{"hidden_part_ids",hidden_parts}},
        {"action","view_id","evaluation_id","selection"})})}},
    {{"type","object"},{"oneOf",Json::array({sync,chunk,context})}},false);
  viewer["_meta"] = {{"ui",{{"visibility",Json::array({"app"})}}}};
  return Json::array({open,
    tool("cad_show","Display a committed document in an existing or new workspace view. Showing a failed view again retries its evaluation.",
      object({{"view_id",id},{"document_id",id}},{"document_id"}),identity,false),
    tool("cad_list","List at most 1000 saved documents and committed HEAD revisions.",object(Json::object(),Json::array()),
      object({{"documents",{{"type","array"},{"maxItems",document_limit},{"items",object({{"document_id",id},{"revision",revision}},{"document_id","revision"})}}},
        {"truncated",{{"type","boolean"}}}}, {"documents","truncated"}),true),
    tool("cad_context","Read validated viewer selection, camera, prompt and current hidden assembly part IDs. stale reports a changed HEAD or displayed evaluation.",
      object({{"view_id",id}},Json::array()),context,true),viewer});
}
}
