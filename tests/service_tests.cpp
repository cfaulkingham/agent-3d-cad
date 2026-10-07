#include "agentcad/hash.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/mcp.hpp"
#include "agentcad/service.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <thread>
#include <tuple>
#include <vector>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

// Service-level transaction, lock, receipt and error-mapping regressions.
using namespace agentcad;
using namespace std::chrono_literals;
namespace {
int checks = 0;
using Clock = std::chrono::steady_clock;
void require(bool value, const std::string& message) { ++checks; if (!value) throw std::runtime_error(message); }
Error fails(const std::string& code, const std::function<void()>& action) {
  try { action(); }
  catch (const Error& e) { require(e.code == code, "Expected " + code + ", got " + e.code + ": " + e.what()); return e; }
  throw std::runtime_error("Expected " + code);
}
std::chrono::milliseconds since(Clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
}
struct Temp {
  fs::path path = temporary_file(fs::temp_directory_path());
  Temp() { fs::remove(path); directory(path); }
  ~Temp() { std::error_code ignored; fs::remove_all(path, ignored); }
};
Json box(double height = 6) {
  return {{"schema_version",1},{"units","mm"},{"parameters",{{"height",height}}},
    {"features",Json::array({{{"id","base"},{"type","box"},{"size",Json::array({20,10,Json{{"parameter","height"}}})}}})},{"output","base"}};
}
Json assembly() {
  return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},
    {"features",Json::array({{{"id","block"},{"type","box"},{"size",{2,3,4}}},
      {{"id","fixture"},{"type","assembly"},{"parts",Json::array({{{"id","p1"},{"input","block"}},
        {{"id","p2"},{"input","block"},{"placement",{{"translation",{10,0,0}}}}}})}}})},{"output","fixture"}};
}
Json edit(std::uint64_t revision, double height, const std::string& request_id = "") {
  Json result = {{"document_id","part"},{"expected_revision",revision},
    {"operations",Json::array({{{"op","set_parameter"},{"name","height"},{"value",height}}})}};
  if (!request_id.empty()) result["request_id"] = request_id;
  return result;
}
std::uint64_t head(Service& service, const std::string& id = "part") {
  return service.call("cad_read",{{"document_id",id}}).at("revision").get<std::uint64_t>();
}

// Another owner of the same document lock, through the public storage API and a
// separate file descriptor/handle, that releases after `hold`.
class Holder {
public:
  Holder(const fs::path& root, const std::string& id, std::chrono::milliseconds hold) {
    std::promise<void> ready; auto acquired = ready.get_future();
    thread_ = std::thread([root, id, hold, ready = std::move(ready)]() mutable {
      try { DocumentLock lock(root, id); ready.set_value(); std::this_thread::sleep_for(hold); }
      catch (...) { ready.set_exception(std::current_exception()); }
    });
    try { acquired.get(); } catch (...) { thread_.join(); throw; }
  }
  ~Holder() { thread_.join(); }
  Holder(const Holder&) = delete;
  Holder& operator=(const Holder&) = delete;
private:
  std::thread thread_;
};

