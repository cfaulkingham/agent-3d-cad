#include "agentcad/live.hpp"
#include "agentcad/app.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/model.hpp"
#include "agentcad/measurement.hpp"
#include "agentcad/section.hpp"
#include "agentcad/artifact_review.hpp"
#include "agentcad/service.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <thread>
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
  return {{"view_id",id},{"document_id",nullptr},{"generation",nonce()},{"build",AGENTCAD_CACHE_BUILD},{"context",nullptr},{"hidden_part_ids",Json::array()}};
}
Json default_appearance() {return {{"default_color",{0.66,0.75,0.80}},{"parts",Json::array()}};}
Json artifact_status(const Json&,const Json&);
Json artifact_context(Store&,const Json&);
void artifact_display(const Json&,const std::string&);
Json default_camera() {return {{"yaw",-0.65},{"pitch",0.6},{"zoom",1},{"pan",{0,0}}};}
Json default_presentation() {return {{"clip",nullptr},{"explode",{{"distance_mm",0},{"directions",Json::array()}}}};}
Json section_geometry(Json plane,Json explode) {
  Json ordered=Json::array();std::map<std::string,Json> directions;
  if(explode.at("distance_mm")!=0)for(const auto& item:explode.at("directions"))directions[text_field(item,"part_id")]=item;
  for(const auto& [id,item]:directions){(void)id;ordered.push_back(item);}
  explode["directions"]=std::move(ordered);return {{"plane",std::move(plane)},{"explode",std::move(explode)}};
}
Json section_geometry(const Json& presentation) {
  const auto& clip=presentation.at("clip");
  return section_geometry(clip.is_null()?Json(nullptr):Json{{"normal",clip.at("normal")},{"offset_mm",clip.at("offset_mm")}},presentation.at("explode"));
}
bool section_matches(const Json& query,const Json& presentation) {
  return !presentation.at("clip").is_null()&&section_geometry(query.at("plane"),query.value("explode",default_presentation().at("explode")))==section_geometry(presentation);
}
void cancel_view_job(Service& service,const Json& id) {
  // Job cancellation takes its own document publication lock. Never call it
  // while holding a view/document writer lock. Retrying this exact idempotent
  // cancel reconciles only transient metadata contention, not job submission.
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
  for(;;){try{service.call("cad_job",{{"action","cancel"},{"job_id",id}});return;}
    catch(const Error& e){if(e.code!="workspace_busy"||std::chrono::steady_clock::now()>=deadline)throw;}
    std::this_thread::sleep_for(std::chrono::milliseconds(2));}
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
  if (display.value("build",std::string{})!=AGENTCAD_CACHE_BUILD) return false;
  if (!state.contains("motion")) return !display.value("draft",false);
  const auto& motion=state.at("motion");
  return !motion.value("saving",false) && display.value("motion_id",Json())==motion.at("id");
}
Json playback_status(const Json& state) {
  auto playback=state.value("playback",Json(nullptr));if(playback.is_null())return playback;
  const auto applied=playback.value("applied",false);playback.erase("applied");playback["state"]=applied?(current_pose(state)?"displayed":"pending"):"unapplied";return playback;
}
std::set<std::string> display_part_ids(const Json& state) {
  std::set<std::string> ids;if(state.value("read_only",false))return ids;
  if (state.contains("display") && state.at("display").at("summary").contains("assembly"))
    for (const auto& part:state.at("display").at("summary").at("assembly").at("parts")) ids.insert(text_field(part,"id"));
  return ids;
}
Json validate_hidden_parts(const Json& hidden,const Json& state) {
  if (!hidden.is_array() || hidden.size()>assembly_leaf_limit) throw Error("invalid_argument","hidden_part_ids must contain at most 1024 unique assembly occurrence paths");
  const auto available=display_part_ids(state);std::set<std::string> seen;
  for (const auto& part:hidden) {
    if (!part.is_string()) throw Error("invalid_argument","hidden_part_ids entries must be assembly part IDs");
    const auto id=part.get<std::string>();validate_occurrence_path(id);
    if (!available.contains(id)) throw Error("invalid_argument","Hidden part is absent from the displayed assembly",{{"part_id",id}});
    if (!seen.insert(id).second) throw Error("invalid_argument","hidden_part_ids must be unique",{{"part_id",id}});
  }
  return hidden;
}
void unit_direction(const Json& value) {
  if(!value.is_array()||value.size()!=3)throw Error("invalid_argument","Presentation directions need three finite unit-vector coordinates");
  double square=0;
  for(const auto& item:value){const auto v=number(item);if(std::abs(v)>1)throw Error("invalid_argument","Unit-vector coordinates must be within -1 and 1");square+=v*v;}
  if(std::abs(square-1)>1e-6)throw Error("invalid_argument","Presentation direction must be a unit vector");
}
Json validate_presentation(const Json& value,const Json& state) {
  fields(value,{"clip","explode"});
  if(!value.at("clip").is_null()) {
    const auto& clip=value.at("clip");fields(clip,{"normal","offset_mm","keep"});unit_direction(clip.at("normal"));
    const auto& offset=clip.at("offset_mm");if(!offset.is_number()||!std::isfinite(offset.get<double>())||std::abs(offset.get<double>())>1e12)throw Error("invalid_argument","Clipping offset exceeds coordinate limits");
    if(clip.at("keep")!="positive"&&clip.at("keep")!="negative")throw Error("invalid_argument","Choose positive or negative clipping half-space");
  }
  const auto& explode=value.at("explode");fields(explode,{"distance_mm","directions"});
  const auto distance=number(explode.at("distance_mm"));if(distance<0||distance>1e6)throw Error("invalid_argument","Exploded distance must be between 0 and 1000000 mm");
  const auto& directions=explode.at("directions");if(!directions.is_array()||directions.size()>assembly_leaf_limit)throw Error("invalid_argument","Exploded directions exceed 1024 leaves");
  const auto available=display_part_ids(state);std::set<std::string> seen;
  if(distance>0&&available.empty())throw Error("invalid_argument","Exploded presentation needs an assembly");
  for(const auto& item:directions) {
    fields(item,{"part_id","direction"});const auto id=text_field(item,"part_id");validate_occurrence_path(id);unit_direction(item.at("direction"));
    if(!available.contains(id)||!seen.insert(id).second)throw Error("invalid_argument","Exploded directions must name unique current leaf occurrences",{{"part_id",id}});
  }
  return value;
}
Json validate_camera(const Json& camera) {
  fields(camera,{"yaw","pitch","zoom","pan"});number(camera.at("yaw"));number(camera.at("pitch"));
  const auto zoom=number(camera.at("zoom"));if(zoom<0.01||zoom>1000)throw Error("invalid_argument","Camera zoom must be between 0.01 and 1000");
  if(!camera.at("pan").is_array()||camera.at("pan").size()!=2)throw Error("invalid_argument","Camera pan needs two finite values");
  for(const auto& value:camera.at("pan"))number(value);return camera;
}
void validate_color(const Json& color) {
  if(!color.is_array()||color.size()!=3)throw Error("invalid_argument","Appearance colors need three RGB components");
  for(const auto& component:color){const auto n=number(component);if(n<0||n>1)throw Error("invalid_argument","Appearance RGB components must be between zero and one");}
}
Json validate_appearance(const Json& value,const Json& state) {
  fields(value,{"default_color","parts"});validate_color(value.at("default_color"));const auto& parts=value.at("parts");
  if(!parts.is_array()||parts.size()>assembly_leaf_limit)throw Error("invalid_argument","Appearance overrides exceed 1024 leaf occurrences");
  const auto available=display_part_ids(state);std::set<std::string> seen;
  for(const auto& item:parts){fields(item,{"part_id","color"});const auto id=text_field(item,"part_id");validate_occurrence_path(id);validate_color(item.at("color"));
    if(!available.contains(id)||!seen.insert(id).second)throw Error("invalid_argument","Appearance overrides must name unique current leaf occurrences",{{"part_id",id}});}
  return value;
}
void prune_appearance(Json& review,const Json& state) {
  auto appearance=review.value("appearance",default_appearance());const auto available=display_part_ids(state);Json parts=Json::array();
  for(const auto& item:appearance.at("parts"))if(available.contains(text_field(item,"part_id")))parts.push_back(item);
  appearance["parts"]=std::move(parts);review["appearance"]=validate_appearance(appearance,state);
}
void prune_presets(Json& state) {
  auto presets=state.value("presets",Json::array());const auto available=display_part_ids(state);
  for(auto& preset:presets){Json hidden=Json::array(),directions=Json::array();
    for(const auto& id:preset.at("hidden_part_ids"))if(available.contains(id.get<std::string>()))hidden.push_back(id);
    for(const auto& item:preset.at("presentation").at("explode").at("directions"))if(available.contains(text_field(item,"part_id")))directions.push_back(item);
    preset["hidden_part_ids"]=std::move(hidden);preset["presentation"]["explode"]["directions"]=std::move(directions);
    if(available.empty())preset["presentation"]["explode"]["distance_mm"]=0;
    preset["presentation"]=validate_presentation(preset.at("presentation"),state);prune_appearance(preset,state);
  }
  state["presets"]=std::move(presets);
}
void prune_presentation(Json& state) {
  auto presentation=state.value("presentation",default_presentation());const auto available=display_part_ids(state);Json directions=Json::array();
  for(const auto& item:presentation.at("explode").at("directions"))if(available.contains(text_field(item,"part_id")))directions.push_back(item);
  presentation["explode"]["directions"]=directions;
  if(available.empty())presentation["explode"]["distance_mm"]=0;
  state["presentation"]=validate_presentation(presentation,state);
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
// Review anchors are inspection coordinates in one committed evaluation, never
// stable design references. Keep their text after source changes without rebinding.
Json annotation_records(const Json& state,const Json& head) {
  auto notes=state.value("annotations",Json::array());
  for(auto& note:notes) {
    const bool active=state.contains("display")&&current_pose(state)&&!state.at("display").value("draft",false)&&
      note.at("document_id")==state.at("document_id")&&note.at("revision")==head&&
      note.at("revision")==state.at("display").at("revision")&&note.at("evaluation_id")==state.at("display").at("evaluation_id")&&
      note.at("feature_id")==state.at("display").at("feature_id");
    note["status"]=active?"current":"retired";
  }
  return notes;
}
void annotation_text(const Json& value) {
  if(!value.is_string())throw Error("invalid_argument","Annotation text must be a string");
  const auto text=value.get<std::string>();
  if(text.empty()||text.size()>512||std::all_of(text.begin(),text.end(),[](unsigned char c){return std::isspace(c);})||
    std::any_of(text.begin(),text.end(),[](unsigned char c){return (c<32&&c!=9&&c!=10)||c==127;}))
    throw Error("invalid_argument","Annotation text needs 1 to 512 UTF-8 bytes; only tab and newline control characters are allowed");
}
Json bounded_anchor_point(const Json& point) {
  if(!point.is_array()||point.size()!=3)throw Error("invalid_argument","Annotation anchor needs three coordinates");
  for(const auto& item:point)if(!item.is_number()||!std::isfinite(item.get<double>())||std::abs(item.get<double>())>1e12)throw Error("invalid_argument","Annotation anchor exceeds coordinate limits");
  return point;
}
Json bounds_center(const Json& bounds) {
  Json point=Json::array();for(int i=0;i<3;++i)point.push_back((bounded_anchor_point(bounds.at("min"))[i].get<double>()+bounded_anchor_point(bounds.at("max"))[i].get<double>())/2);
  return bounded_anchor_point(point);
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
  result["presentation"] = state.value("presentation",default_presentation());
  result["appearance"]=state.value("appearance",default_appearance());result["presets"]=state.value("presets",Json::array());
  result["annotations"]=annotation_records(state,record.at("revision"));
  result["sequences"]=state.value("sequences",Json::array());result["playback"]=playback_status(state);
  if(!state.at("context").is_null()&&state.at("context").at("evaluation_id")==display.at("evaluation_id")&&state.at("context").contains("camera"))result["camera"]=state.at("context").at("camera");
  if(state.contains("measurement")&&state.at("measurement").at("evaluation_id")==display.at("evaluation_id"))result["measurement"]=state.at("measurement");
  if(state.contains("section")&&state.at("section").at("evaluation_id")==display.at("evaluation_id")&&section_matches(state.at("section").at("query"),result.at("presentation")))result["section"]=state.at("section");
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
  if(state.value("read_only",false))return artifact_context(store,state);
  Json result = {{"view_id",state.at("view_id")},{"document_id",state.at("document_id")},
    {"revision",nullptr},{"evaluation_id",nullptr},{"feature_id",nullptr},{"head_revision",nullptr},{"stale",false},{"selection",nullptr},
    {"hidden_part_ids",state.value("hidden_part_ids",Json::array())},{"presentation",state.value("presentation",default_presentation())},{"appearance",state.value("appearance",default_appearance())},{"presets",state.value("presets",Json::array())},{"annotations",Json::array()},{"sequences",state.value("sequences",Json::array())},{"playback",playback_status(state)}};
  if (state.at("document_id").is_null()) return result;
  try { result["head_revision"] = store.read(state.at("document_id").get<std::string>()).at("revision"); }
  catch (const Error& e) { if (e.code != "not_found") throw; }
  if (state.contains("display")) {
    result["revision"] = state.at("display").at("revision");
    result["evaluation_id"] = state.at("display").at("evaluation_id");result["feature_id"]=state.at("display").at("feature_id");
    result["draft"] = state.at("display").value("draft",false);
    if (result.at("draft")==true && current_pose(state)) result["preview_operations"]=state.at("motion").at("operations");
  }
  if (!state.at("context").is_null()) {
    result.update(state.at("context"));
    // Legacy saved contexts did not record feature identity. Never combine a
    // legacy old evaluation with a different display's current feature.
    if(state.contains("display")&&state.at("context").at("evaluation_id")!=state.at("display").at("evaluation_id")&&!state.at("context").contains("feature_id"))result["feature_id"]=nullptr;
  }
  // Visibility describes the current display, even when the saved pick belongs
  // to an older evaluation. It is never restored from that stale context.
  result["hidden_part_ids"]=state.value("hidden_part_ids",Json::array());
  result["presentation"]=state.value("presentation",default_presentation());
  result["appearance"]=state.value("appearance",default_appearance());result["presets"]=state.value("presets",Json::array());
  result["annotations"]=annotation_records(state,result.at("head_revision"));
  result["sequences"]=state.value("sequences",Json::array());result["playback"]=playback_status(state);
  result["stale"] = (!result.at("revision").is_null() && result.at("revision") != result.at("head_revision")) ||
    (!result.at("evaluation_id").is_null() && (!state.contains("display") ||
      result.at("evaluation_id") != state.at("display").at("evaluation_id") || !current_pose(state)));
  if(result.at("stale")==false&&state.contains("measurement")&&state.at("measurement").at("evaluation_id")==result.at("evaluation_id"))result["measurement"]=state.at("measurement");
  if(result.at("stale")==false&&state.contains("section")&&state.at("section").at("evaluation_id")==result.at("evaluation_id")&&section_matches(state.at("section").at("query"),result.at("presentation")))result["section"]=state.at("section");
  return result;
}
void check_display(Store& store, const Json& state, const std::string& evaluation, bool allow_pending=false) {
  if(state.value("read_only",false))throw Error("read_only_artifact","This operation requires a committed editable native document");
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
    if(state.value("read_only",false))return artifact_status(state,arguments);
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
      current.erase("motion");current.erase("playback");current.erase("pending");current.erase("failure");
      current["generation"]=nonce();save_state(path,current);continue;
    }
    if (state.value("build",std::string{})!=AGENTCAD_CACHE_BUILD &&
        !(state.contains("motion") && state.at("motion").value("saving",false))) {
      // Frozen summaries and completed read jobs belong to the producing build.
      // Keep draft intent and presentation, but regenerate derived view data.
      // An admitted save must reconcile its existing request before migration.
      DocumentLock publication(store.root(),document); WorkspaceLock lock(path);
      auto current=read_state(path);
      if (!same_view(current,state) || store.read(document).at("revision")!=revision) continue;
      current["build"]=AGENTCAD_CACHE_BUILD;current["generation"]=nonce();
      current.erase("pending");current.erase("failure");save_state(path,current);continue;
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
        {"feature_id",evaluation.at("feature_id")},{"summary",evaluation.at("summary")},{"draft",draft},{"build",AGENTCAD_CACHE_BUILD}};
      if (draft) current["display"]["motion_id"]=current.at("motion").at("id");
      prune_hidden_parts(current);
      prune_presentation(current);prune_appearance(current,current);prune_presets(current);
      current.erase("measurement");
      current.erase("section");
      if(current.contains("playback")&&(current.at("playback").at("source").at("revision")!=revision||current.at("playback").at("source").at("model_sha256")!=sha256(record.at("model").dump())))current.erase("playback");
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
  if (!state.value("read_only",false)&&state.at("document_id").is_null()) throw Error("stale_selection","View is empty");
  const auto offset_json = arguments.value("offset",Json(0));
  if (!offset_json.is_number_integer() || offset_json < 0 || offset_json > max_view_bytes)
    throw Error("invalid_argument","Mesh offset must be an integer between zero and 64 MiB");
  const auto offset = offset_json.get<std::size_t>();
  std::unique_ptr<DocumentLock> publication;
  if(!state.value("read_only",false))publication=std::make_unique<DocumentLock>(store.root(),state.at("document_id").get<std::string>());
  const auto path = view_path(store.root(),id); WorkspaceLock lock(path);
  const auto current = read_state(path);
  if (!same_view(current,state)) throw Error("stale_selection","View changed before the mesh could be read");
  if(current.value("read_only",false))artifact_display(current,eid);else check_display(store,current,eid);
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
Json measure_view(Service& service,Store& store,const Json& arguments,const std::string& slot="measurement"){
  const auto id=view_id(arguments),eid=text_field(arguments,"evaluation_id");identifier(eid);
  const auto path=view_path(store.root(),id);auto state=snapshot(store.root(),id);check_display(store,state,eid);
  std::optional<Json> admitted;
  if(state.at("display").value("draft",false))throw Error("draft_selection","Save or reset the displayed pose before measuring committed geometry");
  if(arguments.contains("query")){
    if(!arguments.at("query").is_null()){
      validate_measurement_query(arguments.at("query"));
      const auto is_section=text_field(arguments.at("query"),"action")=="section";
      if(is_section!=(slot=="section"))throw Error("invalid_argument","Use the matching live section or measurement action");
    }
    if(arguments.at("query").is_null()&&state.contains(slot))cancel_view_job(service,state.at(slot).at("job_id"));
    DocumentLock publication(store.root(),text_field(state,"document_id"),LockWait::publication);WorkspaceLock lock(path);
    auto current=read_state(path);if(!same_view(current,state))throw Error("stale_selection","Measurement view changed");check_display(store,current,eid);
    if(slot=="section"&&!arguments.at("query").is_null()&&!section_matches(arguments.at("query"),current.value("presentation",default_presentation())))throw Error("stale_selection","Section query does not match the current clipping plane and exploded placement");
    if(arguments.at("query").is_null()){
      if(current.value(slot,Json())!=state.value(slot,Json()))throw Error("stale_selection","View job changed before it could be cleared");
      current.erase(slot);
    }else{
      const auto& display=current.at("display");const Json input={{"document_id",current.at("document_id")},{"revision",display.at("revision")},
        {"evaluation_id",eid},{"feature_id",display.at("feature_id")},{"query",arguments.at("query")}};
      const auto job=service.call("cad_job",{{"action","submit"},{"request_id",(slot=="section"?"section_":"measure_")+nonce()},{"tool","cad_measure"},{"arguments",input}});
      admitted=job;
      current[slot]={{"evaluation_id",eid},{"job_id",job.at("job_id")},{"query",arguments.at("query")}};
    }
    save_state(path,current);state=std::move(current);
  }
  Json response={{"view_id",id},{"evaluation_id",eid},{"state","empty"}};
  if(!state.contains(slot))return response;
  const auto& meta=state.at(slot);if(meta.at("evaluation_id")!=eid)throw Error("stale_selection","Measurement belongs to a retired evaluation");
  if(slot=="section"&&!section_matches(meta.at("query"),state.value("presentation",default_presentation())))throw Error("stale_selection","Section presentation has changed");
  // The saved view and admission snapshot acknowledge this unique start. A
  // second metadata read can contend with the newly admitted worker, turning a
  // successful start into an uncertain error. Polling is a separate read.
  if(admitted){response.update(meta);response["state"]=admitted->at("state");return response;}
  const auto job=service.call("cad_job",{{"action","get"},{"job_id",meta.at("job_id")}});
  if(job.at("tool")!="cad_measure")throw Error("storage_error","View measurement references another job type");
  response.update(meta);response["state"]=job.at("state");
  if(job.contains("result")){
    const auto& result=job.at("result");for(const auto* key:{"document_id","revision","evaluation_id","feature_id"})
      if(result.at(key)!=(std::string(key)=="document_id"?state.at(key):state.at("display").at(key)))throw Error("stale_selection","Measurement job result belongs to another source evaluation");
    if((result.at("report").at("action")=="section")!=(slot=="section"))throw Error("storage_error","View references the wrong measurement result type");
    response["result"]=result;
  }
  if(job.contains("error"))response["error"]=job.at("error");
  DocumentLock publication(store.root(),text_field(state,"document_id"),LockWait::publication);WorkspaceLock lock(path);
  const auto current=read_state(path);check_display(store,current,eid);
  if(!same_view(current,state)||!current.contains(slot)||current.at(slot)!=meta||(slot=="section"&&!section_matches(meta.at("query"),current.value("presentation",default_presentation()))))throw Error("stale_selection","Measurement changed during polling");
  return response;
}

Json publish_context(Service& service, Store& store, const Json& arguments) {
  const auto id = view_id(arguments), eid = text_field(arguments,"evaluation_id"); identifier(eid);
  auto state = snapshot(store.root(),id); check_display(store,state,eid);
  Json context = {{"revision",state.at("display").at("revision")},{"evaluation_id",eid},{"feature_id",state.at("display").at("feature_id")},{"selection",arguments.at("selection")}};
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
  if(arguments.contains("camera"))context["camera"]=validate_camera(arguments.at("camera"));
  if (arguments.contains("prompt")) {
    const auto prompt = text_field(arguments,"prompt");
    if (prompt.size() > 8192) throw Error("limit_exceeded","Viewer prompt exceeds 8192 UTF-8 bytes");
    context["prompt"] = prompt;
  }
  context["updated_at_unix_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  std::optional<Json> retired_section;
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
    current["appearance"]=validate_appearance(arguments.contains("appearance")?arguments.at("appearance"):current.value("appearance",default_appearance()),current);
    current["hidden_part_ids"]=hidden;
    if(arguments.contains("presentation")&&arguments.at("presentation")!=current.value("presentation",default_presentation()))current.erase("playback");
    current["presentation"]=validate_presentation(arguments.contains("presentation")?arguments.at("presentation"):
      current.value("presentation",default_presentation()),current);
    if(current.contains("section")&&!section_matches(current.at("section").at("query"),current.at("presentation"))){
      retired_section=current.at("section").at("job_id");current.erase("section");
    }
    current["context"] = context; save_state(path,current); state = std::move(current);
  }
  if(retired_section)cancel_view_job(service,*retired_section);
  return context_result(store,state);
}
Json preset_action(Service& service,Store& store,const Json& arguments) {
  const auto id=view_id(arguments),eid=text_field(arguments,"evaluation_id"),operation=text_field(arguments,"operation");identifier(eid);
  if(operation!="list"&&operation!="save"&&operation!="apply"&&operation!="delete")throw Error("invalid_argument","Unknown review preset operation");
  std::string name;
  if(operation!="list") {
    name=text_field(arguments,"name");if(name.size()>64||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;})||std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isspace(c);}))throw Error("invalid_argument","Preset names need 1 to 64 UTF-8 bytes without control characters");
  }
  auto state=snapshot(store.root(),id);check_display(store,state,eid);std::optional<Json> retired_section;
  {
    DocumentLock publication(store.root(),state.at("document_id").get<std::string>());const auto path=view_path(store.root(),id);WorkspaceLock lock(path);auto current=read_state(path);
    if(!same_view(current,state))throw Error("stale_selection","View changed before preset operation");check_display(store,current,eid);
    if((operation=="save"||operation=="apply")&&current.at("display").value("draft",false))throw Error("stale_selection","Save or reset motion preview before saving or applying a review preset");
    auto presets=current.value("presets",Json::array());auto found=std::find_if(presets.begin(),presets.end(),[&](const Json& v){return v.at("name")==name;});
    if(operation=="save") {
      if(found==presets.end()&&presets.size()>=16)throw Error("limit_exceeded","A view stores at most 16 review presets");
      auto camera=default_camera();if(!current.at("context").is_null()&&current.at("context").at("evaluation_id")==eid&&current.at("context").contains("camera"))camera=current.at("context").at("camera");
      Json saved={{"name",name},{"camera",validate_camera(camera)},{"presentation",validate_presentation(current.value("presentation",default_presentation()),current)},
        {"hidden_part_ids",validate_hidden_parts(current.value("hidden_part_ids",Json::array()),current)},{"appearance",validate_appearance(current.value("appearance",default_appearance()),current)}};
      if(found==presets.end())presets.push_back(std::move(saved));else *found=std::move(saved);
    } else if(operation=="apply"||operation=="delete") {
      if(found==presets.end())throw Error("not_found","Review preset is absent",{{"name",name}});
      if(operation=="delete")presets.erase(found);
      else {
        const auto presentation=validate_presentation(found->at("presentation"),current),appearance=validate_appearance(found->at("appearance"),current),hidden=validate_hidden_parts(found->at("hidden_part_ids"),current),camera=validate_camera(found->at("camera"));
        if(current.value("presentation",default_presentation())!=presentation)current.erase("playback");
        current["presentation"]=presentation;current["appearance"]=appearance;current["hidden_part_ids"]=hidden;
        if(current.contains("section")&&!section_matches(current.at("section").at("query"),presentation)){retired_section=current.at("section").at("job_id");current.erase("section");}
        Json context={{"revision",current.at("display").at("revision")},{"evaluation_id",eid},{"feature_id",current.at("display").at("feature_id")},{"selection",nullptr},{"camera",camera},
          {"updated_at_unix_ms",std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()}};
        if(!current.at("context").is_null()&&current.at("context").at("evaluation_id")==eid&&current.at("context").contains("prompt"))context["prompt"]=current.at("context").at("prompt");
        current["context"]=std::move(context);
      }
    }
    std::sort(presets.begin(),presets.end(),[](const Json& a,const Json& b){return a.at("name").get<std::string>()<b.at("name").get<std::string>();});current["presets"]=std::move(presets);
    if(operation!="list")save_state(path,current);state=std::move(current);
  }
  if(retired_section)cancel_view_job(service,*retired_section);
  // Preset operations were admitted for the current display. An old saved pick
  // remains observable through cad_context, but cannot qualify this action's
  // response or restore its old camera/selection after source or motion refresh.
  if(state.at("context").is_null()||state.at("context").at("evaluation_id")!=eid)
    state["context"]={{"revision",state.at("display").at("revision")},{"evaluation_id",eid},{"feature_id",state.at("display").at("feature_id")},{"selection",nullptr}};
  return context_result(store,state);
}
Json annotation_action(Service& service,Store& store,const Json& arguments) {
  const auto id=view_id(arguments),eid=text_field(arguments,"evaluation_id"),operation=text_field(arguments,"operation");identifier(eid);
  if(operation!="list"&&operation!="add"&&operation!="update"&&operation!="delete"&&operation!="clear")throw Error("invalid_argument","Unknown annotation operation");
  auto state=snapshot(store.root(),id);check_display(store,state,eid);Json anchor;
  if(operation=="add"||operation=="update")annotation_text(arguments.at("text"));
  if(operation=="add") {
    if(state.at("display").value("draft",false))throw Error("stale_selection","Save or reset motion preview before adding an inspection anchor");
    const auto& request=arguments.at("anchor");const auto kind=text_field(request,"kind");
    anchor={{"kind",kind},{"part_id",nullptr},{"position_semantics","bounds_center"}};
    if(kind=="model") {fields(request,{"kind"});anchor["point_mm"]=bounds_center(state.at("display").at("summary").at("bounds_mm"));}
    else if(kind=="part") {
      fields(request,{"kind","part_id"});const auto part=text_field(request,"part_id");validate_occurrence_path(part);bool found=false;
      if(state.at("display").at("summary").contains("assembly"))for(const auto& leaf:state.at("display").at("summary").at("assembly").at("parts"))if(leaf.at("id")==part){anchor["part_id"]=part;anchor["point_mm"]=bounds_center(leaf.at("bounds_mm"));found=true;break;}
      if(!found)throw Error("invalid_argument","Annotation owner must be a current assembly leaf",{{"part_id",part}});
    } else if(kind=="entity") {
      fields(request,{"kind","reference"});const auto& ref=request.at("reference");fields(ref,{"document_id","revision","evaluation_id","feature_id","kind","entity_id"});
      for(const auto* key:{"document_id","revision","evaluation_id","feature_id"})if(ref.at(key)!=state.at("display").at(key))throw Error("stale_selection","Annotation reference belongs to another evaluation");
      if(ref.at("kind")!="face"&&ref.at("kind")!="edge")throw Error("invalid_argument","Annotation entity must be a source face or edge");
      // Resolve before acquiring the publication lock; native resolution has its
      // own storage reads. Recheck the full displayed revision under the lock.
      const auto resolved=service.call("cad_resolve_selection",ref);const auto& geometry=resolved.at("geometry");
      anchor["point_mm"]=bounded_anchor_point(geometry.at("center_mm"));anchor["part_id"]=geometry.value("part_id",Json(nullptr));
      if(!anchor.at("part_id").is_null()&&!display_part_ids(state).contains(anchor.at("part_id").get<std::string>()))throw Error("stale_selection","Resolved annotation owner is absent");
      anchor["position_semantics"]="entity_center";anchor["reference"]=ref;
    } else throw Error("invalid_argument","Choose a model, leaf or resolved entity annotation anchor");
  }
  {
    DocumentLock publication(store.root(),state.at("document_id").get<std::string>());const auto path=view_path(store.root(),id);WorkspaceLock lock(path);auto current=read_state(path);
    if(!same_view(current,state))throw Error("stale_selection","View changed before annotation operation");check_display(store,current,eid);
    if(operation=="add"&&current.at("display").value("draft",false))throw Error("stale_selection","Inspection anchors require committed geometry");
    auto notes=current.value("annotations",Json::array());
    if(operation=="add") {
      if(notes.size()>=32)throw Error("limit_exceeded","A view stores at most 32 annotations, including retired notes");
      const auto& display=current.at("display");notes.push_back({{"id","ann_"+nonce()},{"text",arguments.at("text")},
        {"document_id",display.at("document_id")},{"revision",display.at("revision")},{"evaluation_id",eid},{"feature_id",display.at("feature_id")},
        {"anchor_lifetime","evaluation"},{"coordinate_space","committed_source_pose"},{"anchor",anchor}});
    } else if(operation=="update"||operation=="delete") {
      const auto note_id=text_field(arguments,"annotation_id");identifier(note_id);
      auto found=std::find_if(notes.begin(),notes.end(),[&](const Json& note){return note.at("id")==note_id;});
      if(found==notes.end())throw Error("not_found","Annotation is absent",{{"annotation_id",note_id}});
      if(operation=="update")(*found)["text"]=arguments.at("text");else notes.erase(found);
    } else if(operation=="clear")notes=Json::array();
    current["annotations"]=std::move(notes);if(operation!="list")save_state(path,current);state=std::move(current);
  }
  if(state.at("context").is_null()||state.at("context").at("evaluation_id")!=eid)
    state["context"]={{"revision",state.at("display").at("revision")},{"evaluation_id",eid},{"feature_id",state.at("display").at("feature_id")},{"selection",nullptr}};
  return context_result(store,state);
}
Json sequence_source(const Json& record,const Json& display) {
  return {{"document_id",display.at("document_id")},{"revision",record.at("revision")},{"evaluation_id",display.at("evaluation_id")},{"feature_id",display.at("feature_id")},{"model_sha256",sha256(record.at("model").dump())}};
}
bool sequence_current(const Json& source,const Json& record,const Json& display) {
  return source.at("document_id")==display.at("document_id")&&source.at("revision")==record.at("revision")&&source.at("feature_id")==display.at("feature_id")&&source.at("model_sha256")==sha256(record.at("model").dump());
}
std::string sequence_name(const Json& arguments) {
  const auto name=text_field(arguments,"name");if(name.size()>64||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;})||std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isspace(c);}))throw Error("invalid_argument","Sequence names need 1 to 64 UTF-8 bytes without control characters");return name;
}
Json sequence_operations(const Json& joints,const Json& model,const std::string& feature) {
  if(!joints.is_array()||joints.size()>16)throw Error("invalid_argument","A sequence supports at most sixteen mechanism definitions");
  const auto mechanisms=assembly_mechanisms(model,feature);std::set<std::string> scopes;Json operations=Json::array();
  for(const auto& item:joints) {
    fields(item,{"assembly_id","values"});const auto id=text_field(item,"assembly_id");model_identifier(id);const Json* scope=nullptr;
    for(const auto& candidate:mechanisms)if(candidate.at("assembly_id")==id)scope=&candidate;
    if(!scope||!scopes.insert(id).second)throw Error("invalid_argument","Sequence mechanism definitions must be unique and reachable");
    const auto& values=item.at("values");if(!values.is_array()||values.empty()||values.size()>126)throw Error("invalid_argument","Sequence values need every independent coordinate");
    std::set<std::string> expected,seen;for(const auto& dof:scope->at("motion").at("dofs"))if(!dof.at("driven").get<bool>())expected.insert(text_field(dof,"mate_id")+"."+text_field(dof,"coordinate"));
    for(const auto& value:values){fields(value,{"mate_id","coordinate","value"});number(value.at("value"));const auto key=text_field(value,"mate_id")+"."+text_field(value,"coordinate");
      if(!expected.contains(key)||!seen.insert(key).second)throw Error("invalid_argument","Sequence values must include every independent coordinate exactly once");
      auto operation=value;operation["op"]="set_joint_value";operation["assembly_id"]=id;operations.push_back(std::move(operation));}
    if(seen!=expected)throw Error("invalid_argument","Sequence omits an independent coordinate");
  }
  if(operations.size()>126)throw Error("limit_exceeded","A sequence frame supports at most 126 independent coordinates in total");
  if(!operations.empty())apply_operations(model,operations);return operations;
}
Json sequence_shape(const Json& frame) {
  auto presentation=frame.at("presentation");presentation["explode"]["distance_mm"]=1;auto geometry=section_geometry(presentation);geometry["plane"]=presentation.at("clip").is_null()?Json(nullptr):Json{{"normal",presentation.at("clip").at("normal")},{"keep",presentation.at("clip").at("keep")}};
  Json scopes=Json::object();for(const auto& item:frame.at("joints")){Json keys=Json::array();for(const auto& value:item.at("values"))keys.push_back(text_field(value,"mate_id")+"."+text_field(value,"coordinate"));std::sort(keys.begin(),keys.end());scopes[text_field(item,"assembly_id")]=keys;}return {{"presentation",geometry},{"joints",scopes}};
}
Json validate_sequence(const Json& value,const Json& state,const Json& record) {
  fields(value,{"name","frames"});sequence_name(value);const auto& frames=value.at("frames");
  if(!frames.is_array()||frames.size()<2||frames.size()>64||value.dump().size()>256*1024)throw Error("limit_exceeded","Sequences need 2 to 64 keyframes within 256 KiB");
  double previous=-1;Json shape;
  for(const auto& frame:frames){fields(frame,{"time_s","presentation","joints"});const auto time=number(frame.at("time_s"));if(time<0||time>3600||time<=previous||(previous<0&&time!=0))throw Error("invalid_argument","Sequence times start at zero and increase to at most 3600 seconds");previous=time;
    validate_presentation(frame.at("presentation"),state);sequence_operations(frame.at("joints"),record.at("model"),text_field(state.at("display"),"feature_id"));
    const auto current=sequence_shape(frame);if(shape.is_null())shape=current;else if(shape!=current)throw Error("invalid_argument","Sequence frames must retain the same mechanisms, clipping normal/side and explode directions");}
  return value;
}
Json sequence_sample(const Json& sequence,double time) {
  const auto& frames=sequence.at("frames");if(time<0||time>frames.back().at("time_s").get<double>())throw Error("invalid_argument","Seek time lies outside this sequence");
  std::size_t upper=1;while(upper+1<frames.size()&&frames[upper].at("time_s").get<double>()<time)++upper;
  const auto& a=frames[upper-1];const auto& b=frames[upper];const auto t=(time-a.at("time_s").get<double>())/(b.at("time_s").get<double>()-a.at("time_s").get<double>());auto sample=a;
  sample["time_s"]=time;auto& presentation=sample["presentation"];presentation["explode"]["distance_mm"]=a.at("presentation").at("explode").at("distance_mm").get<double>()*(1-t)+b.at("presentation").at("explode").at("distance_mm").get<double>()*t;
  if(!presentation.at("clip").is_null())presentation["clip"]["offset_mm"]=a.at("presentation").at("clip").at("offset_mm").get<double>()*(1-t)+b.at("presentation").at("clip").at("offset_mm").get<double>()*t;
  std::map<std::string,double> ends;for(const auto& scope:b.at("joints"))for(const auto& v:scope.at("values"))ends[text_field(scope,"assembly_id")+"/"+text_field(v,"mate_id")+"/"+text_field(v,"coordinate")]=v.at("value").get<double>();
  for(auto& scope:sample["joints"])for(auto& v:scope["values"]){const auto key=text_field(scope,"assembly_id")+"/"+text_field(v,"mate_id")+"/"+text_field(v,"coordinate");v["value"]=v.at("value").get<double>()*(1-t)+ends.at(key)*t;}return sample;
}
Json sequence_action(Service& service,Store& store,const Json& arguments) {
  const auto id=view_id(arguments),eid=text_field(arguments,"evaluation_id"),operation=text_field(arguments,"operation");identifier(eid);auto state=snapshot(store.root(),id);check_display(store,state,eid);
  std::optional<Json> retired_section;bool evaluate=false;
  {
    DocumentLock publication(store.root(),state.at("document_id").get<std::string>());const auto path=view_path(store.root(),id);WorkspaceLock lock(path);auto current=read_state(path);
    if(!same_view(current,state))throw Error("stale_selection","View changed before sequence operation");check_display(store,current,eid);const auto record=store.read(text_field(current,"document_id"));auto sequences=current.value("sequences",Json::array());
    const auto name=operation=="list"?std::string{}:operation=="save"?sequence_name(arguments.at("sequence")):sequence_name(arguments);auto found=std::find_if(sequences.begin(),sequences.end(),[&](const Json& value){return value.at("name")==name;});
    if(operation=="save") {
      if(current.at("display").value("draft",false))throw Error("stale_selection","Reset the preview before saving a sequence definition");
      auto saved=validate_sequence(arguments.at("sequence"),current,record);saved["source"]=sequence_source(record,current.at("display"));
      if(found==sequences.end()){if(sequences.size()>=16)throw Error("limit_exceeded","A view stores at most sixteen sequences");sequences.push_back(std::move(saved));}else *found=std::move(saved);
      if(current.contains("playback")&&current.at("playback").at("name")==name)current.erase("playback");
    } else if(operation!="list") {
      if(found==sequences.end())throw Error("not_found","Sequence is absent",{{"name",name}});
      if(operation=="delete"){sequences.erase(found);if(current.contains("playback")&&current.at("playback").at("name")==name)current.erase("playback");}
      else {
        if(!sequence_current(found->at("source"),record,current.at("display")))throw Error("stale_selection","Sequence belongs to another committed source revision");
        auto playback=current.value("playback",Json{{"name",name},{"time_s",0},{"speed",1},{"loop",false},{"source",found->at("source")},{"applied",false}});
        if(playback.at("name")!=name)playback={{"name",name},{"time_s",0},{"speed",1},{"loop",false},{"source",found->at("source")},{"applied",false}};
        if(operation=="options") {
          if(arguments.contains("speed")){const auto speed=number(arguments.at("speed"));if(speed<0.1||speed>4)throw Error("invalid_argument","Playback speed must be between 0.1 and 4");playback["speed"]=speed;}
          if(arguments.contains("loop")){if(!arguments.at("loop").is_boolean())throw Error("invalid_argument","Playback looping must be boolean");playback["loop"]=arguments.at("loop");}
        } else if(operation=="seek") {
          const auto time=number(arguments.at("time_s"));const auto sample=sequence_sample(*found,time),ops=sequence_operations(sample.at("joints"),record.at("model"),text_field(current.at("display"),"feature_id"));
          current["presentation"]=validate_presentation(sample.at("presentation"),current);playback["time_s"]=time;playback["applied"]=true;
          if(!ops.empty()) {
            current["motion"]={{"id","motion_"+sha256(nonce()).substr(0,48)},{"revision",record.at("revision")},{"assembly_id",current.at("display").at("feature_id")},{"target_assembly_id",sample.at("joints")[0].at("assembly_id")},{"source_evaluation_id",found->at("source").at("evaluation_id")},{"operations",ops}};
            current["generation"]=nonce();current.erase("pending");current.erase("failure");current.erase("measurement");evaluate=true;
          }
          if(current.contains("section")&&(evaluate||!section_matches(current.at("section").at("query"),current.at("presentation")))){retired_section=current.at("section").at("job_id");current.erase("section");}
          if(!current.at("context").is_null()){current["context"]["selection"]=nullptr;current["context"].erase("resolved_selection");}
        } else throw Error("invalid_argument","Unknown sequence operation");
        current["playback"]=std::move(playback);
      }
    }
    std::sort(sequences.begin(),sequences.end(),[](const Json& a,const Json& b){return a.at("name").get<std::string>()<b.at("name").get<std::string>();});current["sequences"]=std::move(sequences);if(operation!="list")save_state(path,current);state=std::move(current);
  }
  if(retired_section)cancel_view_job(service,*retired_section);
  if(evaluate)return synchronize(store,{{"view_id",id}});
  if(state.at("context").is_null()||state.at("context").at("evaluation_id")!=eid)state["context"]={{"revision",state.at("display").at("revision")},{"evaluation_id",eid},{"feature_id",state.at("display").at("feature_id")},{"selection",nullptr}};
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
    const auto record=store.read(document);const auto displayed=text_field(state.at("display"),"feature_id");
    const auto& model=record.at("model");
    const auto mechanisms=assembly_mechanisms(model,displayed);
    if (mechanisms.empty()) throw Error("invalid_argument","The displayed assembly has no moving coordinates");
    const auto default_target=action=="motion_save" && state.contains("motion") ? state.at("motion").value("target_assembly_id",displayed) : displayed;
    const auto assembly_id=arguments.contains("assembly_id")?text_field(arguments,"assembly_id"):default_target;
    model_identifier(assembly_id);
    const Json* scope=nullptr;
    for (const auto& item:mechanisms) if (item.at("assembly_id")==assembly_id) scope=&item;
    if (action!="motion_reset" && (action!="motion_save" || arguments.contains("pose_id")) && !scope)
      throw Error("invalid_argument","Choose a moving assembly definition reachable from this view",{{"assembly_id",assembly_id}});
    if (action=="motion_reset") {
      state.erase("motion");
    } else if (action=="motion_save") {
      if (!state.contains("motion") || !state.at("display").value("draft",false))
        throw Error("invalid_argument","Preview a pose before saving it");
      auto operations=state.at("motion").at("operations");
      if (arguments.contains("pose_id")) {
        const auto name=text_field(arguments,"pose_id");model_identifier(name);Json values=Json::array();
        const auto candidate=apply_operations(model,operations);
        Json saved_motion;
        for (const auto& feature:candidate.at("features")) if (feature.at("id")==assembly_id) saved_motion=assembly_motion(feature,candidate.at("parameters"));
        for (const auto& dof:saved_motion.at("dofs"))
          if (!dof.at("driven").get<bool>()) values.push_back({{"mate_id",dof.at("mate_id")},{"coordinate",dof.at("coordinate")},{"value",dof.at("value")}});
        operations.push_back({{"op","set_pose"},{"assembly_id",assembly_id},{"pose",{{"id",name},{"values",values}}}});
      }
      apply_operations(model,operations);
      state["motion"]["saving"]=true;state["motion"]["save_operations"]=std::move(operations);
    } else {
      Json operations=Json::array();
      if (state.contains("motion")) for (const auto& operation:state.at("motion").at("operations"))
        if (operation.at("assembly_id")!=assembly_id) operations.push_back(operation);
      if (arguments.contains("pose_id")) {
        const auto pose=text_field(arguments,"pose_id");model_identifier(pose);
        operations.push_back({{"op","apply_pose"},{"assembly_id",assembly_id},{"pose_id",pose}});
      } else {
        const auto& values=arguments.at("values");
        if (!values.is_array() || values.empty() || values.size()>126) throw Error("invalid_argument","Supply every independent motion coordinate, at most 126 values");
        std::set<std::string> expected,seen;
        for (const auto& dof:scope->at("motion").at("dofs")) if (!dof.at("driven").get<bool>()) expected.insert(text_field(dof,"mate_id")+"."+text_field(dof,"coordinate"));
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
        {"assembly_id",displayed},{"target_assembly_id",assembly_id},{"source_evaluation_id",eid},{"operations",std::move(operations)}};
    }
    // The generation makes any in-flight transfer/build from the former pose
    // unable to publish, even when both poses share the same saved revision.
    state.erase("playback");state["generation"]=nonce();state.erase("pending");state.erase("failure");
    if (!state.at("context").is_null()) {
      state["context"]["selection"]=nullptr;state["context"].erase("resolved_selection");
    }
    save_state(path,state);
  }
  return synchronize(store,{{"view_id",id}});
}
Json artifact_status(const Json& state,const Json& arguments) {
  const auto& display=state.at("display");
  Json result={{"view_id",state.at("view_id")},{"document_id",nullptr},{"revision",nullptr},{"feature_id",nullptr},
    {"state","ready"},{"changed",arguments.value("known_evaluation_id",std::string{})!=text_field(display,"evaluation_id")},
    {"evaluation_id",display.at("evaluation_id")},{"summary",state.at("artifact").at("summary")},{"artifact",state.at("artifact")},
    {"read_only",true},{"draft",false},{"saving",false},{"hidden_part_ids",Json::array()},
    {"presentation",state.value("presentation",default_presentation())},{"appearance",state.value("appearance",default_appearance())},{"presets",Json::array()},{"annotations",Json::array()},{"sequences",Json::array()},{"playback",nullptr}};
  if(!state.at("context").is_null()&&state.at("context").contains("camera"))result["camera"]=state.at("context").at("camera");
  return result;
}
Json artifact_context(Store&,const Json& state) {
  Json result={{"view_id",state.at("view_id")},{"document_id",nullptr},{"revision",nullptr},{"feature_id",nullptr},
    {"evaluation_id",state.at("display").at("evaluation_id")},{"head_revision",nullptr},{"stale",false},{"selection",nullptr},
    {"read_only",true},{"artifact",state.at("artifact")},{"hidden_part_ids",Json::array()},
    {"presentation",state.value("presentation",default_presentation())},{"appearance",state.value("appearance",default_appearance())},{"presets",Json::array()},{"annotations",Json::array()},{"sequences",Json::array()},{"playback",nullptr}};
  if(!state.at("context").is_null())result.update(state.at("context"));return result;
}
void artifact_display(const Json& state,const std::string& eid) {
  if(!state.value("read_only",false)||!state.at("document_id").is_null()||!state.contains("artifact")||!state.contains("display")||state.at("display").at("evaluation_id")!=eid)
    throw Error("stale_selection","Artifact review is no longer displayed");
}
Json show_artifact(Store& store,const Json& arguments) {
  fields(arguments,{"review_path","expected_sha256"},{"view_id"});const auto id=view_id(arguments),hash=text_field(arguments,"expected_sha256");
  const auto review_path=path_from_utf8(text_field(arguments,"review_path"));
  if(!review_path.is_absolute()||review_path.filename()!="review.json")throw Error("invalid_argument","Artifact show requires an absolute review.json");
  const auto verified=verify_external_artifact(review_path.parent_path(),hash);const auto raw=read_text(review_path,max_view_bytes);
  if(sha256(raw)!=hash)throw Error("artifact_mismatch","Review changed after verification");const auto review=parse_json(raw,max_view_bytes);
  if(review.at("schema_version")!=1||review.at("read_only")!=true||review.at("native_selection_references")!=false||review.at("editable_history_recovered")!=false)
    throw Error("invalid_argument","Unsupported external review contract");
  std::unique_ptr<DocumentLock> source_lock;
  const auto& association=review.at("source").at("native_source_association");
  if(!association.is_null()) {
    const auto& reference=association.at("reference");
    source_lock=std::make_unique<DocumentLock>(store.root(),text_field(reference,"document_id"),LockWait::publication);
    const auto record=store.read(text_field(reference,"document_id"),revision_number(reference.at("revision")));
    bool found=false;for(const auto& feature:record.at("model").at("features"))if(feature.at("id")==reference.at("feature_id"))found=true;
    if(!found||sha256(record.dump())!=text_field(association,"source_record_sha256"))throw Error("artifact_source_mismatch","Declared historical native source differs before viewer publication");
  }
  const auto eid="artifact_"+hash.substr(0,55);Json artifact={{"review_sha256",hash},{"source",verified.at("source")},{"summary",verified.at("summary")}};
  Json evaluation={{"schema_version",1},{"kind","artifact"},{"document_id",nullptr},{"revision",nullptr},{"feature_id",nullptr},{"evaluation_id",eid},{"draft",false},
    {"read_only",true},{"artifact",artifact},{"summary",review.at("summary")},{"artifact_geometry",review.at("geometry")},{"metadata",review.at("metadata")},
    {"limitations",review.at("limitations")},{"selection_lifetime","review_sha256"}};
  const auto path=view_path(store.root(),id,true);WorkspaceLock lock(path);auto state=empty_state(id);
  state["read_only"]=true;state["artifact"]=artifact;state["display"]={{"evaluation_id",eid},{"build",AGENTCAD_CACHE_BUILD}};
  directory(path/"evaluations");write_frozen(path/"evaluations"/(eid+".json"),evaluation);save_state(path,state);prune_frozen(path,eid);
  return {{"view_id",id},{"document_id",nullptr},{"read_only",true},{"artifact",artifact},{"resource_uri",viewer_app_uri}};
}
Json artifact_publish_context(Store& store,const Json& arguments) {
  const auto id=view_id(arguments),eid=text_field(arguments,"evaluation_id");identifier(eid);auto state=snapshot(store.root(),id);artifact_display(state,eid);
  Json context={{"evaluation_id",eid},{"selection",arguments.at("selection")}};
  if(!state.at("context").is_null())for(const auto* key:{"camera","prompt"})if(state.at("context").contains(key))context[key]=state.at("context").at(key);
  if(!arguments.at("selection").is_null()) {
    const auto& selection=arguments.at("selection");fields(selection,{"review_sha256","kind","entity_id"});
    if(selection.at("review_sha256")!=state.at("artifact").at("review_sha256"))throw Error("stale_selection","Artifact pick belongs to a different review");
    const auto kind=text_field(selection,"kind"),entity=text_field(selection,"entity_id");if(kind!="mesh_group"&&kind!="curve")throw Error("invalid_argument","Artifact selection must be a mesh group or curve");
    const auto payload=parse_json(read_text(view_path(store.root(),id)/"evaluations"/(eid+".json"),max_view_bytes),max_view_bytes);
    bool found=false;for(auto geometry:payload.at("artifact_geometry").at(kind=="mesh_group"?"groups":"polylines"))if(geometry.at("id")==entity){
      if(geometry.contains("points")){geometry["point_count"]=geometry.at("points").size();geometry.erase("points");}
      context["resolved_selection"]={{"reference",selection},{"geometry",geometry}};found=true;break;}
    if(!found)throw Error("selection_missing","Artifact label is absent from the displayed review");
  }
  if(arguments.contains("camera"))context["camera"]=validate_camera(arguments.at("camera"));
  if(arguments.contains("prompt")){const auto prompt=text_field(arguments,"prompt");if(prompt.size()>8192)throw Error("limit_exceeded","Viewer prompt exceeds 8192 UTF-8 bytes");context["prompt"]=prompt;}
  context["updated_at_unix_ms"]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  const auto path=view_path(store.root(),id);WorkspaceLock lock(path);auto current=read_state(path);if(!same_view(current,state))throw Error("stale_selection","View changed before artifact context could be saved");artifact_display(current,eid);
  if(arguments.contains("hidden_part_ids")&&!arguments.at("hidden_part_ids").empty())throw Error("invalid_argument","Artifact labels are not native assembly occurrences");
  current["presentation"]=validate_presentation(arguments.value("presentation",current.value("presentation",default_presentation())),current);
  current["appearance"]=validate_appearance(arguments.value("appearance",current.value("appearance",default_appearance())),current);current["context"]=context;save_state(path,current);return artifact_context(store,current);
}
}

