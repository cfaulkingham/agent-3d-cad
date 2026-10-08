#include "agentcad/printer.hpp"
#include "agentcad/gcode.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/jobs.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <random>
#include <set>

namespace agentcad {
namespace {
constexpr std::size_t plan_limit = 2 * 1024 * 1024;
const std::array<const char*, 3> roles = {"machine", "process", "filament"};
Json object(Json properties, Json required) {
  return {{"type", "object"}, {"properties", properties}, {"required", required}, {"additionalProperties", false}};
}
void digest(const Json& value) {
  if (!value.is_string() || value.get_ref<const std::string&>().size() != 64 ||
      value.get_ref<const std::string&>().find_first_not_of("0123456789abcdef") != std::string::npos)
    throw Error("invalid_argument", "Expected SHA-256 must be 64 lowercase hex digits");
}
void printable(const Json& value) {
  if (!value.is_string()) throw Error("invalid_argument", "Printer descriptions must be text");
  const auto& text = value.get_ref<const std::string&>();
  if (text.empty() || text.size() > 64 || invalid_utf8_offset(text) ||
      std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; }))
    throw Error("invalid_argument", "Printer descriptions must be 1..64 printable UTF-8 bytes");
}
void artifact(const Json& value) {
  fields(value, {"path", "expected_sha256"});
  if (!path_from_utf8(text_field(value, "path")).is_absolute())
    throw Error("invalid_argument", "Printer inputs require absolute paths");
  digest(value.at("expected_sha256"));
}
void options_valid(const Json& options) {
  fields(options, {"printer", "profiles", "review"}, {"template_project"});
  const auto& printer = options.at("printer");
  fields(printer, {"backend", "id", "model", "nozzle_diameter_mm", "bed_type", "handoff"});
  portable_identifier(text_field(printer, "id"));
  printable(printer.at("model")); printable(printer.at("bed_type"));
  const auto& nozzle = printer.at("nozzle_diameter_mm");
  if (!nozzle.is_number() || !std::isfinite(nozzle.get<double>()) || nozzle < 0.05 || nozzle > 5)
    throw Error("invalid_argument", "Explicit nozzle diameter must be within 0.05..5 mm");
  if (printer.at("backend") != "manual" && printer.at("backend") != "bambu_lan")
    throw Error("invalid_argument", "Printer planning supports manual or external Bambu LAN handoff");
  if (printer.at("handoff") != "plain_gcode" && printer.at("handoff") != "template_project")
    throw Error("invalid_argument", "Printer handoff must be plain_gcode or template_project");
  const bool project = printer.at("handoff") == "template_project";
  if (project && printer.at("backend") != "bambu_lan")
    throw Error("invalid_argument", "Template-project handoff belongs to the external Bambu LAN workflow");
  if (project != options.contains("template_project"))
    throw Error("invalid_argument", "Template-project handoff requires exactly one explicit template input");
  if (project) {
    const auto& input = options.at("template_project");
    fields(input, {"path", "expected_sha256", "printer_model", "plate"});
    artifact(Json{{"path", input.at("path")}, {"expected_sha256", input.at("expected_sha256")}});
    printable(input.at("printer_model"));
    if (input.at("printer_model") != printer.at("model"))
      throw Error("invalid_argument", "Caller-declared template and target printer models differ");
    const auto& plate = input.at("plate");
    if (!plate.is_number_integer() || plate < 1 || plate > 16)
      throw Error("invalid_argument", "Template plate must be an integer within 1..16");
  }
  fields(options.at("profiles"), {"machine", "process", "filament"});
  for (const auto* role : roles) artifact(options.at("profiles").at(role));
  validate_gcode_options(options.at("review"));
}
fs::path regular(const fs::path& path) {
  if (!path.is_absolute() || !fs::is_regular_file(path) || fs::is_symlink(fs::symlink_status(path)))
    throw Error("invalid_argument", "Printer artifacts require absolute regular non-symlink files");
  return path;
}
std::string raw_input(const Json& input, std::size_t limit) {
  check_job_cancelled();
  const auto raw = read_text(regular(path_from_utf8(text_field(input, "path"))), limit);
  if (sha256(raw) != text_field(input, "expected_sha256"))
    throw Error("artifact_mismatch", "Printer input differs from its expected SHA-256");
  return raw;
}
void gcode_extension(const fs::path& path) {
  auto extension = path_to_utf8(path.extension());
  for (auto& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (extension != ".gcode") throw Error("invalid_argument", "Printer plans require existing plain .gcode");
}
void template_signature(const std::string& raw, const std::string& name) {
  auto suffix = name; for (auto& c : suffix) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (!suffix.ends_with(".gcode.3mf") || raw.size() < 4 || raw.compare(0, 4, "PK\x03\x04", 4) != 0)
    throw Error("invalid_argument", "Template must be a .gcode.3mf file with a ZIP local-header signature");
  // No ZIP extraction or model/firmware compatibility claim is made here.
  // Actual archive/plate replacement remains the explicit external dry-run step.
}
Json profile(const std::string& raw, const std::string& role) {
  const auto value = parse_json(raw, printer_profile_bytes_limit);
  if (!value.is_object() || value.value("type", Json()) != role || !value.contains("name"))
    throw Error("invalid_argument", "Printer profile must identify its matching machine/process/filament role");
  printable(value.at("name"));
  if (value.contains("inherits") && (!value.at("inherits").is_string() || !value.at("inherits").get_ref<const std::string&>().empty()))
    throw Error("invalid_argument", "Supply explicit resolved printer profiles; implicit inheritance is unsupported");
  if (value.contains("post_process")) {
    if (!value.at("post_process").is_array()) throw Error("invalid_argument", "Profile post-processing must be an empty string list");
    for (const auto& entry : value.at("post_process"))
      if (!entry.is_string() || !entry.get_ref<const std::string&>().empty())
        throw Error("invalid_argument", "Printer plans prohibit profile post-processing commands");
  }
  return value;
}
std::string feature_of(const Json& record, const Json& args) {
  const auto feature = args.value("feature_id", text_field(record.at("model"), "output"));
  model_identifier(feature);
  for (const auto& entry : record.at("model").at("features")) if (entry.at("id") == feature) return feature;
  throw Error("invalid_argument", "Printer association names a missing source feature");
}
Json source_of(const Json& record, const std::string& feature, const std::string& build) {
  if (build.empty() || build.size() > 256 || invalid_utf8_offset(build) ||
      std::any_of(build.begin(), build.end(), [](unsigned char c) { return c < 32 || c == 127; }))
    throw Error("invalid_argument", "Native build identity must be explicit printable text");
  return {{"document_id", record.at("document_id")}, {"revision", record.at("revision")},
    {"feature_id", feature}, {"model_sha256", sha256(record.at("model").dump())},
    {"kernel_version", record.at("kernel_version")}, {"native_build", build}};
}
void record_matches(const fs::path& workspace, const Json& record, const Json& args) {
  if (args.at("document_id") != record.at("document_id") || args.at("revision") != record.at("revision"))
    throw Error("artifact_mismatch", "Printer plan request belongs to another source record");
  if (Store(workspace).read(text_field(record, "document_id"), revision_number(record.at("revision"))) != record)
    throw Error("artifact_mismatch", "Committed printer source record changed");
}
Json readiness_for(const Json& options, const Json& report) {
  const bool bambu = options.at("printer").at("backend") == "bambu_lan";
  const bool project = options.at("printer").at("handoff") == "template_project";
  const auto check = [](const char* id, const Json& status, const char* reason) {
    return Json{{"id", id}, {"status", status}, {"reason", reason}};
  };
  Json checks = Json::array({
    check("static_gcode", report.at("status"), "Native static findings use caller-declared Marlin bounds/material/initial assumptions; they are not physical firmware simulation."),
    check("profile_compatibility", "unknown", "Profile bytes and role/name are verified; actual machine/nozzle/bed/material compatibility and toolpath provenance are not established."),
    check("machine_setup", "unknown", "Target printer identity, firmware, storage, calibration and current state have not been observed."),
    check("physical_setup", "unknown", "Correct installed plate/nozzle/filament, clear build area and nearby operator require current physical checks."),
    check("print_intent", "unknown", "A prepared plan does not authorize an upload or physical print start; use the user's specific requested action."),
    check("external_adapter", "unknown", bambu ? "Install and verify the optional Bambu LAN helper/runtime/config, read live status and inspect its exact dry-run payload before any authorized upload." : "The operator must verify the intended machine's documented file-transfer procedure and supported G-code semantics."),
    check("handoff_format", "unknown", project ? "Only the template's raw hash and ZIP signature are checked; the external helper must validate the same-printer archive, selected plate and resulting project." : "Plain G-code transfer/start compatibility has not been tested on this printer; A1 Mini plain gcode_file starts are unsuitable for the documented skill workflow.")
  });
  return {{"status", report.at("status") == "fail" ? "fail" : "unknown"}, {"checks", checks},
    {"native_upload_supported", false}, {"native_start_supported", false},
    {"requires_specific_print_start", true}};
}
Json workflow_for(const Json& options) {
  const bool bambu = options.at("printer").at("backend") == "bambu_lan";
  return {{"backend", options.at("printer").at("backend")}, {"handoff", options.at("printer").at("handoff")},
    {"adapter", bambu ? "optional_installed_bambu_labs_skill" : "operator_managed_transfer"},
    {"installation_status", "not_checked"}, {"credentials", "external_private_config_not_in_package"},
    {"steps", Json::array({"verify_reviewed_native_plan", "verify_installed_adapter_and_machine_setup",
      "read_current_printer_status", "inspect_exact_external_dry_run", "authorized_upload_only",
      "confirm_uploaded_artifact", "specific_authorized_print_start", "confirm_acceptance_and_observe_first_layer"})},
    {"hardware_contact", false}, {"physical_print_started", false}};
}
std::vector<std::string> names_for(const Json& options) {
  std::vector<std::string> names = {"original.gcode", "source.json", "review.json",
    "profiles/machine.json", "profiles/process.json", "profiles/filament.json"};
  if (options.contains("template_project")) names.push_back("template.gcode.3mf");
  return names;
}
Json ledger_for(const fs::path& root, const std::vector<std::string>& names) {
  Json ledger = Json::array(); std::uintmax_t total = 0;
  for (const auto& name : names) {
    check_job_cancelled(); const auto path = regular(root/path_from_utf8(name)); const auto size = fs::file_size(path);
    if (size > printer_package_bytes_limit - total) throw Error("limit_exceeded", "Printer package exceeds 128 MiB");
    total += size; ledger.push_back({{"path", name}, {"bytes", size}, {"sha256", sha256_file(path, printer_package_bytes_limit, check_job_cancelled)}});
  }
  return ledger;
}
Json manifest_for(const Json& source, const Json& artifacts, const std::string& plan) {
  auto files = artifacts; files.push_back({{"path", "plan.json"}, {"bytes", plan.size()}, {"sha256", sha256(plan)}});
  return {{"schema_version", 1}, {"source", source}, {"artifacts", files},
    {"hardware_contact", false}, {"physical_print_started", false}, {"printer_approval", "not_evaluated"}};
}
void package_entries(const fs::path& root, const std::vector<std::string>& names) {
  if (!fs::is_directory(root) || fs::is_symlink(fs::symlink_status(root))) throw Error("artifact_mismatch", "Printer package root must be a regular directory");
  std::set<std::string> files(names.begin(), names.end()); files.insert("plan.json"); files.insert("manifest.json");
  std::size_t count = 0; std::uintmax_t bytes = 0;
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    check_job_cancelled();
    const auto relative = entry.path().lexically_relative(root).generic_u8string();
    const auto name = std::string(reinterpret_cast<const char*>(relative.data()), relative.size());
    if (++count > 16 || entry.is_symlink() || (!entry.is_regular_file() && !entry.is_directory()))
      throw Error("artifact_mismatch", "Printer package has unsupported entries");
    if (entry.is_directory()) { if (name != "profiles") throw Error("artifact_mismatch", "Printer package has an unexpected directory"); continue; }
    const auto size = entry.file_size();
    if (size > printer_package_bytes_limit - bytes) throw Error("limit_exceeded", "Printer package exceeds 128 MiB");
    bytes += size;
    if (!files.erase(name)) throw Error("artifact_mismatch", "Printer package has an unexpected file");
  }
  if (!files.empty()) throw Error("artifact_mismatch", "Printer package is incomplete");
}
Json result_for(const Json& plan, const fs::path& root, const std::string& raw, const char* action) {
  auto result = plan.at("source"); result["action"] = action; result["source_association"] = plan.at("source_association"); result["directory"] = path_to_utf8(root);
  result["path"] = path_to_utf8(root/"plan.json"); result["sha256"] = sha256(raw);
  result["gcode_path"] = path_to_utf8(root/"original.gcode"); result["gcode_sha256"] = plan.at("gcode").at("sha256");
  result["gcode_bytes"] = plan.at("gcode").at("bytes"); result["report"] = plan.at("report");
  result["readiness"] = plan.at("readiness"); result["hardware_contact"] = false; result["physical_print_started"] = false;
  return result;
}
struct Stage {
  fs::path path;
  explicit Stage(const fs::path& parent) { directory(parent); path = temporary_directory(parent); }
  ~Stage() { std::error_code ignored; fs::remove_all(path, ignored); }
};
}

