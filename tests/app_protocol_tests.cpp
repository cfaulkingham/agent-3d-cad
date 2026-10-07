#include "agentcad/app.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/mcp.hpp"
#include <chrono>
#include <iostream>
#include <set>
#include <sstream>
#include <vector>

using namespace agentcad;
namespace {
int checks = 0;
void require(bool condition, const std::string& message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
struct Temporary {
  fs::path path = fs::temp_directory_path() / ("agentcad-app-protocol-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  Temporary() { fs::create_directory(path); }
  ~Temporary() { std::error_code ec; fs::remove_all(path, ec); }
};
Json rpc(const Json& id, const std::string& method, const Json& params = Json::object()) {
  return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}
Json initialize(bool apps) {
  Json capabilities = Json::object();
  if (apps) capabilities["extensions"]["io.modelcontextprotocol/ui"]["mimeTypes"] = Json::array({viewer_app_mime});
  return rpc("initialize", "initialize", {{"protocolVersion", "2025-11-25"},
      {"capabilities", capabilities}, {"clientInfo", {{"name", "app-protocol-test"}, {"version", "1"}}}});
}
Json result(McpSession& session, const Json& request) {
  const auto reply = session.handle(request);
  require(reply.has_value() && reply->contains("result"), "Expected successful response: " + request.dump());
  require(reply->at("id") == request.at("id"), "Response preserves request correlation");
  return reply->at("result");
}
void error(McpSession& session, const Json& request, int code) {
  const auto reply = session.handle(request);
  require(reply.has_value() && reply->contains("error") && reply->at("error").at("code") == code,
          "Expected protocol error " + std::to_string(code) + " for " + request.dump());
}
void source_integrity() {
  const auto source = path_from_utf8(CAD_SOURCE_DIR);
  auto expected = read_text(source / "web/viewer.html");
  for (const auto& [token, asset] : {
       std::pair{"@VIEWER_STYLES@", "styles.css"}, {"@VIEWER_BRIDGE@", "bridge.js"},
       {"@VIEWER_RENDERER@", "renderer.js"}, {"@VIEWER_STATE@", "state.js"}, {"@VIEWER_APP@", "app.js"}}) {
    const auto index = expected.find(token);
    require(index != std::string::npos, std::string("Missing HTML asset token: ") + token);
    expected.replace(index, std::char_traits<char>::length(token), read_text(source / "web" / asset));
  }
  // CMake text reads use canonical LF even in a Windows CRLF checkout.
  for (auto pos = expected.find("\r\n"); pos != std::string::npos; pos = expected.find("\r\n", pos))
    expected.erase(pos, 1);
  const auto& embedded = viewer_app_html();
  std::size_t first_difference = 0;
  while (first_difference < embedded.size() && first_difference < expected.size() &&
         embedded[first_difference] == expected[first_difference]) ++first_difference;
  require(embedded == expected, "Embedded HTML exactly matches reviewed source assets (actual " +
      std::to_string(embedded.size()) + ", expected " + std::to_string(expected.size()) +
      " bytes; first difference " + std::to_string(first_difference) + ")");
  require(viewer_app_html().find("@VIEWER_") == std::string::npos, "No unexpanded asset placeholders");
  require(viewer_app_html().find("<canvas") != std::string::npos, "Resource contains the CAD canvas");
  require(viewer_app_html().find("CadBridge") != std::string::npos &&
          viewer_app_html().find("CadLiveState") != std::string::npos, "Bridge and state controller are embedded");
  require(viewer_app_html().find("<script src=") == std::string::npos &&
          viewer_app_html().find("<link rel=\"stylesheet\"") == std::string::npos,
          "Resource does not require external scripts or stylesheets");
}
void session_contract(Service& service, bool apps) {
  McpSession session(service);
  error(session, rpc(1, "resources/list"), -32000);
  error(session, rpc(2, "resources/read", {{"uri", viewer_app_uri}}), -32000);
  const auto initialized = result(session, initialize(apps));
  require(initialized.at("capabilities").at("resources").at("subscribe") == false,
          "Static resource capability does not advertise subscriptions");
  require(initialized.at("capabilities").at("extensions").at("io.modelcontextprotocol/ui").at("mimeTypes") == Json::array({viewer_app_mime}),
          "Apps extension advertises HTML MIME type");
  error(session, rpc(3, "resources/list"), -32000);
  require(!session.handle({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}), "Lifecycle notification has no reply");
  const auto listed = result(session, rpc(4, "resources/list"));
  require(listed.at("resources").size() == 1 && !listed.contains("nextCursor"), "One static app resource without pagination");
  const auto resource = listed.at("resources").at(0);
  require(resource.at("uri") == viewer_app_uri && resource.at("mimeType") == viewer_app_mime,
          "Resource discovery URI and MIME agree");
  const auto read = result(session, rpc("read-app", "resources/read", {{"uri", viewer_app_uri}}));
  const auto content = read.at("contents").at(0);
  require(read.at("contents").size() == 1 && content.at("text") == viewer_app_html(), "Read returns compiled HTML content");
  require(content.at("uri") == resource.at("uri") && content.at("mimeType") == resource.at("mimeType"), "Read and discovery identify the same app");
  require(content.at("_meta") == resource.at("_meta"), "Read includes the resource security metadata");
  require(content.at("_meta").at("ui").at("permissions") == Json{{"clipboardWrite", Json::object()}},
          "Only clipboard permission is requested for user-initiated copy fallback");
  for (const auto* field : {"connectDomains", "resourceDomains", "frameDomains", "baseUriDomains"})
    require(content.at("_meta").at("ui").at("csp").at(field) == Json::array(), std::string("No external domains for ") + field);
  for (const auto* uri : {"file:///etc/passwd", "ui://agent-3d-cad/../viewer.html", "ui://agent-3d-cad/viewer.html?x=1", "ui://other/viewer.html"})
    error(session, rpc(5, "resources/read", {{"uri", uri}}), -32002);
  error(session, rpc(6, "resources/read"), -32602);
  error(session, rpc(7, "resources/read", {{"uri", 1}}), -32602);
  error(session, rpc(8, "resources/read", {{"uri", viewer_app_uri}, {"path", "arbitrary"}}), -32602);
  error(session, rpc(9, "resources/list", {{"cursor", "arbitrary"}}), -32602);
  require(!session.handle({{"jsonrpc", "2.0"}, {"method", "resources/read"}, {"params", {{"uri", viewer_app_uri}}}}), "Resource notifications produce no response");
  Json tools;
  const auto discovered = result(session, rpc(10, "tools/list"));
  for (const auto& tool : discovered.at("tools")) tools[tool.at("name").get<std::string>()] = tool;
  require(tools.at("cad_open").at("_meta").at("ui").at("resourceUri") == viewer_app_uri,
          "Open links the app resource");
  require(!tools.at("cad_show").contains("_meta") || !tools.at("cad_show").at("_meta").value("ui", Json::object()).contains("resourceUri"),
          "Show does not instantiate a second app");
  require(tools.at("cad_viewer").at("_meta").at("ui").at("visibility") == Json::array({"app"}), "Viewer internals have app-only host visibility");
  const auto invalid = result(session, rpc(11, "tools/call", {{"name", "cad_viewer"}, {"arguments", {{"invalid", true}}}}));
  require(invalid.at("isError") == true && invalid.at("structuredContent").contains("error"), "App tool validation errors remain structured tool results");
  require(parse_json(invalid.at("content").at(0).at("text").get<std::string>()) == invalid.at("structuredContent"), "App errors preserve text-only fallback");
  require(!session.handle({{"jsonrpc", "2.0"}, {"id", 41}, {"result", Json::object()}}), "A client response is never answered");
  require(!session.handle({{"jsonrpc", "2.0"}, {"id", nullptr}, {"error", {{"code", -32600}, {"message", "x"}}}}), "A client error response is never answered");
  error(session, {{"jsonrpc", "2.0"}, {"id", 42}}, -32600);
}
// Collects "#/$defs/<name>" targets, ignoring the schema's own $defs map.
void references(const Json& value, std::set<std::string>& names) {
  if (value.is_array()) for (const auto& item : value) references(item, names);
  if (!value.is_object()) return;
  for (const auto& [key, item] : value.items()) {
    if (key == "$ref") {
      const auto target = item.get<std::string>();
      require(target.rfind("#/$defs/", 0) == 0, "Schema references stay inside the standalone schema: " + target);
      names.insert(target.substr(8));
    } else if (key != "$defs") references(item, names);
  }
}
void discovery_contract(Service& service) {
  McpSession session(service);
  result(session, initialize(true));
  session.handle({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
  const auto listed = result(session, rpc("list", "tools/list"));
  const auto bytes = listed.dump().size();
  // 1,058,723 compact bytes when every schema carried all model $defs.
  require(bytes < 420 * 1024, "tools/list stays compact (" + std::to_string(bytes) + " bytes)");
  for (const auto& tool : listed.at("tools")) for (const auto* key : {"inputSchema", "outputSchema"}) {
    const auto& schema = tool.at(key);
    const auto name = tool.at("name").get<std::string>() + "." + key;
    std::set<std::string> reachable, pending;
    references(schema, pending);
    while (!pending.empty()) {
      const auto next = *pending.begin(); pending.erase(pending.begin());
      if (!reachable.insert(next).second) continue;
      require(schema.contains("$defs") && schema.at("$defs").contains(next), name + " resolves #/$defs/" + next);
      references(schema.at("$defs").at(next), pending);
    }
    std::set<std::string> attached;
    const auto definitions = schema.value("$defs", Json::object());
    for (const auto& [definition, unused] : definitions.items()) { (void)unused; attached.insert(definition); }
    require(attached == reachable, name + " carries only the definitions it references");
  }
  Json tools;
  for (const auto& tool : listed.at("tools")) tools[tool.at("name").get<std::string>()] = tool;
  require(!tools.at("cad_list").at("inputSchema").contains("$defs") && !tools.at("cad_list").at("outputSchema").contains("$defs"),
          "Reference-free schemas carry no definitions");
  require(tools.at("cad_create").at("inputSchema").at("$defs").contains("model") &&
          tools.at("cad_job").at("inputSchema").at("$defs").contains("operation"), "Nested references keep their definitions");
  require(listed.at("tools") == tool_definitions(), "MCP and CLI discovery publish the same definitions");
  // Dispatch checks a cached name set; it never rebuilds the tool catalog per call.
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 2000; ++i) error(session, rpc(i, "tools/call", {{"name", "cad_absent"}}), -32602);
  const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  require(elapsed < 2.0, "2000 tools/call dispatches complete without rebuilding definitions (" + std::to_string(elapsed) + " s)");
}
std::vector<Json> frames(Service& service, const std::string& input) {
  std::istringstream stream(input);
  std::ostringstream output;
  serve(service, stream, output);
  std::vector<Json> parsed;
  std::istringstream lines(output.str());
  std::string line;
  while (std::getline(lines, line)) parsed.push_back(parse_json(line));
  return parsed;
}
void framing_contract(Service& service) {
  const auto ping = [](const Json& id) { return rpc(id, "ping").dump(); };
  auto replies = frames(service, "\n   \n\r\n\t\r\n" + ping(1) + "\n\n");
  require(replies.size() == 1 && replies[0].at("id") == 1, "Blank and whitespace-only lines are skipped silently");
  replies = frames(service, initialize(true).dump() + "\r\n" + Json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}.dump() + "\r\n" + ping("crlf") + "\r\n");
  require(replies.size() == 2 && replies[0].at("id") == "initialize" && replies[1].at("id") == "crlf" && replies[1].contains("result"),
          "CRLF-delimited frames are handled like LF frames");
  replies = frames(service, Json{{"jsonrpc", "2.0"}, {"id", 7}, {"result", Json::object()}}.dump() + "\n" +
      Json{{"jsonrpc", "2.0"}, {"id", 8}, {"error", {{"code", -1}, {"message", "client failure"}}}}.dump() + "\n" + ping(9) + "\n");
  require(replies.size() == 1 && replies[0].at("id") == 9, "Stray client responses produce no reply");
  replies = frames(service, "{\"jsonrpc\":\n" + ping(10) + "\n");
  require(replies.size() == 2 && replies[0].at("id").is_null() && replies[0].at("error").at("code") == -32700 && replies[1].at("id") == 10,
          "Malformed JSON reports one parse error and the session continues");
  replies = frames(service, "[1]\n" + ping(11) + "\n");
  require(replies.size() == 2 && replies[0].at("error").at("code") == -32600 && replies[1].at("id") == 11, "Invalid requests still report -32600");
  replies = frames(service, std::string(max_json_bytes + 4096, ' ') + "{}\n" + ping(12) + "\n");
  require(replies.size() == 2 && replies[0].at("error").at("code") == -32600 && replies[1].at("id") == 12,
          "An oversized line is reported once and the session continues");
  replies = frames(service, ping(13) + "\n" + ping(14));
  require(replies.size() == 2 && replies[1].at("id") == 14, "A final frame without a newline is processed at EOF");
  replies = frames(service, ping(15) + "\n  \r");
  require(replies.size() == 1 && replies[0].at("id") == 15, "Trailing whitespace at EOF is not a frame");
}
}
int main() {
  try {
    set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));
    Temporary temporary;
    Service service(temporary.path);
    source_integrity();
    session_contract(service, true);
    session_contract(service, false);
    discovery_contract(service);
    framing_contract(service);
    std::istringstream input(initialize(true).dump() + "\n" + Json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}.dump() + "\n" +
        rpc(12, "resources/read", {{"uri", viewer_app_uri}}).dump() + "\n");
    std::ostringstream output;
    serve(service, input, output);
    std::istringstream frames(output.str());
    std::string line;
    require(static_cast<bool>(std::getline(frames, line)) && parse_json(line).at("id") == "initialize", "Stdio initialization frame");
    require(static_cast<bool>(std::getline(frames, line)) && parse_json(line).at("result").at("contents").at(0).at("text") == viewer_app_html(), "Multiline HTML is one JSON stdio frame");
    require(!std::getline(frames, line), "No additional resource stdout frames");
    std::cout << "app protocol: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "app protocol: " << e.what() << '\n'; return 1; }
}