void lock_wait_tests() {
  Temp temp; const auto& root = temp.path;
  {
    Holder holder(root, "part", 300ms);
    auto start = Clock::now();
    fails("workspace_busy", [&]{ DocumentLock immediate(root, "part"); });
    require(since(start) < 250ms, "Default document lock stays non-blocking");
    start = Clock::now();
    DocumentLock independent(root, "other", LockWait::publication);
    require(since(start) < 250ms, "Publication wait applies only to the contended document");
    start = Clock::now();
    DocumentLock waited(root, "part", LockWait::publication);
    const auto elapsed = since(start);
    require(elapsed >= 150ms && elapsed < publication_lock_wait, "Publication lock waits for a transient holder (" + std::to_string(elapsed.count()) + " ms)");
  }
  {
    Holder holder(root, "part", publication_lock_wait + 1500ms);
    const auto start = Clock::now();
    const auto error = fails("workspace_busy", [&]{ DocumentLock expired(root, "part", LockWait::publication); });
    const auto elapsed = since(start);
    require(elapsed >= publication_lock_wait - 20ms && elapsed < publication_lock_wait + 1000ms,
      "Publication wait is bounded (" + std::to_string(elapsed.count()) + " ms)");
    require(error.details.at("waited_ms") == publication_lock_wait.count(), "Expired wait reports its bound");
  }
#ifndef _WIN32
  // A second process holding the lock is waited for in the same way.
  int ready[2];
  require(::pipe(ready) == 0, "pipe");
  const auto child = ::fork();
  require(child >= 0, "fork");
  if (child == 0) {
    ::close(ready[0]);
    try { DocumentLock lock(root, "part"); (void)::write(ready[1], "x", 1); std::this_thread::sleep_for(300ms); }
    catch (...) { ::_exit(2); }
    ::_exit(0);
  }
  ::close(ready[1]);
  char signal = 0; require(::read(ready[0], &signal, 1) == 1, "child owns document lock");
  ::close(ready[0]);
  const auto start = Clock::now();
  { DocumentLock waited(root, "part", LockWait::publication); }
  require(since(start) >= 150ms, "Publication lock waits for another process");
  int status = 0; ::waitpid(child, &status, 0);
  require(WIFEXITED(status) && WEXITSTATUS(status) == 0, "lock child exits cleanly");
#endif
}

void publication_wait_tests() {
  Temp temp; Service service(temp.path);
  service.call("cad_create",{{"document_id","part"},{"model",box()}});
  {
    // A transient holder (viewer sync, mesh chunk, another server) delays but
    // never fails a mutation.
    Holder holder(temp.path, "part", 300ms);
    require(service.call("cad_apply",edit(1,8)).at("revision") == 2, "Mutation waits for a transient document holder");
  }
  // Export holds no document lock until publication: keep the lock from the
  // moment the build starts until its STEP bytes exist, so the finished build
  // must wait at publication rather than discard its work.
  {
    const auto exports = temp.path / "exports";
    auto holder = std::async(std::launch::async, [&] {
      const auto deadline = Clock::now() + 60s;
      fs::path pending;
      while (pending.empty()) {
        std::error_code ignored;
        for (const auto& entry : fs::directory_iterator(exports, ignored))
          if (path_to_utf8(entry.path().filename()).starts_with(".pending-")) pending = entry.path();
        require(Clock::now() < deadline, "export started");
        std::this_thread::sleep_for(1ms);
      }
      std::unique_ptr<DocumentLock> lock;
      while (!lock) {
        try { lock = std::make_unique<DocumentLock>(temp.path, "part"); }
        catch (const Error& e) { if (e.code != "workspace_busy" || Clock::now() >= deadline) throw; std::this_thread::sleep_for(1ms); }
      }
      for (std::error_code error; fs::exists(pending, error) && fs::file_size(pending, error) == 0;) {
        require(Clock::now() < deadline, "export produced STEP bytes");
        std::this_thread::sleep_for(2ms);
      }
      std::this_thread::sleep_for(400ms);
    });
    const auto exported = service.call("cad_export",{{"document_id","part"},{"revision",2},{"format","step"}});
    holder.get();
    require(fs::exists(path_from_utf8(exported.at("path").get<std::string>())) && exported.at("bytes") > 0, "Export publishes after waiting");
  }
  // cad_bom publication is the only lock in that tool.
  service.call("cad_create",{{"document_id","asm"},{"model",assembly()}});
  {
    Holder holder(temp.path, "asm", 300ms);
    const auto bom = service.call("cad_bom",{{"document_id","asm"},{"revision",1}});
    require(fs::exists(path_from_utf8(bom.at("path").get<std::string>())), "BOM manifest publishes after waiting");
  }
  // Concurrent writers with the same expected revision: exactly one commits
  // and the other observes revision_conflict, never workspace_busy.
  for (int round = 0; round < 3; ++round) {
    const auto base = head(service);
    auto run = [&](double height) {
      Service writer(temp.path);
      try { writer.call("cad_apply", edit(base, height)); return std::string("ok"); }
      catch (const Error& e) { return e.code; }
    };
    auto first = std::async(std::launch::async, run, 10.0 + round), second = std::async(std::launch::async, run, 20.0 + round);
    std::vector<std::string> outcomes = {first.get(), second.get()};
    std::sort(outcomes.begin(), outcomes.end());
    require(outcomes == std::vector<std::string>{"ok","revision_conflict"}, "Concurrent edits: one commit and one revision_conflict (got " + outcomes[0] + ", " + outcomes[1] + ")");
    require(head(service) == base + 1, "Exactly one concurrent edit published");
  }
}