Json printer_definitions() {
  Json definitions;
  const Json hash = {{"type", "string"}, {"pattern", "^[a-f0-9]{64}$"}}, text = {{"type", "string"}}, id = {{"type", "string"}, {"pattern", "^[A-Za-z][A-Za-z0-9_-]{0,63}$"}};
  const Json name = {{"type", "string"}, {"minLength", 1}, {"maxLength", 64}, {"pattern", "^[^\\u0000-\\u001f\\u007f]+$"}};
  const Json file = object({{"path", text}, {"expected_sha256", hash}}, {"path", "expected_sha256"});
  const Json printer = object({{"backend", {{"enum", {"manual", "bambu_lan"}}}}, {"id", id}, {"model", name},
    {"nozzle_diameter_mm", {{"type", "number"}, {"minimum", .05}, {"maximum", 5}}}, {"bed_type", name},
    {"handoff", {{"enum", {"plain_gcode", "template_project"}}}}}, {"backend", "id", "model", "nozzle_diameter_mm", "bed_type", "handoff"});
  definitions["printer_options"] = object({{"printer", printer},
    {"profiles", object({{"machine", file}, {"process", file}, {"filament", file}}, {"machine", "process", "filament"})},
    {"review", {{"$ref", "#/$defs/gcode_options"}}},
    {"template_project", object({{"path", text}, {"expected_sha256", hash}, {"printer_model", name},
      {"plate", {{"type", "integer"}, {"minimum", 1}, {"maximum", 16}}}}, {"path", "expected_sha256", "printer_model", "plate"})}}, {"printer", "profiles", "review"});
  const Json revision = {{"type", "integer"}, {"minimum", 1}, {"maximum", 9007199254740991ULL}};
  definitions["printer_arguments"] = {{"type", "object"}, {"oneOf", Json::array({
    object({{"document_id", id}, {"revision", revision}, {"action", {{"const", "plan"}}}, {"feature_id", id},
      {"path", text}, {"expected_sha256", hash}, {"options", {{"$ref", "#/$defs/printer_options"}}}}, {"document_id", "revision", "action", "path", "expected_sha256", "options"}),
    object({{"document_id", id}, {"revision", revision}, {"action", {{"const", "verify"}}}, {"plan_path", text},
      {"expected_sha256", hash}}, {"document_id", "revision", "action", "plan_path", "expected_sha256"})})}};
  const Json check = object({{"id", {{"enum", {"static_gcode", "profile_compatibility", "machine_setup", "physical_setup", "print_intent", "external_adapter", "handoff_format"}}}},
    {"status", {{"enum", {"pass", "fail", "unknown"}}}}, {"reason", text}}, {"id", "status", "reason"});
  definitions["printer_readiness"] = object({{"status", {{"enum", {"fail", "unknown"}}}},
    {"checks", {{"type", "array"}, {"items", check}, {"minItems", 7}, {"maxItems", 7}}},
    {"native_upload_supported", {{"const", false}}}, {"native_start_supported", {{"const", false}}},
    {"requires_specific_print_start", {{"const", true}}}}, {"status", "checks", "native_upload_supported", "native_start_supported", "requires_specific_print_start"});
  definitions["printer_result"] = object({{"document_id", id}, {"revision", revision}, {"feature_id", id},
    {"model_sha256", hash}, {"kernel_version", {{"const", "8.0.1"}}}, {"native_build", text},
    {"action", {{"enum", {"plan", "verify"}}}}, {"source_association", {{"const", "caller_declared_not_geometry_verified"}}}, {"directory", text}, {"path", text}, {"sha256", hash},
    {"gcode_path", text}, {"gcode_sha256", hash}, {"gcode_bytes", {{"type", "integer"}, {"minimum", 0}, {"maximum", gcode_bytes_limit}}},
    {"report", {{"$ref", "#/$defs/gcode_report"}}}, {"readiness", {{"$ref", "#/$defs/printer_readiness"}}},
    {"hardware_contact", {{"const", false}}}, {"physical_print_started", {{"const", false}}}},
    {"document_id", "revision", "feature_id", "model_sha256", "kernel_version", "native_build", "action", "source_association", "directory", "path", "sha256", "gcode_path", "gcode_sha256", "gcode_bytes", "report", "readiness", "hardware_contact", "physical_print_started"});
  return definitions;
}
void validate_printer_arguments(const Json& args) {
  const auto action = text_field(args, "action");
  if (action == "plan") {
    fields(args, {"document_id", "revision", "action", "path", "expected_sha256", "options"}, {"feature_id"});
    artifact(Json{{"path", args.at("path")}, {"expected_sha256", args.at("expected_sha256")}}); options_valid(args.at("options"));
    gcode_extension(path_from_utf8(text_field(args, "path")));
    if (args.contains("feature_id")) model_identifier(text_field(args, "feature_id"));
  } else if (action == "verify") {
    fields(args, {"document_id", "revision", "action", "plan_path", "expected_sha256"});
    artifact(Json{{"path", args.at("plan_path")}, {"expected_sha256", args.at("expected_sha256")}});
  } else throw Error("invalid_argument", "Printer handoff action must be plan or verify; native upload/start is unsupported");
  identifier(text_field(args, "document_id")); revision_number(args.at("revision"));
}
Json plan_printer_handoff(const fs::path& workspace, const Json& record, const Json& args, const std::string& build) {
  validate_printer_arguments(args);
  if (args.at("action") != "plan") throw Error("invalid_argument", "Expected printer plan action");
  record_matches(workspace, record, args); check_job_cancelled();
  const auto& options = args.at("options"); const auto source = source_of(record, feature_of(record, args), build);
  const auto raw = raw_input(Json{{"path", args.at("path")}, {"expected_sha256", args.at("expected_sha256")}}, gcode_bytes_limit);
  Stage stage(workspace/"exports"); directory(stage.path/"profiles"); atomic_text(stage.path/"original.gcode", raw, gcode_bytes_limit);
  Json profiles = Json::object();
  for (const auto* role : roles) {
    const auto content = raw_input(options.at("profiles").at(role), printer_profile_bytes_limit);
    profiles[role] = profile(content, role).at("name"); atomic_text(stage.path/"profiles"/(std::string(role)+".json"), content, printer_profile_bytes_limit);
  }
  if (options.contains("template_project")) {
    const auto content = raw_input(options.at("template_project"), gcode_bytes_limit);
    template_signature(content, text_field(options.at("template_project"), "path")); atomic_text(stage.path/"template.gcode.3mf", content, gcode_bytes_limit);
  }
  const auto report = evaluate_model(workspace, record.at("model"), {{"kind", "gcode_review"}, {"path", path_to_utf8(stage.path/"original.gcode")}, {"options", options.at("review")}}).at("gcode_report");
  const Json gcode = {{"path", "original.gcode"}, {"sha256", sha256(raw)}, {"bytes", raw.size()}};
  const auto review = Json{{"schema_version", 1}, {"source", source}, {"source_association", "caller_declared_not_geometry_verified"},
    {"gcode", gcode}, {"options", options.at("review")}, {"report", report}}.dump(2)+"\n";
  atomic_text(stage.path/"source.json", record.dump(2)+"\n"); atomic_text(stage.path/"review.json", review, plan_limit);
  const auto names = names_for(options); const auto artifacts = ledger_for(stage.path, names);
  const Json plan = {{"schema_version", 1}, {"source", source}, {"source_association", "caller_declared_not_geometry_verified"},
    {"options", options}, {"profiles", profiles}, {"gcode", gcode}, {"artifacts", artifacts}, {"report", report},
    {"readiness", readiness_for(options, report)}, {"workflow", workflow_for(options)}, {"hardware_contact", false}, {"physical_print_started", false}};
  const auto content = plan.dump(2)+"\n"; atomic_text(stage.path/"plan.json", content, plan_limit);
  atomic_text(stage.path/"manifest.json", manifest_for(source, artifacts, content).dump(2)+"\n", plan_limit); package_entries(stage.path, names);
  std::random_device random; const auto nonce = sha256(Json::array({random(), random(), random(), random()}).dump()).substr(0, 32);
  const auto destination = workspace/"exports"/(text_field(source, "document_id")+"-r"+std::to_string(revision_number(source.at("revision")))+"-printer-plan-"+nonce);
  DocumentLock lock(workspace, text_field(source, "document_id"), LockWait::publication);
  check_job_cancelled(); record_matches(workspace, record, args); fs::rename(stage.path, destination);
  return result_for(plan, destination, content, "plan");
}
Json verify_printer_handoff(const fs::path& workspace, const Json& record, const Json& args, const std::string& build) {
  validate_printer_arguments(args);
  if (args.at("action") != "verify") throw Error("invalid_argument", "Expected printer verify action");
  record_matches(workspace, record, args); check_job_cancelled();
  const auto path = regular(path_from_utf8(text_field(args, "plan_path"))); const auto raw = read_text(path, plan_limit);
  if (path.filename() != "plan.json") throw Error("invalid_argument", "Reviewed printer plan must name plan.json in its package");
  if (sha256(raw) != text_field(args, "expected_sha256")) throw Error("artifact_mismatch", "Reviewed printer plan bytes changed");
  const auto plan = parse_json(raw, plan_limit);
  fields(plan, {"schema_version", "source", "source_association", "options", "profiles", "gcode", "artifacts", "report", "readiness", "workflow", "hardware_contact", "physical_print_started"});
  if (plan.at("schema_version") != 1 || plan.at("source_association") != "caller_declared_not_geometry_verified" || plan.at("hardware_contact") != false || plan.at("physical_print_started") != false)
    throw Error("artifact_mismatch", "Reviewed printer plan has unsupported version or physical-operation claims");
  fields(plan.at("source"), {"document_id", "revision", "feature_id", "model_sha256", "kernel_version", "native_build"});
  identifier(text_field(plan.at("source"), "document_id"));
  model_identifier(text_field(plan.at("source"), "feature_id")); revision_number(plan.at("source").at("revision"));
  digest(plan.at("source").at("model_sha256")); text_field(plan.at("source"), "native_build");
  options_valid(plan.at("options")); const auto& options = plan.at("options");
  auto qualified_args = args; qualified_args["feature_id"] = plan.at("source").at("feature_id");
  const auto source = source_of(record, feature_of(record, qualified_args), build);
  if (plan.at("source") != source) throw Error("artifact_mismatch", "Reviewed printer plan source/build identity differs");
  const auto root = path.parent_path(); const auto names = names_for(options); package_entries(root, names);
  if (plan.at("artifacts") != ledger_for(root, names)) throw Error("artifact_mismatch", "Reviewed printer package bytes or ledger changed");
  const auto manifest_raw = read_text(root/"manifest.json", plan_limit);
  if (parse_json(manifest_raw, plan_limit) != manifest_for(source, plan.at("artifacts"), raw))
    throw Error("artifact_mismatch", "Reviewed printer manifest changed");
  if (parse_json(read_text(root/"source.json")) != record) throw Error("artifact_mismatch", "Reviewed printer source snapshot differs");
  const auto gcode = read_text(root/"original.gcode", gcode_bytes_limit);
  const Json identity = {{"path", "original.gcode"}, {"sha256", sha256(gcode)}, {"bytes", gcode.size()}};
  if (plan.at("gcode") != identity) throw Error("artifact_mismatch", "Reviewed printer G-code identity differs");
  Json profiles = Json::object();
  for (const auto* role : roles) {
    const auto content = read_text(root/"profiles"/(std::string(role)+".json"), printer_profile_bytes_limit);
    if (sha256(content) != text_field(options.at("profiles").at(role), "expected_sha256")) throw Error("artifact_mismatch", "Reviewed printer profile differs from declared snapshot");
    profiles[role] = profile(content, role).at("name");
  }
  if (profiles != plan.at("profiles")) throw Error("artifact_mismatch", "Reviewed printer profile names differ");
  if (options.contains("template_project")) {
    const auto content = read_text(root/"template.gcode.3mf", gcode_bytes_limit);
    if (sha256(content) != text_field(options.at("template_project"), "expected_sha256")) throw Error("artifact_mismatch", "Reviewed printer template differs");
    template_signature(content, text_field(options.at("template_project"), "path"));
  }
  const auto report = evaluate_model(workspace, record.at("model"), {{"kind", "gcode_review"}, {"path", path_to_utf8(root/"original.gcode")}, {"options", options.at("review")}}).at("gcode_report");
  const Json reviewed = {{"schema_version", 1}, {"source", source}, {"source_association", "caller_declared_not_geometry_verified"},
    {"gcode", identity}, {"options", options.at("review")}, {"report", report}};
  if (plan.at("report") != report || parse_json(read_text(root/"review.json", plan_limit), plan_limit) != reviewed ||
      plan.at("readiness") != readiness_for(options, report) || plan.at("workflow") != workflow_for(options))
    throw Error("artifact_mismatch", "Reviewed printer findings or workflow differ from native verification");
  // Recheck every captured byte after the worker; concurrent replacement may
  // invalidate verification, but can never produce an upload/start side effect.
  package_entries(root, names);
  if (read_text(path, plan_limit) != raw || read_text(root/"manifest.json", plan_limit) != manifest_raw || plan.at("artifacts") != ledger_for(root, names))
    throw Error("artifact_mismatch", "Reviewed printer package changed during verification");
  record_matches(workspace, record, args); check_job_cancelled();
  return result_for(plan, root, raw, "verify");
}
}
