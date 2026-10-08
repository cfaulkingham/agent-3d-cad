#include "agentcad/printer.hpp"
#include "agentcad/gcode.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/service.hpp"
#include <functional>
#include <chrono>
#include <iostream>
#include <set>
#include <thread>

using namespace agentcad;
namespace {
int checks = 0;
Json contract_arguments = Json::array(), contract_results = Json::array();
void require(bool value, const std::string& message) { ++checks; if (!value) throw std::runtime_error(message); }
void fails(const std::string& code, const std::function<void()>& action) {
  try { action(); throw std::runtime_error("Expected " + code); }
  catch (const Error& error) { require(error.code == code, "Expected " + code + ", got " + error.code + ": " + error.what()); }
}
struct Temp {
  fs::path root;
  Temp() : root(temporary_directory(fs::temp_directory_path())) {}
  ~Temp() { std::error_code ignored; fs::remove_all(root, ignored); }
};
Json review_options() {
  return {{"firmware", "marlin"}, {"machine", {{"name", "Analytical fixture"}, {"motion_bounds_mm", {{-20,20},{-20,20},{0,100}}}}},
    {"material", {{"name", "Analytical PLA range"}, {"nozzle_temperature_c", {190,230}}, {"bed_temperature_c", {50,70}}}},
    {"initial", {{"units", "mm"}, {"xyz_mode", "absolute"}, {"extrusion_mode", "absolute"}, {"position_mm", {0,0,0}}, {"extruder_mm", 0}}}};
}
const std::string good = "; UTF-8 original café\r\nG21\nG90\nM83\nM104S210\nM140S60\nG1X10Y5Z1E1\n";
const std::string build = sha256("standalone native printer module") + "-Release";
Json file(const fs::path& path, const std::string& raw) {
  atomic_text(path, raw, gcode_bytes_limit); return {{"path", path_to_utf8(path)}, {"expected_sha256", sha256(raw)}};
}
Json fixture(const fs::path& root, const std::string& raw = good) {
  const auto artifact = file(root/"actual job.gcode", raw);
  Json profiles = Json::object();
  for (const auto* role : {"machine", "process", "filament"}) {
    const auto body = Json{{"type", role}, {"name", std::string("Explicit fixture ")+role}, {"post_process", Json::array()}}.dump(2)+"\n";
    profiles[role] = file(root/(std::string(role)+".json"), body);
  }
  return {{"document_id", "part"}, {"revision", 1}, {"action", "plan"}, {"path", artifact.at("path")}, {"expected_sha256", artifact.at("expected_sha256")},
    {"options", {{"printer", {{"backend", "manual"}, {"id", "fixture_printer"}, {"model", "Explicit fixture"}, {"nozzle_diameter_mm", .4}, {"bed_type", "Assessed fixture plate"}, {"handoff", "plain_gcode"}}},
      {"profiles", profiles}, {"review", review_options()}}}};
}
Json verify_args(const Json& result) {
  return {{"document_id", result.at("document_id")}, {"revision", result.at("revision")}, {"action", "verify"},
    {"plan_path", result.at("path")}, {"expected_sha256", result.at("sha256")}};
}
std::set<std::string> files(const fs::path& root) {
  std::set<std::string> result; for (const auto& entry : fs::directory_iterator(root/"exports")) result.insert(path_to_utf8(entry.path().filename())); return result;
}
Json model() {
  return {{"schema_version", 1}, {"units", "mm"}, {"parameters", Json::object()},
    {"features", Json::array({{{"id", "body"}, {"type", "box"}, {"size", {10,10,2}}}})}, {"output", "body"}};
}
Json source(const fs::path& workspace) {
  Service service(workspace); service.call("cad_create", {{"document_id", "part"}, {"model", model()}}); return service.call("cad_read", {{"document_id", "part"}, {"revision", 1}});
}
void validation() {
  Temp temp; auto args = fixture(temp.root); validate_printer_arguments(args);
  for (const auto* action : {"upload", "start", "run", "upload-start"}) {
    auto bad = args; bad["action"] = action; fails("invalid_argument", [&] { validate_printer_arguments(bad); });
  }
  auto bad = args; bad["execute"] = true; fails("invalid_argument", [&] { validate_printer_arguments(bad); });
  bad = args; bad["expected_sha256"] = std::string(64, 'A'); fails("invalid_argument", [&] { validate_printer_arguments(bad); });
  bad = args; bad["path"] = "relative.gcode"; fails("invalid_argument", [&] { validate_printer_arguments(bad); });
  bad = args; bad["path"] = path_to_utf8(temp.root/"job.gcode.3mf"); fails("invalid_argument", [&] { validate_printer_arguments(bad); });
  bad = args; bad["options"]["printer"]["access_code"] = "secret"; fails("invalid_argument", [&] { validate_printer_arguments(bad); });
  bad = args; bad["options"]["printer"]["host"] = "192.168.1.34"; fails("invalid_argument", [&] { validate_printer_arguments(bad); });
  bad = args; bad["options"]["printer"]["nozzle_diameter_mm"] = 0; fails("invalid_argument", [&] { validate_printer_arguments(bad); });
  bad = args; bad["options"]["printer"]["model"] = "printer\ncommand"; fails("invalid_argument", [&] { validate_printer_arguments(bad); });
  bad = args; bad["options"]["printer"]["id"] = "CON"; fails("invalid_argument", [&] { validate_printer_arguments(bad); });
  bad = args; bad["options"]["printer"]["backend"] = "guessed_driver"; fails("invalid_argument", [&] { validate_printer_arguments(bad); });
  bad = args; bad["options"]["printer"]["handoff"] = "template_project"; fails("invalid_argument", [&] { validate_printer_arguments(bad); });
  const auto definitions = printer_definitions(); require(definitions.contains("printer_arguments") && definitions.contains("printer_result") && definitions.contains("printer_options") && definitions.contains("printer_readiness"), "Published closed schemas cover both actions and results");
  require(definitions.at("printer_readiness").at("properties").at("status").at("enum") == Json::array({"fail", "unknown"}), "Readiness cannot claim a physical pass from an offline plan");
}
void packages() {
  Temp temp; const auto workspace = temp.root/"workspace"; const auto record = source(workspace); auto args = fixture(temp.root);
  const auto head = read_text(workspace/"documents/part/HEAD.json"); const auto result = plan_printer_handoff(workspace, record, args, build);
  contract_arguments.push_back(args); contract_results.push_back(result);
  const auto root = path_from_utf8(text_field(result, "directory")); const auto raw = read_text(root/"plan.json", 2*1024*1024); const auto plan = parse_json(raw, 2*1024*1024);
  require(result.at("report").at("status") == "pass" && result.at("readiness").at("status") == "unknown", "Measured supported static pass never becomes printer approval");
  require(result.at("hardware_contact") == false && result.at("physical_print_started") == false, "Plan reports no fabricated physical side effects");
  require(result.at("native_build") == build && plan.at("source").at("model_sha256") == sha256(record.at("model").dump()), "Source revision, actual model hash and native build qualify the plan");
  require(plan.at("source_association") == "caller_declared_not_geometry_verified", "G-code/source association is explicit and does not invent slicer causality");
  require(read_text(root/"original.gcode") == good && result.at("gcode_sha256") == sha256(good), "Portable package preserves exact G-code bytes and comments");
  require(sha256(raw) == result.at("sha256").get<std::string>(), "Returned plan hash is the actual serialized manifest hash");
  const auto manifest = parse_json(read_text(root/"manifest.json"));
  for (const auto& entry : manifest.at("artifacts")) {
    const auto bytes = read_text(root/path_from_utf8(text_field(entry, "path")), printer_package_bytes_limit);
    require(entry.at("bytes") == bytes.size() && entry.at("sha256") == sha256(bytes), "Independent package ledger hashes actual bytes");
  }
  require(parse_json(read_text(root/"source.json")) == record, "Complete editable committed source remains in the package");
  const auto verified = verify_printer_handoff(workspace, record, verify_args(result), build);
  contract_arguments.push_back(verify_args(result)); contract_results.push_back(verified);
  require(verified.at("report") == result.at("report") && verified.at("action") == "verify", "Reviewed-plan verification recomputes the real native findings");
  require(read_text(workspace/"documents/part/HEAD.json") == head, "Planning and verification preserve raw source HEAD");
  Service newer(workspace);
  newer.call("cad_apply", {{"document_id", "part"}, {"expected_revision", 1}, {"operations", Json::array({
    {{"op", "replace_feature"}, {"id", "body"}, {"feature", {{"id", "body"}, {"type", "box"}, {"size", {12,10,2}}}}}})}});
  const auto new_head = read_text(workspace/"documents/part/HEAD.json");
  require(verify_printer_handoff(workspace, record, verify_args(result), build).at("revision") == 1 &&
    read_text(workspace/"documents/part/HEAD.json") == new_head, "Historical source verification remains pinned after a newer revision without rolling HEAD back");
  for (const auto* role : {"machine", "process", "filament"}) fs::remove(path_from_utf8(text_field(args.at("options").at("profiles").at(role), "path")));
  fs::remove(path_from_utf8(text_field(args, "path"))); Temp moved; fs::copy(root, moved.root/"portable", fs::copy_options::recursive);
  auto relocated = verify_args(result); relocated["plan_path"] = path_to_utf8(moved.root/"portable/plan.json");
  require(verify_printer_handoff(workspace, record, relocated, build).at("gcode_sha256") == sha256(good), "Relocated package verifies after every original input is removed");
  const auto previous = files(workspace); auto wrong = relocated; wrong["expected_sha256"] = std::string(64, 'f');
  fails("artifact_mismatch", [&] { verify_printer_handoff(workspace, record, wrong, build); });
  fails("artifact_mismatch", [&] { verify_printer_handoff(workspace, record, relocated, build+"-changed"); });
  atomic_text(moved.root/"portable/original.gcode", good+"G1X999E1\n"); fails("artifact_mismatch", [&] { verify_printer_handoff(workspace, record, relocated, build); });
  require(files(workspace) == previous && read_text(workspace/"documents/part/HEAD.json") == new_head, "Verification failures publish nothing and preserve prior exports/HEAD");
  atomic_text(moved.root/"portable/original.gcode", good); atomic_text(moved.root/"portable/extra.txt", "unexpected");
  fails("artifact_mismatch", [&] { verify_printer_handoff(workspace, record, relocated, build); }); fs::remove(moved.root/"portable/extra.txt");
  auto changed_plan = plan; changed_plan["readiness"]["status"] = "pass";
  const auto forged = changed_plan.dump(2)+"\n"; atomic_text(moved.root/"portable/plan.json", forged, 2*1024*1024); relocated["expected_sha256"] = sha256(forged);
  auto forged_manifest = manifest; forged_manifest["artifacts"].back()["sha256"] = sha256(forged); forged_manifest["artifacts"].back()["bytes"] = forged.size(); atomic_text(moved.root/"portable/manifest.json", forged_manifest.dump(2)+"\n");
  fails("artifact_mismatch", [&] { verify_printer_handoff(workspace, record, relocated, build); });
  require(read_text(workspace/"documents/part/HEAD.json") == new_head, "Even rehashed fabricated readiness cannot replace native verification");
}
void failures_and_external() {
  Temp temp; const auto workspace = temp.root/"workspace"; const auto record = source(workspace); auto args = fixture(temp.root); const auto before = files(workspace);
  auto bad = args; bad["expected_sha256"] = std::string(64, '0'); fails("artifact_mismatch", [&] { plan_printer_handoff(workspace, record, bad, build); });
  require(files(workspace) == before, "Mismatched G-code publishes no partial package");
  bad = args; bad["feature_id"] = "missing"; fails("invalid_argument", [&] { plan_printer_handoff(workspace, record, bad, build); });
  const auto process_path = path_from_utf8(text_field(args.at("options").at("profiles").at("process"), "path"));
  bad = args; bad["options"]["profiles"]["process"] = file(process_path, Json{{"type","process"},{"name","Injected command"},{"post_process",Json::array({"touch /tmp/should-never-run"})}}.dump());
  fails("invalid_argument", [&] { plan_printer_handoff(workspace, record, bad, build); }); require(files(workspace) == before, "Rejected profile cleans its private stage");
  args = fixture(temp.root, good+"G1X999E1\n"); auto result = plan_printer_handoff(workspace, record, args, build);
  contract_arguments.push_back(args); contract_results.push_back(result);
  require(result.at("report").at("status") == "fail" && result.at("readiness").at("status") == "fail", "Out-of-bounds native findings remain failed in a prepared package");
  args = fixture(temp.root, good+"M117 operator message\n"); result = plan_printer_handoff(workspace, record, args, build);
  contract_arguments.push_back(args); contract_results.push_back(result);
  require(result.at("report").at("status") == "unknown" && result.at("readiness").at("status") == "unknown", "Unsupported firmware commands preserve explicit unknown coverage");
  args = fixture(temp.root); args["options"]["printer"]["backend"] = "bambu_lan"; args["options"]["printer"]["model"] = "A1 Mini";
  result = plan_printer_handoff(workspace, record, args, build); const auto plan = parse_json(read_text(path_from_utf8(text_field(result, "path")), 2*1024*1024), 2*1024*1024);
  contract_arguments.push_back(args); contract_results.push_back(result);
  require(plan.at("workflow").at("installation_status") == "not_checked" && !result.at("readiness").at("native_start_supported").get<bool>(), "Unavailable native backend is explicit; adapter installation is not fabricated");
  require(result.at("readiness").at("checks").back().at("reason").get<std::string>().find("A1 Mini") != std::string::npos, "Offline Bambu plan does not claim plain A1 Mini starts work");
  args["options"]["printer"]["handoff"] = "template_project";
  // Signature-only fixture deliberately is not a valid archive. The native plan
  // must state unknown format coverage rather than label it a validated project.
  auto input = file(temp.root/"same-printer.gcode.3mf", std::string("PK\x03\x04",4)+"signature-only fixture"); input["printer_model"] = "A1 Mini"; input["plate"] = 1;
  args["options"]["template_project"] = input; result = plan_printer_handoff(workspace, record, args, build);
  contract_arguments.push_back(args); contract_results.push_back(result);
  require(result.at("readiness").at("checks").back().at("status") == "unknown", "A template signature never claims archive, selected plate or firmware validation");
  require(fs::exists(path_from_utf8(text_field(result,"directory"))/"template.gcode.3mf"), "Exact template bytes join the portable hash ledger");
  require(verify_printer_handoff(workspace, record, verify_args(result), build).at("readiness") == result.at("readiness"), "Template review rechecks raw bytes without executing external code");
  bad = args; bad["options"]["template_project"]["printer_model"] = "Other printer"; fails("invalid_argument", [&] { validate_printer_arguments(bad); });
  bad = args; bad["options"]["template_project"]["plate"] = 0; fails("invalid_argument", [&] { validate_printer_arguments(bad); });
  bad = args; auto nozip = file(temp.root/"not-zip.gcode.3mf", "text is not a template"); nozip["printer_model"] = "A1 Mini"; nozip["plate"] = 1; bad["options"]["template_project"] = nozip;
  fails("invalid_argument", [&] { plan_printer_handoff(workspace, record, bad, build); });
}
Json read_job(Service& service, const std::string& id) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  for (;;) {
    try { return service.call("cad_job", {{"action", "get"}, {"job_id", id}}); }
    catch (const Error& error) {
      if (error.code != "workspace_busy" || std::chrono::steady_clock::now() >= deadline) throw;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
}
Json completed(Service& service, const std::string& id) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  for (;;) {
    auto job = read_job(service, id); const auto state = text_field(job, "state");
    if (state != "queued" && state != "running" && state != "cancelling") return job;
    if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Printer job exceeded its test bound");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}
void service_and_jobs() {
  Temp temp; const auto workspace = temp.root/"workspace"; const auto record = source(workspace);
  Service service(workspace); auto args = fixture(temp.root);
  const auto head = read_text(workspace/"documents/part/HEAD.json");
  const auto result = service.call("cad_printer_handoff", args);
  require(result.at("source_association") == "caller_declared_not_geometry_verified" && result.at("native_build") != build,
    "Service exposes declared association and its actual native build, not fixture provenance");
  require(service.call("cad_printer_handoff", verify_args(result)).at("readiness") == result.at("readiness"),
    "CLI/MCP Service path re-verifies native findings and physical unknowns");
  auto bad = args; bad["action"] = "upload";
  fails("invalid_argument", [&] { service.call("cad_printer_handoff", bad); });
  const Json submitted = {{"action", "submit"}, {"request_id", "printer_plan"}, {"tool", "cad_printer_handoff"}, {"arguments", args}};
  const auto admitted = service.call("cad_job", submitted), job = completed(service, text_field(admitted, "job_id"));
  require(job.at("state") == "succeeded" && job.at("result").at("hardware_contact") == false,
    "Durable printer job publishes a native offline plan without contacting hardware");
  require(service.call("cad_job", submitted).at("job_id") == admitted.at("job_id"),
    "An idempotent printer job replay retains its original identity");
  const auto verified = service.call("cad_job", {{"action", "submit"}, {"request_id", "printer_verify"},
    {"tool", "cad_printer_handoff"}, {"arguments", verify_args(job.at("result"))}});
  require(completed(service, text_field(verified, "job_id")).at("state") == "succeeded",
    "Native verification is accepted through the same bounded job contract");
  contract_arguments.push_back(args); contract_results.push_back(result);
  const auto previous = files(workspace);
  const auto timed = service.call("cad_job", {{"action", "submit"}, {"request_id", "printer_timeout"},
    {"tool", "cad_printer_handoff"}, {"arguments", args}, {"budget", {{"timeout_ms", 1}}}});
  const auto timeout = completed(service, text_field(timed, "job_id"));
  require(timeout.at("state") == "failed" && timeout.at("error").at("code") == "job_timeout",
    "A one-millisecond printer job exhausts its native budget explicitly");
  require(files(workspace) == previous && read_text(workspace/"documents/part/HEAD.json") == head,
    "Timed-out planning preserves existing exports and source HEAD");
  std::string large = good; const std::string comment = ";" + std::string(1022, 'x') + "\n";
  for (int i = 0; i < 32768; ++i) large += comment;
  args = fixture(temp.root, large);
  const auto cancellable = service.call("cad_job", {{"action", "submit"}, {"request_id", "printer_cancel"},
    {"tool", "cad_printer_handoff"}, {"arguments", args}});
  const auto cancel_id = text_field(cancellable, "job_id");
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  auto running = read_job(service, cancel_id);
  while (running.at("state") == "queued" && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2)); running = read_job(service, cancel_id);
  }
  require(running.at("state") == "running", "Cancellation fixture observes an actually running printer plan");
  service.call("cad_job", {{"action", "cancel"}, {"job_id", cancel_id}});
  require(completed(service, cancel_id).at("state") == "cancelled", "Running printer planning honors native cancellation");
  require(files(workspace) == previous && read_text(workspace/"documents/part/HEAD.json") == head,
    "Cancelled planning removes its private stage and preserves prior exports and raw HEAD");
}
}
int main(int argc, char** argv) {
  try {
    set_worker_executable(path_from_utf8(CAD_SERVICE_EXE)); validation(); packages(); failures_and_external(); service_and_jobs();
    if (argc == 2) {
      auto definitions = printer_definitions(); definitions.update(gcode_definitions());
      atomic_text(path_from_utf8(argv[1]), Json{{"definitions", definitions}, {"arguments", contract_arguments}, {"results", contract_results}}.dump(2)+"\n", 4*1024*1024);
    }
    std::cout << "printer: " << checks << " checks passed\n"; return 0;
  }
  catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