void receipt_tests() {
  Temp temp; Service service(temp.path);
  const Json create = {{"document_id","part"},{"model",box()},{"request_id","createOnce"}};
  const auto created = service.call("cad_create", create);
  const auto first = edit(1, 7, "editOne");
  const auto edited = service.call("cad_apply", first);
  service.call("cad_apply", edit(2, 8));
  const auto document = temp.path / "documents" / "part";
  const auto receipts = document / "receipts";
  auto entry = [&](const std::string& id) { return receipts / (sha256(id) + ".json"); };
  auto coverage = [&] { return parse_json(read_text(receipts / "coverage.json")).at("indexed_through_revision"); };
  require(fs::exists(entry("createOnce")) && fs::exists(entry("editOne")), "Committed receipts are indexed");
  require(coverage() == 3, "Index coverage follows HEAD");
  auto conflicting = first; conflicting["operations"][0]["value"] = 99;

  // Replay hits the index: it reads HEAD, the entry and the named revision, not
  // the unrelated newer history.
  const auto revision2 = read_text(document / "revisions" / "2.json"), revision3 = read_text(document / "revisions" / "3.json");
  atomic_text(document / "revisions" / "3.json", "not json");
  require(service.call("cad_apply", first) == edited, "Indexed replay does not scan newer revisions");
  atomic_text(document / "revisions" / "2.json", "not json");
  require(service.call("cad_create", create) == created, "Indexed replay reads only its revision");
  auto changed = create; changed["model"]["parameters"]["height"] = 9;
  fails("request_conflict", [&]{ service.call("cad_create", changed); });
  atomic_text(document / "revisions" / "2.json", revision2); atomic_text(document / "revisions" / "3.json", revision3);
  fails("request_conflict", [&]{ service.call("cad_apply", conflicting); });

  // Without an index (older builds), replay falls back to the full scan; the
  // next commit backfills the index.
  fs::remove_all(receipts);
  require(service.call("cad_apply", first) == edited, "Scan fallback replays without an index");
  fails("request_conflict", [&]{ service.call("cad_apply", conflicting); });
  require(head(service) == 3, "Replay without an index commits nothing");
  require(service.call("cad_apply", edit(3, 9)).at("revision") == 4, "Commit on an unindexed document");
  require(fs::exists(entry("createOnce")) && fs::exists(entry("editOne")) && coverage() == 4, "Commit backfills the index");

  // Index entries are hints verified against revisions; wrong ones are ignored.
  atomic_text(entry("editOne"), Json{{"schema_version",1},{"request_id","editOne"},{"revision",1}}.dump());
  require(service.call("cad_apply", first) == edited, "Incorrect entry falls back to a verified scan");
  atomic_text(entry("createOnce"), "garbage");
  require(service.call("cad_create", create) == created, "Damaged entry falls back to a verified scan");
  atomic_text(entry("fresh"), Json{{"schema_version",1},{"request_id","fresh"},{"revision",2}}.dump());
  const auto fresh = edit(4, 10, "fresh");
  require(service.call("cad_apply", fresh).at("revision") == 5, "Forged entry cannot replay another request's revision");
  require(service.call("cad_apply", fresh).at("revision") == 5 && head(service) == 5, "Request replays after commit");
  // An interrupted commit leaves an entry and candidate beyond HEAD.
  auto candidate = parse_json(read_text(document / "revisions" / "5.json"));
  candidate["revision"] = 6;
  candidate["receipt"] = {{"request_id","orphan"},{"fingerprint","interrupted"},{"result",Json::object()}};
  atomic_text(document / "revisions" / "6.json", candidate.dump());
  atomic_text(entry("orphan"), Json{{"schema_version",1},{"request_id","orphan"},{"revision",6}}.dump());
  const auto orphan = edit(5, 11, "orphan");
  require(service.call("cad_apply", orphan).at("revision") == 6, "Uncommitted receipt beyond HEAD is not replayed");
  require(service.call("cad_apply", orphan).at("revision") == 6 && head(service) == 6, "Recommitted request replays");
  // Coverage lagging HEAD (crash after HEAD) scans only the uncovered tail.
  fs::remove(entry("orphan"));
  atomic_text(receipts / "coverage.json", Json{{"schema_version",1},{"indexed_through_revision",5}}.dump());
  require(service.call("cad_apply", orphan).at("revision") == 6, "Uncovered tail is scanned");
  // Job recovery uses the same replay.
  require(Store(temp.path).request_replay("part", "fresh", request_fingerprint("cad_apply", fresh)).value().at("revision") == 5,
    "Store replay serves job recovery");
}