Json live_call(Service& service, Store& store, const std::string& tool, const Json& arguments) {
  if(tool=="cad_artifact_show")return show_artifact(store,arguments);
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
  if(snapshot(store.root(),id).value("read_only",false)&&action!="sync"&&action!="mesh"&&action!="context")throw Error("read_only_artifact","This operation requires a committed editable native document");
  if (action == "context") {
    fields(arguments,{"action","view_id","evaluation_id","selection"},{"camera","prompt","hidden_part_ids","presentation","appearance"});
    if(snapshot(store.root(),id).value("read_only",false))return artifact_publish_context(store,arguments);
    return publish_context(service,store,arguments);
  }
  if(action=="annotation"){const auto operation=text_field(arguments,"operation");if(operation=="add")fields(arguments,{"action","view_id","evaluation_id","operation","text","anchor"});else if(operation=="update")fields(arguments,{"action","view_id","evaluation_id","operation","annotation_id","text"});else if(operation=="delete")fields(arguments,{"action","view_id","evaluation_id","operation","annotation_id"});else fields(arguments,{"action","view_id","evaluation_id","operation"});return annotation_action(service,store,arguments);}
  if(action=="sequence"){const auto op=text_field(arguments,"operation");if(op=="list")fields(arguments,{"action","view_id","evaluation_id","operation"});else if(op=="save")fields(arguments,{"action","view_id","evaluation_id","operation","sequence"});else if(op=="delete")fields(arguments,{"action","view_id","evaluation_id","operation","name"});else if(op=="seek")fields(arguments,{"action","view_id","evaluation_id","operation","name","time_s"});else if(op=="options")fields(arguments,{"action","view_id","evaluation_id","operation","name"},{"speed","loop"});else throw Error("invalid_argument","Unknown sequence operation");return sequence_action(service,store,arguments);}
  if(action=="preset"){const auto operation=text_field(arguments,"operation");if(operation=="list")fields(arguments,{"action","view_id","evaluation_id","operation"});else fields(arguments,{"action","view_id","evaluation_id","operation","name"});return preset_action(service,store,arguments);}
  if(action=="measure"){fields(arguments,{"action","view_id","evaluation_id"},{"query"});return measure_view(service,store,arguments);}
  if(action=="section"){fields(arguments,{"action","view_id","evaluation_id"},{"query"});return measure_view(service,store,arguments,"section");}
  if (action=="motion_preview") {
    fields(arguments,{"action","view_id","evaluation_id"},{"values","pose_id","assembly_id"});
    if (arguments.contains("values")==arguments.contains("pose_id")) throw Error("invalid_argument","Preview requires either values or pose_id");
    return motion_action(store,arguments);
  }
  if (action=="motion_reset" || action=="motion_save") {
    if (action=="motion_save") fields(arguments,{"action","view_id","evaluation_id"},{"pose_id","assembly_id"});
    else fields(arguments,{"action","view_id","evaluation_id"});
    return motion_action(store,arguments);
  }
  throw Error("invalid_argument","Unknown viewer action");
}

