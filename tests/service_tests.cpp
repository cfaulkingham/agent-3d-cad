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
      {"identifiers", identifier_tests}, {"utf8", utf8_tests}};
    for (const auto& [name, run] : sections) if (only.empty() || only == name) run();
    std::cout << "service: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "FAILED: " << e.what() << '\n'; return 1; }
}