std::string cli_error(const fs::path& workspace, const std::string& tool, const Json& arguments) {
  static int counter = 0;
  const auto input = workspace / ("cli-input-" + std::to_string(++counter) + ".json");
  const auto output = workspace / ("cli-output-" + std::to_string(counter) + ".txt");
  const auto errors = workspace / ("cli-error-" + std::to_string(counter) + ".txt");
  atomic_text(input, arguments.dump());
  const std::string exe = CAD_SERVICE_EXE;
#ifdef _WIN32
  const auto quote = [](const std::string& text) { return "\"" + text + "\""; };
  const auto command = "\"" + quote(exe) + " call " + tool + " --workspace " + quote(path_to_utf8(workspace)) + " --input " +
    quote(path_to_utf8(input)) + " >" + quote(path_to_utf8(output)) + " 2>" + quote(path_to_utf8(errors)) + "\"";
#else
  const auto quote = [](const std::string& text) { return "'" + text + "'"; };
  const auto command = quote(exe) + " call " + tool + " --workspace " + quote(path_to_utf8(workspace)) + " --input " +
    quote(path_to_utf8(input)) + " >" + quote(path_to_utf8(output)) + " 2>" + quote(path_to_utf8(errors));
#endif
  require(std::system(command.c_str()) != 0, "CLI reports failure for " + tool);
  return parse_json(read_text(errors)).at("error").at("code").get<std::string>();
}

