#include "agentcad/kernel.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/model.hpp"
#include "agentcad/mcp.hpp"
#include <STEPControl_Reader.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <numbers>
#include <sstream>
#include <random>
#include <thread>
#include <chrono>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace agentcad;
namespace {
int checks = 0;
void require(bool condition, const std::string& message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
void close_to(double actual, double expected, double tolerance = 1e-6) {
  require(std::abs(actual - expected) <= tolerance, "Measurement mismatch: " + std::to_string(actual) + " vs " + std::to_string(expected));
}
void error(const std::string& code, const std::function<void()>& function) {
  try { function(); } catch (const Error& e) { require(e.code == code, "Expected " + code + ", got " + e.code + ": " + e.what()); return; }
  throw std::runtime_error("Expected error: " + code);
}
struct Temporary {
  fs::path path;
  Temporary() {
    std::random_device random;
    for (int attempt = 0; attempt < 50; ++attempt) {
      const auto candidate = fs::temp_directory_path() / ("agentcad-test-" + std::to_string(random()) + "-" + std::to_string(random()));
      if (fs::create_directory(candidate)) { path = candidate; return; }
    }
    throw std::runtime_error("Unable to create unique test directory");
  }
  ~Temporary() { std::error_code unused; fs::remove_all(path, unused); }
};
Json box() {
  return {{"schema_version",1}, {"units","mm"}, {"parameters",{{"height",6}}},
    {"features", Json::array({{{"id","base"},{"type","box"},{"size",Json::array({20,10,Json{{"parameter","height"}}})}}})}, {"output","base"}};
}
Json edits(std::uint64_t revision, double height) {
  return {{"document_id","part"},{"expected_revision",revision},
    {"operations", Json::array({{{"op","set_parameter"},{"name","height"},{"value",height}}})}};
}
Json rpc(const Json& id, const std::string& method, Json params = Json::object()) {
  return {{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}};
}
Json initialize() {
  return rpc(1,"initialize",{{"protocolVersion","2025-11-25"},{"capabilities",Json::object()},
    {"clientInfo",{{"name","test"},{"version","1"}}}});
}

void model_tests() {
  validate_model(box());
  auto model = box(); model["unknown"] = 1;
  error("invalid_argument", [&]{validate_model(model);});
  model = box(); model["schema_version"] = 2;
  error("unsupported_schema", [&]{validate_model(model);});
  model = box(); model["features"][0]["size"][0] = -1;
  error("invalid_model", [&]{validate_model(model);});
  model = box(); model["features"].push_back(model["features"][0]);
  error("invalid_model", [&]{validate_model(model);});
  model = box(); model["parameters"].erase("height");
  error("invalid_model", [&]{validate_model(model);});
  model = box(); model["features"][0] = {{"id","future"},{"type","cut"},{"left","base"},{"right","base"}};
  error("invalid_model", [&]{validate_model(model);});
  model = box(); model["units"] = "in";
  error("invalid_model", [&]{validate_model(model);});
  error("invalid_argument", []{identifier("../outside");});
  error("invalid_argument", []{number(std::numeric_limits<double>::infinity());});
  error("invalid_argument", []{revision_number(1.5);});
  error("invalid_argument", []{revision_number(-1);});
  error("invalid_json", []{parse_json("{");});
  error("limit_exceeded", []{parse_json(std::string(max_json_bytes+1, ' '));});
  error("limit_exceeded", []{parse_json(std::string(80,'[')+"1"+std::string(80,']'));});
  const auto changed = apply_operations(box(), edits(1,8).at("operations"));
  require(changed.at("parameters").at("height") == 8, "parameter edit");
  const auto replacement = apply_operations(box(), Json::array({
    {{"op","add_feature"},{"feature",{{"id","second"},{"type","box"},{"size",{2,3,4}}}}},
    {{"op","set_output"},{"feature_id","second"}},
    {{"op","remove_feature"},{"id","base"}}
  }));
  require(replacement.at("features").size() == 1 && replacement.at("output") == "second", "batch final-state validation");
  error("invalid_argument", []{apply_operations(box(), Json::array({{{"op","replace_feature"},{"id","base"},{"feature",{{"id","renamed"}}}}}));});
}

void geometry_tests() {
  require(kernel_version() == "8.0.1", "Exact OCCT release");
  BuiltModel built(box());
  auto summary = built.summary();
  close_to(summary.at("volume_mm3"),1200);
  close_to(summary.at("area_mm2"),760);
  require(summary.at("solid_count") == 1 && summary.at("face_count") == 6, "box topology");
  close_to(summary.at("bounds_mm").at("max")[2],6);
  auto bored = box();
  bored["features"].push_back({{"id","hole"},{"type","cylinder"},{"radius",2},{"height",6},{"origin",{10,5,0}}});
  bored["features"].push_back({{"id","bored"},{"type","cut"},{"left","base"},{"right","hole"}});
  bored["output"] = "bored";
  BuiltModel bored_shape(bored);
  close_to(bored_shape.summary().at("volume_mm3"),1200-std::numbers::pi*4*6);
  auto merged = box();
  merged["features"].push_back({{"id","overlap"},{"type","box"},{"size",{20,10,6}},{"origin",{10,0,0}}});
  merged["features"].push_back({{"id","merged"},{"type","fuse"},{"left","base"},{"right","overlap"}});
  merged["output"] = "merged";
  close_to(BuiltModel(merged).summary().at("volume_mm3"),1800);
  auto empty = box();
  empty["features"].push_back({{"id","empty"},{"type","cut"},{"left","base"},{"right","base"}});
  empty["output"] = "empty";
  error("invalid_shape", [&]{BuiltModel unused(empty);});
  Temporary temporary;
  built.export_file(temporary.path / "box.step", "step");
  built.export_file(temporary.path / "box.stl", "stl");
  STEPControl_Reader reader;
  const auto step_path = (temporary.path / "box.step").u8string();
  require(reader.ReadFile(reinterpret_cast<const char*>(step_path.c_str())) == IFSelect_RetDone, "STEP parses");
  require(reader.TransferRoots() > 0, "STEP transfers");
  GProp_GProps props;
  BRepGProp::VolumeProperties(reader.OneShape(),props);
  close_to(props.Mass(),1200);
  require(fs::file_size(temporary.path / "box.stl") > 84, "STL has triangles");
  close_to(built.summary().at("volume_mm3"),1200);
  const auto fixture = parse_json(read_text(path_from_utf8(CAD_SOURCE_DIR) / "examples/plate.create.json"));
  const auto plate = BuiltModel(fixture.at("model")).summary();
  require(plate.at("solid_count") == 1 && plate.at("volume_mm3") > 22000 && plate.at("volume_mm3") < 24000, "filleted four-hole plate");
}

void transaction_tests() {
  Temporary temporary;
  Service service(temporary.path);
  auto created = service.call("cad_create",{{"document_id","part"},{"model",box()}});
  require(created.at("revision") == 1,"create revision");
  error("already_exists",[&]{service.call("cad_create",{{"document_id","part"},{"model",box()}});});
  error("invalid_model",[&]{service.call("cad_apply",edits(1,-5));});
  auto bad = edits(1,8);
  bad["operations"].push_back({{"op","add_feature"},{"feature",{{"id","broken"},{"type","fillet"},{"input","base"},{"radius",1000},{"edges","all"}}}});
  bad["operations"].push_back({{"op","set_output"},{"feature_id","broken"}});
  try { service.call("cad_apply",bad); throw std::runtime_error("Invalid fillet succeeded"); }
  catch (const Error& e) {
    require(e.code == "kernel_failure" || e.code == "invalid_shape", "kernel failure type");
    require(e.details.at("feature_id") == "broken", "failure identifies feature");
  }
  require(service.call("cad_read",{{"document_id","part"}}).at("revision") == 1,"failure preserves HEAD");
  require(!fs::exists(temporary.path / "documents/part/revisions/2.json"),"failure publishes no revision");
  // Three nested 64-copy patterns would request 262,144 solids. The per-feature
  // budget rejects the third level before building it, well inside the default
  // worker deadline and memory budget, and the committed revision is untouched.
  Json nested = {{"document_id","part"},{"expected_revision",1},{"operations",Json::array()}};
  std::string previous = "base";
  for (int level = 0; level < 3; ++level) {
    const auto id = "array" + std::to_string(level);
    Json step = {0,0,0}; step[level] = 30;
    nested["operations"].push_back({{"op","add_feature"},{"feature",{{"id",id},{"type","pattern"},{"input",previous},{"count",64},{"step",step}}}});
    previous = id;
  }
  nested["operations"].push_back({{"op","set_output"},{"feature_id",previous}});
  try { service.call("cad_apply",nested); throw std::runtime_error("Nested pattern budget was not enforced"); }
  catch (const Error& e) {
    require(e.code == "limit_exceeded" && e.details.at("feature_id") == "array2" && e.details.at("solids") == 262144,
      "nested pattern fails at its budget, got " + e.code + ": " + e.what());
  }
  require(service.call("cad_read",{{"document_id","part"}}).at("revision") == 1,"budget failure preserves HEAD");
  require(!fs::exists(temporary.path / "documents/part/revisions/2.json"),"budget failure publishes no revision");
  require(service.call("cad_apply",edits(1,8)).at("revision") == 2,"successful edit");
  error("revision_conflict",[&]{service.call("cad_apply",edits(1,9));});
  Service reopened(temporary.path);
  close_to(reopened.call("cad_query",{{"document_id","part"},{"revision",2}}).at("summary").at("volume_mm3"),1600);
  close_to(reopened.call("cad_query",{{"document_id","part"},{"revision",1}}).at("summary").at("volume_mm3"),1200);
  error("not_found",[&]{reopened.call("cad_read",{{"document_id","part"},{"revision",3}});});
  atomic_text(temporary.path / "documents/part/revisions/3.json", "{}");
  error("not_found",[&]{reopened.call("cad_read",{{"document_id","part"},{"revision",3}});});
  require(reopened.call("cad_apply",edits(2,9)).at("revision") == 3,"recovers orphan candidate");
  auto exported = reopened.call("cad_export",{{"document_id","part"},{"revision",1},{"format","step"}});
  require(fs::exists(exported.at("path").get<std::string>()),"revision export");
  fs::create_directory_symlink(temporary.path,temporary.path / "documents/link");
  error("storage_error",[&]{reopened.call("cad_read",{{"document_id","link"}});});
  // A separate process owning the writer lock cannot be raced by this process.
#ifdef _WIN32
  wchar_t executable[32768];
  require(GetModuleFileNameW(nullptr, executable, 32768) != 0, "test executable path");
  std::wstring command = L"\"" + std::wstring(executable) + L"\" --lock-child \"" + temporary.path.wstring() + L"\"";
  STARTUPINFOW startup{}; startup.cb = sizeof(startup);
  PROCESS_INFORMATION child{};
  require(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child) != 0, "start separate lock owner");
  CloseHandle(child.hThread);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!fs::exists(temporary.path / "lock-ready") && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  require(fs::exists(temporary.path / "lock-ready"), "child owns lock");
#else
  int ready[2], release[2];
  require(::pipe(ready) == 0 && ::pipe(release) == 0,"pipes");
  const auto child = ::fork();
  require(child >= 0,"fork");
  if (child == 0) {
    ::close(ready[0]); ::close(release[1]);
    try {
      WorkspaceLock lock(temporary.path);
      const char signal = 'x'; (void)::write(ready[1],&signal,1);
      char done; (void)::read(release[0],&done,1);
    } catch (...) { ::_exit(2); }
    ::_exit(0);
  }
  ::close(ready[1]); ::close(release[0]);
  char signal; require(::read(ready[0],&signal,1) == 1,"child owns lock");
#endif
  Service concurrent_reader(temporary.path);
  require(concurrent_reader.call("cad_read",{{"document_id","part"}}).at("revision") == 3,"new reader does not wait on writer");
  bool busy = false;
  try { service.call("cad_apply",edits(3,10)); } catch (const Error& e) { busy = e.code == "workspace_busy"; }
#ifdef _WIN32
  atomic_text(temporary.path / "lock-release", "release");
  const auto waited = WaitForSingleObject(child.hProcess, 10000);
  DWORD status = 1;
  GetExitCodeProcess(child.hProcess, &status);
  CloseHandle(child.hProcess);
  require(waited == WAIT_OBJECT_0 && status == 0 && busy, "cross-process lock exclusion");
#else
  (void)::write(release[1],"x",1);
  ::close(ready[0]); ::close(release[1]);
  int status; ::waitpid(child,&status,0);
  require(WIFEXITED(status) && WEXITSTATUS(status) == 0 && busy,"cross-process lock exclusion");
#endif
  require(service.call("cad_apply",edits(3,10)).at("revision") == 4,"lock released after child exits");
}

void protocol_tests() {
  Temporary temporary;
  Service service(temporary.path);
  McpSession session(service);
  require(session.handle(rpc(0,"tools/list"))->contains("error"),"requires initialization");
  auto init = initialize(); init["params"]["protocolVersion"] = "2099-01-01";
  require(session.handle(init)->at("result").at("protocolVersion") == "2025-11-25","version negotiation");
  require(session.handle(rpc(2,"tools/list"))->contains("error"),"requires initialized notification");
  require(!session.handle({{"jsonrpc","2.0"},{"method","notifications/initialized"}}),"notification has no response");
  const auto discovered=session.handle(rpc(3,"tools/list"))->at("result").at("tools");
  require(discovered.size()==tool_definitions().size() && discovered.size()>=10,"tool discovery includes implemented capabilities");
  auto call = rpc("create","tools/call",{{"name","cad_create"},{"arguments",{{"document_id","part"},{"model",box()}}}});
  auto notification = call; notification.erase("id");
  require(!session.handle(notification),"tools/call notification ignored");
  error("not_found",[&]{service.call("cad_read",{{"document_id","part"}});});
  require(session.handle(call)->at("result").at("structuredContent").at("revision") == 1,"MCP create");
  const auto invalid = session.handle(rpc(5,"tools/call",{{"name","cad_apply"},{"arguments",edits(1,-1)}}));
  require(invalid->at("result").at("isError") == true && !invalid->contains("error"),"tool errors are tool results");
  require(session.handle(rpc(6,"tools/call",{{"name","missing"}}))->at("error").at("code") == -32602,"unknown tool protocol error");
  require(session.handle(rpc(7,"missing"))->at("error").at("code") == -32601,"unknown method");
  require(session.handle(Json::array())->at("error").at("code") == -32600,"batch rejected");
  std::istringstream input("bad json\n" + initialize().dump() + "\n{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n" + rpc(8,"ping").dump());
  std::ostringstream output;
  serve(service,input,output);
  std::istringstream replies(output.str());
  std::string line;
  std::getline(replies,line); require(parse_json(line).at("error").at("code") == -32700,"parse error reply");
  std::getline(replies,line); require(parse_json(line).at("id") == 1,"stream recovers after invalid JSON");
  std::getline(replies,line); require(parse_json(line).at("id") == 8,"final unterminated frame");
  require(!std::getline(replies,line),"no extra stdout messages");
}
}