Json live_tool_definitions() {
  auto definitions = model_definitions();definitions.update(measurement_definitions());definitions.update(artifact_review_definitions());
  const Json id = {{"type","string"},{"pattern","^[A-Za-z][A-Za-z0-9_-]{0,63}$"}};
  const Json revision = {{"type","integer"},{"minimum",1},{"maximum",9007199254740991ULL}};
  definitions["revision"]=revision;
  const auto nullable = [](Json schema){return Json{{"anyOf",Json::array({schema,Json{{"type","null"}}})}};};
  const Json numeric = {{"type","number"},{"minimum",-1e6},{"maximum",1e6}};
  const Json pick = object({{"document_id",id},{"revision",revision},{"evaluation_id",id},{"feature_id",id},
    {"kind",{{"enum",{"face","edge"}}}},{"entity_id",id}}, {"document_id","revision","evaluation_id","feature_id","kind","entity_id"});
  const Json artifact_pick=object({{"review_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},{"kind",{{"enum",{"mesh_group","curve"}}}},{"entity_id",{{"type","string"},{"pattern","^(artifact|curve)-[1-9][0-9]*$"}}}},{"review_sha256","kind","entity_id"});
  const Json artifact=object({{"review_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}},{"source",{{"$ref","#/$defs/artifact_review_source"}}},{"summary",{{"$ref","#/$defs/artifact_review_summary"}}}},{"review_sha256","source","summary"});
  const Json selection=nullable({{"oneOf",Json::array({pick,artifact_pick})}});
  const Json camera = object({{"yaw",numeric},{"pitch",numeric},{"zoom",{{"type","number"},{"minimum",0.01},{"maximum",1000}}},
    {"pan",{{"type","array"},{"items",numeric},{"minItems",2},{"maxItems",2}}}}, {"yaw","pitch","zoom","pan"});
  const Json prompt = {{"type","string"},{"maxLength",8192}};
  const Json hidden_parts={{"type","array"},{"items",occurrence_schema()},{"maxItems",assembly_leaf_limit},{"uniqueItems",true}};
  const Json unit_vector={{"type","array"},{"items",{{"type","number"},{"minimum",-1},{"maximum",1}}},{"minItems",3},{"maxItems",3}};
  const Json presentation=object({{"clip",nullable(object({{"normal",unit_vector},{"offset_mm",{{"type","number"},{"minimum",-1e12},{"maximum",1e12}}},
    {"keep",{{"enum",{"positive","negative"}}}}},{"normal","offset_mm","keep"}))},
    {"explode",object({{"distance_mm",{{"type","number"},{"minimum",0},{"maximum",1e6}}},
      {"directions",{{"type","array"},{"maxItems",assembly_leaf_limit},{"items",object({{"part_id",occurrence_schema()},{"direction",unit_vector}},{"part_id","direction"})}}}},
      {"distance_mm","directions"})}},{"clip","explode"});
  const Json rgb={{"type","array"},{"items",{{"type","number"},{"minimum",0},{"maximum",1}}},{"minItems",3},{"maxItems",3}};
  const Json appearance=object({{"default_color",rgb},{"parts",{{"type","array"},{"maxItems",assembly_leaf_limit},{"items",object({{"part_id",occurrence_schema()},{"color",rgb}},{"part_id","color"})}}}},{"default_color","parts"});
  const Json preset_name={{"type","string"},{"minLength",1},{"maxLength",64}};
  const Json presets={{"type","array"},{"maxItems",16},{"items",object({{"name",preset_name},{"camera",camera},{"presentation",presentation},{"hidden_part_ids",hidden_parts},{"appearance",appearance}},
    {"name","camera","presentation","hidden_part_ids","appearance"})}};
  const Json note_text={{"type","string"},{"minLength",1},{"maxLength",512},{"pattern",R"(^[^\u0000-\u0008\u000b-\u001f\u007f]+$)"}};
  const Json anchor_request={{"oneOf",Json::array({object({{"kind",{{"const","model"}}}},{"kind"}),
    object({{"kind",{{"const","part"}}},{"part_id",occurrence_schema()}},{"kind","part_id"}),
    object({{"kind",{{"const","entity"}}},{"reference",pick}},{"kind","reference"})})}};
  const Json anchor_point={{"type","array"},{"items",{{"type","number"},{"minimum",-1e12},{"maximum",1e12}}},{"minItems",3},{"maxItems",3}};
  auto stored_anchor=[&](const char* kind,const char* semantics,Json owner,bool reference){
    Json props={{"kind",{{"const",kind}}},{"part_id",owner},{"point_mm",anchor_point},{"position_semantics",{{"const",semantics}}}};
    Json required={"kind","part_id","point_mm","position_semantics"};if(reference){props["reference"]=pick;required.push_back("reference");}return object(props,required);
  };
  definitions["review_annotation"]=object({{"id",id},{"text",note_text},{"document_id",id},{"revision",revision},{"evaluation_id",id},{"feature_id",id},
    {"anchor_lifetime",{{"const","evaluation"}}},{"coordinate_space",{{"const","committed_source_pose"}}},{"status",{{"enum",{"current","retired"}}}},
    {"anchor",{{"oneOf",Json::array({stored_anchor("model","bounds_center",{{"type","null"}},false),stored_anchor("part","bounds_center",occurrence_schema(),false),stored_anchor("entity","entity_center",nullable(occurrence_schema()),true)})}}}},
    {"id","text","document_id","revision","evaluation_id","feature_id","anchor_lifetime","coordinate_space","status","anchor"});
  const Json annotations={{"type","array"},{"maxItems",32},{"items",{{"$ref","#/$defs/review_annotation"}}}};
  // Preview context cannot carry arbitrary modeling edits. Keeping its two
  // actual operation shapes here also avoids dragging the entire feature
  // vocabulary into every standalone context schema.
  const Json operations={{"type","array"},{"minItems",1},{"maxItems",256},{"items",{{"oneOf",Json::array({
    object({{"op",{{"const","set_joint_value"}}},{"assembly_id",id},{"mate_id",id},
      {"coordinate",{{"enum",{"angle_deg","travel_mm"}}}},{"value",numeric}},{"op","assembly_id","mate_id","coordinate","value"}),
    object({{"op",{{"const","apply_pose"}}},{"assembly_id",id},{"pose_id",id}},{"op","assembly_id","pose_id"})})}}}};
  const Json values={{"type","array"},{"minItems",1},{"maxItems",126},{"items",object({{"mate_id",id},
    {"coordinate",{{"enum",{"angle_deg","travel_mm"}}}},{"value",numeric}},{"mate_id","coordinate","value"})}};
  const Json error = object({{"code",{{"type","string"}}},{"message",{{"type","string"}}},{"details",{{"type","object"}}}}, {"code","message","details"});
  const Json measurement=object({{"evaluation_id",id},{"job_id",id},{"query",{{"$ref","#/$defs/measurement_distance_query"}}}},{"evaluation_id","job_id","query"});
  const Json section=object({{"evaluation_id",id},{"job_id",id},{"query",{{"$ref","#/$defs/section_query"}}}},{"evaluation_id","job_id","query"});
  const Json measure_status=object({{"view_id",id},{"evaluation_id",id},{"job_id",id},{"query",{{"$ref","#/$defs/measurement_query"}}},
    {"state",{{"enum",{"empty","queued","running","cancelling","succeeded","failed","cancelled","interrupted"}}}},
    {"result",{{"$ref","#/$defs/measurement_result"}}},{"error",error}},{"view_id","evaluation_id","state"});
  const Json sequence_source_schema=object({{"document_id",id},{"revision",revision},{"evaluation_id",id},{"feature_id",id},{"model_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}}}, {"document_id","revision","evaluation_id","feature_id","model_sha256"});
  const Json frame=object({{"time_s",{{"type","number"},{"minimum",0},{"maximum",3600}}},{"presentation",presentation},{"joints",{{"type","array"},{"maxItems",16},{"items",object({{"assembly_id",id},{"values",values}},{"assembly_id","values"})}}}}, {"time_s","presentation","joints"});
  const Json sequence=object({{"name",preset_name},{"frames",{{"type","array"},{"minItems",2},{"maxItems",64},{"items",frame}}}}, {"name","frames"});
  auto saved_sequence=sequence;saved_sequence["properties"]["source"]=sequence_source_schema;saved_sequence["required"].push_back("source");
  const Json sequences={{"type","array"},{"maxItems",16},{"items",saved_sequence}};
  const Json playback=nullable(object({{"name",preset_name},{"time_s",{{"type","number"},{"minimum",0},{"maximum",3600}}},{"speed",{{"type","number"},{"minimum",0.1},{"maximum",4}}},{"loop",{{"type","boolean"}}},{"source",sequence_source_schema},{"state",{{"enum",{"unapplied","pending","displayed"}}}}}, {"name","time_s","speed","loop","source","state"}));
  const Json edit_selector={{"oneOf",Json::array({Json{{"$ref","#/$defs/selector"}},Json{{"$ref","#/$defs/face_selector"}}})}};
  Json context = object({{"view_id",id},{"document_id",nullable(id)},{"revision",nullable(revision)},{"evaluation_id",nullable(id)},{"feature_id",nullable(id)},
    {"head_revision",nullable(revision)},{"stale",{{"type","boolean"}}},{"selection",selection},
    {"resolved_selection",object({{"reference",pick},{"geometry",{{"oneOf",Json::array({Json{{"$ref","#/$defs/face"}},Json{{"$ref","#/$defs/edge"}}})}}},
      {"selector",edit_selector}}, {"reference","geometry"})},
    {"camera",camera},{"prompt",prompt},{"hidden_part_ids",hidden_parts},{"presentation",presentation},{"appearance",appearance},{"presets",presets},{"annotations",annotations},{"sequences",sequences},{"playback",playback},{"measurement",measurement},{"section",section},{"updated_at_unix_ms",{{"type","integer"}}},
    {"draft",{{"type","boolean"}}},{"preview_operations",operations}},
    {"view_id","document_id","revision","evaluation_id","feature_id","head_revision","stale","selection","hidden_part_ids","presentation","appearance","presets","annotations"});
  context["properties"]["read_only"]={{"const",true}};context["properties"]["artifact"]=artifact;
  const auto native_resolved=context["properties"]["resolved_selection"];context["properties"]["resolved_selection"]={{"oneOf",Json::array({native_resolved,object({{"reference",artifact_pick},{"geometry",{{"type","object"}}}},{"reference","geometry"})})}};
  Json identity = object({{"view_id",id},{"document_id",nullable(id)},{"resource_uri",{{"const",viewer_app_uri}}}}, {"view_id","document_id","resource_uri"});
  identity["properties"]["read_only"]={{"const",true}};identity["properties"]["artifact"]=artifact;
  const Json point = {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3}};
  const Json summary = object({{"valid",{{"const",true}}},{"units",{{"const","mm"}}},{"volume_mm3",{{"type","number"}}},{"area_mm2",{{"type","number"}}},
    {"center_of_mass_mm",point},{"bounds_mm",object({{"min",point},{"max",point}},{"min","max"})},
    {"solid_count",{{"type","integer"},{"minimum",0}}},{"face_count",{{"type","integer"},{"minimum",0}}},{"edge_count",{{"type","integer"},{"minimum",0}}},
    {"assembly",{{"$ref","#/$defs/assembly_summary"}}},
    {"sheet_metal",{{"$ref","#/$defs/sheet_metal_report"}}},
    {"components",{{"type","array"},{"maxItems",64},{"items",{{"$ref","#/$defs/component_status"}}}}}},
    {"valid","units","volume_mm3","area_mm2","center_of_mass_mm","bounds_mm","solid_count","face_count","edge_count"});
  Json sync = object({{"view_id",id},{"document_id",nullable(id)},{"revision",nullable(revision)},
    {"state",{{"enum",{"empty","loading","ready","error"}}}},{"changed",{{"type","boolean"}}},{"evaluation_id",id},{"feature_id",id},
    {"summary",summary},{"model",{{"$ref","#/$defs/model"}}},{"error",error},{"camera",camera},{"hidden_part_ids",hidden_parts},{"presentation",presentation},{"appearance",appearance},{"presets",presets},{"annotations",annotations},{"sequences",sequences},{"playback",playback},{"measurement",measurement},{"section",section},
    {"draft",{{"type","boolean"}}},{"saving",{{"type","boolean"}}},{"preview_operations",operations}}, {"view_id","document_id","revision","state","changed"});
  sync["properties"]["read_only"]={{"const",true}};sync["properties"]["artifact"]=artifact;sync["properties"]["feature_id"]=nullable(id);sync["properties"]["summary"]={{"oneOf",Json::array({summary,Json{{"$ref","#/$defs/artifact_review_summary"}}})}};
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
  auto viewer = tool("cad_viewer","App-only view synchronization, frozen mesh chunks, assembly visibility and validated context. Motion controls preview native poses, reset, or explicitly save through atomic revision-checked edits. Declarative sequences sample joints/presentation without source edits.",
    {{"type","object"},{"oneOf",Json::array({
      object({{"action",{{"const","sync"}}},{"view_id",id},{"known_evaluation_id",id}},{"action","view_id"}),
      object({{"action",{{"const","mesh"}}},{"view_id",id},{"evaluation_id",id},{"offset",offset}},{"action","view_id","evaluation_id"}),
      object({{"action",{{"const","measure"}}},{"view_id",id},{"evaluation_id",id},{"query",nullable({{"$ref","#/$defs/measurement_distance_query"}})}},{"action","view_id","evaluation_id"}),
      object({{"action",{{"const","section"}}},{"view_id",id},{"evaluation_id",id},{"query",nullable({{"$ref","#/$defs/section_query"}})}},{"action","view_id","evaluation_id"}),
      object({{"action",{{"const","motion_preview"}}},{"view_id",id},{"evaluation_id",id},{"assembly_id",id},{"values",values}},{"action","view_id","evaluation_id","values"}),
      object({{"action",{{"const","motion_preview"}}},{"view_id",id},{"evaluation_id",id},{"assembly_id",id},{"pose_id",id}},{"action","view_id","evaluation_id","pose_id"}),
      object({{"action",{{"const","motion_reset"}}},{"view_id",id},{"evaluation_id",id}},{"action","view_id","evaluation_id"}),
      object({{"action",{{"const","motion_save"}}},{"view_id",id},{"evaluation_id",id},{"assembly_id",id},{"pose_id",id}},{"action","view_id","evaluation_id"}),
      object({{"action",{{"const","annotation"}}},{"view_id",id},{"evaluation_id",id},{"operation",{{"enum",{"list","clear"}}}}},{"action","view_id","evaluation_id","operation"}),
      object({{"action",{{"const","annotation"}}},{"view_id",id},{"evaluation_id",id},{"operation",{{"const","add"}}},{"text",note_text},{"anchor",anchor_request}},{"action","view_id","evaluation_id","operation","text","anchor"}),
      object({{"action",{{"const","annotation"}}},{"view_id",id},{"evaluation_id",id},{"operation",{{"const","update"}}},{"annotation_id",id},{"text",note_text}},{"action","view_id","evaluation_id","operation","annotation_id","text"}),
      object({{"action",{{"const","annotation"}}},{"view_id",id},{"evaluation_id",id},{"operation",{{"const","delete"}}},{"annotation_id",id}},{"action","view_id","evaluation_id","operation","annotation_id"}),
      object({{"action",{{"const","sequence"}}},{"view_id",id},{"evaluation_id",id},{"operation",{{"const","list"}}}}, {"action","view_id","evaluation_id","operation"}),
      object({{"action",{{"const","sequence"}}},{"view_id",id},{"evaluation_id",id},{"operation",{{"const","save"}}},{"sequence",sequence}}, {"action","view_id","evaluation_id","operation","sequence"}),
      object({{"action",{{"const","sequence"}}},{"view_id",id},{"evaluation_id",id},{"operation",{{"const","delete"}}},{"name",preset_name}}, {"action","view_id","evaluation_id","operation","name"}),
      object({{"action",{{"const","sequence"}}},{"view_id",id},{"evaluation_id",id},{"operation",{{"const","seek"}}},{"name",preset_name},{"time_s",{{"type","number"},{"minimum",0},{"maximum",3600}}}}, {"action","view_id","evaluation_id","operation","name","time_s"}),
      object({{"action",{{"const","sequence"}}},{"view_id",id},{"evaluation_id",id},{"operation",{{"const","options"}}},{"name",preset_name},{"speed",{{"type","number"},{"minimum",0.1},{"maximum",4}}},{"loop",{{"type","boolean"}}}}, {"action","view_id","evaluation_id","operation","name"}),
      object({{"action",{{"const","preset"}}},{"view_id",id},{"evaluation_id",id},{"operation",{{"const","list"}}}},{"action","view_id","evaluation_id","operation"}),
      object({{"action",{{"const","preset"}}},{"view_id",id},{"evaluation_id",id},{"operation",{{"enum",{"save","apply","delete"}}}},{"name",preset_name}},{"action","view_id","evaluation_id","operation","name"}),
      object({{"action",{{"const","context"}}},{"view_id",id},{"evaluation_id",id},{"selection",selection},{"camera",camera},{"prompt",prompt},{"hidden_part_ids",hidden_parts},{"presentation",presentation},{"appearance",appearance}},
        {"action","view_id","evaluation_id","selection"})})}},
    {{"type","object"},{"oneOf",Json::array({sync,chunk,context,measure_status})}},false);
  viewer["_meta"] = {{"ui",{{"visibility",Json::array({"app"})}}}};
  auto artifact_show=tool("cad_artifact_show","Open a verified portable external review in the live viewer. Original artifact units/hashes and review-local mesh/curve labels remain explicit; native editing, measuring, notes and playback are unavailable.",
    object({{"view_id",id},{"review_path",{{"type","string"}}},{"expected_sha256",{{"type","string"},{"pattern","^[a-f0-9]{64}$"}}}},{"review_path","expected_sha256"}),identity,false);
  artifact_show["_meta"]=open["_meta"];
  return Json::array({open,artifact_show,
    tool("cad_show","Display a committed document in an existing or new workspace view. Showing a failed view again retries its evaluation.",
      object({{"view_id",id},{"document_id",id}},{"document_id"}),identity,false),
    tool("cad_list","List at most 1000 saved documents and committed HEAD revisions.",object(Json::object(),Json::array()),
      object({{"documents",{{"type","array"},{"maxItems",document_limit},{"items",object({{"document_id",id},{"revision",revision}},{"document_id","revision"})}}},
        {"truncated",{{"type","boolean"}}}}, {"documents","truncated"}),true),
    tool("cad_context","Read validated viewer selection, camera, prompt and current hidden assembly part IDs. stale reports a changed HEAD or displayed evaluation.",
      object({{"view_id",id}},Json::array()),context,true),viewer});
}
}