void error_mapping_tests() {
  auto captured = [](const std::function<void()>& action) {
    try { action(); } catch (...) { return std::current_exception(); }
    throw std::runtime_error("Expected an exception");
  };
  require(service_error(captured([]{ (void)Json(5).at("id"); })).code == "invalid_argument", "JSON access errors are argument errors");
  require(service_error(captured([]{ throw fs::filesystem_error("probe", std::make_error_code(std::errc::permission_denied)); })).code == "storage_error",
    "Filesystem errors are storage errors");
  require(service_error(captured([]{ throw std::runtime_error("probe"); })).code == "internal_error", "Unknown failures are internal");
  const auto passthrough = service_error(captured([]{ throw Error("revision_conflict", "probe", {{"a",1}}); }));
  require(passthrough.code == "revision_conflict" && passthrough.details.at("a") == 1, "Domain errors pass through unchanged");
  Temp temp;
  fails("storage_error", [&]{ Service unusable(temp.path / std::string(300, 'x') / std::string(300, 'y')); });
  Service service(temp.path);
  service.call("cad_create",{{"document_id","part"},{"model",box()}});
  // The feature pushed by an earlier operation in the same batch is shape
  // checked when added, rather than crashing a later lookup.
  const Json missing_id = {{"document_id","part"},{"expected_revision",1},{"operations",Json::array({
    {{"op","add_feature"},{"feature",{{"type","box"},{"size",{1,1,1}}}}},{{"op","remove_feature"},{"id","base"}}})}};
  auto error = fails("invalid_model", [&]{ service.call("cad_apply", missing_id); });
  require(error.details.at("operation_index") == 0, "Malformed feature names its operation");
  auto scalar = missing_id; scalar["operations"][0]["feature"] = 5;
  error = fails("invalid_model", [&]{ service.call("cad_apply", scalar); });
  require(error.details.at("operation_index") == 0, "Non-object feature names its operation");
  service.call("cad_create",{{"document_id","asm"},{"model",assembly()}});
  auto malformed_parts = assembly().at("features")[1]; malformed_parts["parts"] = Json::array({5});
  const Json assembly_batch = {{"document_id","asm"},{"expected_revision",1},{"operations",Json::array({
    {{"op","replace_feature"},{"id","fixture"},{"feature",malformed_parts}},
    {{"op","set_part_placement"},{"assembly_id","fixture"},{"part_id","p1"},{"placement",Json::object()}}})}};
  error = fails("invalid_argument", [&]{ service.call("cad_apply", assembly_batch); });
  require(error.details.at("operation_index") == 1, "Malformed assembly parts fail at the dependent operation");
  require(head(service) == 1, "Rejected batches preserve HEAD");

  // Every adapter receives the code Service::call produces.
  const auto step = temp.path / "latin1.step";
  atomic_text(step, std::string("ISO-10303-21;\n/* caf\xE9 */\nEND-ISO-10303-21;\n"));
  const std::vector<std::tuple<std::string, Json, std::string>> cases = {
    {"cad_apply", missing_id, "invalid_model"},
    {"cad_apply", scalar, "invalid_model"},
    {"cad_create", {{"document_id","CON"},{"model",box()}}, "invalid_argument"},
    {"cad_import", {{"document_id","imported"},{"path",path_to_utf8(step)}}, "invalid_argument"}};
  McpSession session(service);
  session.handle({{"jsonrpc","2.0"},{"id",1},{"method","initialize"},{"params",{{"protocolVersion","2025-11-25"},
    {"capabilities",Json::object()},{"clientInfo",{{"name","service-test"},{"version","1"}}}}}});
  session.handle({{"jsonrpc","2.0"},{"method","notifications/initialized"}});
  int id = 2;
  for (const auto& [tool, arguments, code] : cases) {
    const auto direct = fails(code, [&]{ service.call(tool, arguments); });
    const auto reply = session.handle({{"jsonrpc","2.0"},{"id",id++},{"method","tools/call"},{"params",{{"name",tool},{"arguments",arguments}}}});
    require(reply && reply->at("result").at("isError") == true, "MCP reports a tool error for " + tool);
    const auto mcp = reply->at("result").at("structuredContent").at("error").at("code").get<std::string>();
    const auto cli = cli_error(temp.path, tool, arguments);
    require(mcp == direct.code && cli == direct.code, tool + " codes agree: service " + direct.code + ", MCP " + mcp + ", CLI " + cli);
  }
  require(head(service) == 1, "Adapter failures preserve HEAD");
}