int run_tests(int argc,const char* const* argv) {
  configure_kernel_logging();
  try {
    set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));
#ifdef _WIN32
    if (argc == 3 && std::string(argv[1]) == "--lock-child") {
      const fs::path workspace = path_from_utf8(argv[2]);
      WorkspaceLock lock(workspace);
      atomic_text(workspace / "lock-ready", "ready");
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
      while (!fs::exists(workspace / "lock-release") && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      return fs::exists(workspace / "lock-release") ? 0 : 2;
    }
#endif
    if (argc != 2) throw std::runtime_error("Specify test suite");
    const std::string suite = argv[1];
    if (suite == "model") model_tests();
    else if (suite == "geometry") geometry_tests();
    else if (suite == "transactions") transaction_tests();
    else if (suite == "protocol") protocol_tests();
    else throw std::runtime_error("Unknown suite");
    std::cout << suite << ": " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "FAILED: " << e.what() << '\n'; return 1; }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  std::vector<std::string> text;
  for (int index = 0; index < argc; ++index) text.push_back(path_to_utf8(fs::path(argv[index])));
  std::vector<const char*> arguments;
  for (const auto& argument : text) arguments.push_back(argument.c_str());
  return run_tests(argc, arguments.data());
}
#else
int main(int argc, char** argv) { return run_tests(argc, argv); }
#endif