void utf8_tests() {
  require(!invalid_utf8_offset("ISO-10303-21;"), "ASCII is UTF-8");
  require(!invalid_utf8_offset("caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80 \xF4\x8F\xBF\xBF"), "Multibyte UTF-8 accepted");
  require(invalid_utf8_offset("ab\xC3\x28") == 2u, "Bad continuation offset");
  require(invalid_utf8_offset("\xC0\xAF") == 0u, "Overlong rejected");
  require(invalid_utf8_offset("\xE0\x80\xAF") == 0u, "Three-byte overlong rejected");
  require(invalid_utf8_offset("x\xED\xA0\x80") == 1u, "Surrogate rejected");
  require(invalid_utf8_offset("\xF4\x90\x80\x80") == 0u, "Beyond U+10FFFF rejected");
  require(invalid_utf8_offset("abc\xE2\x82") == 3u, "Truncated sequence rejected");
  require(invalid_utf8_offset("\x80") == 0u && invalid_utf8_offset("ok\xFF") == 2u, "Invalid lead bytes rejected");
  Temp temp; Service service(temp.path);
  const std::string content = "ISO-10303-21;\n/* caf\xE9 */\nEND-ISO-10303-21;\n";
  atomic_text(temp.path / "latin1.step", content);
  const auto error = fails("invalid_argument", [&]{
    service.call("cad_import",{{"document_id","imported"},{"path",path_to_utf8(temp.path / "latin1.step")},{"request_id","importOnce"}}); });
  require(error.details.at("byte_offset") == content.find('\xE9'), "Import names the first invalid byte");
  require(std::string(error.what()).find("UTF-8") != std::string::npos, "Import error explains the encoding rule");
  fails("not_found", [&]{ service.call("cad_read",{{"document_id","imported"}}); });
}

void identifier_tests() {
  for (const auto* reserved : {"con","CON","Con","prn","PRN","aux","AuX","nul","NUL","com0","com1","COM9","Com5","lpt0","lpt1","LPT9","lPt4"})
    fails("invalid_argument", [&]{ identifier(reserved); });
  for (const auto* portable : {"console","com","com10","lpt","lpt12","nul_part","auxiliary","comA","conX","prn-1","part"})
    identifier(portable);
  Temp temp; Service service(temp.path);
  const auto error = fails("invalid_argument", [&]{ service.call("cad_create",{{"document_id","Aux"},{"model",box()}}); });
  require(std::string(error.what()).find("reserved") != std::string::npos, "Reserved-name error is explicit");
  require(!fs::exists(temp.path / ".locks" / "Aux.lock") && !fs::exists(temp.path / "documents" / "Aux"), "Reserved document IDs create no files");
  fails("invalid_argument", [&]{ service.call("cad_create",{{"document_id","part"},{"model",box()},{"request_id","nul"}}); });
  fails("not_found", [&]{ service.call("cad_read",{{"document_id","part"}}); });
  fails("invalid_argument", [&]{ dispatch_job(temp.path,{{"action","get"},{"job_id","LPT1"}}); });
  // Names inside a model never become file names, so existing documents that
  // use them stay valid and addressable.
  for (const auto* name : {"con","AUX","nul","com1"}) model_identifier(name);
  Json internal = {{"schema_version",1},{"units","mm"},{"parameters",{{"con",4}}},
    {"features",Json::array({{{"id","aux"},{"type","box"},{"size",Json::array({Json{{"parameter","con"}},3,2})}},
      {{"id","nul"},{"type","assembly"},{"parts",Json::array({{{"id","com1"},{"input","aux"}}})}}})},{"output","nul"}};
  require(service.call("cad_create",{{"document_id","part"},{"model",internal}}).at("revision") == 1, "Model-internal device names are valid");
  require(service.call("cad_query",{{"document_id","part"},{"revision",1},{"feature_id","aux"}}).at("feature_id") == "aux", "Model-internal names are addressable");
  require(service.call("cad_apply",{{"document_id","part"},{"expected_revision",1},
    {"operations",Json::array({{{"op","set_parameter"},{"name","con"},{"value",5}}})}}).at("revision") == 2, "Model-internal names are editable");
}
}

int main(int argc, char** argv) {
  try {
    configure_kernel_logging();
    set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));
    // Optional section name for focused debugging; ctest runs every section.
    const std::string only = argc > 1 ? argv[1] : "";
    const std::vector<std::pair<std::string, void(*)()>> sections = {
      {"identifiers", identifier_tests}, {"utf8", utf8_tests}, {"locks", lock_wait_tests},
      {"receipts", receipt_tests}, {"errors", error_mapping_tests}, {"publication", publication_wait_tests}};
    for (const auto& [name, run] : sections) if (only.empty() || only == name) run();
    std::cout << "service: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "FAILED: " << e.what() << '\n'; return 1; }
}
