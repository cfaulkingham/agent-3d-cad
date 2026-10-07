#include "agentcad/mcp.hpp"
#include "agentcad/app.hpp"
#include <set>

namespace agentcad {
namespace {
Json rpc_error(const Json& id, int code, const std::string& message) {
  return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}
Json tool_result(const Json& value, bool error = false) {
  return {{"content", Json::array({{{"type", "text"}, {"text", value.dump()}}})},
          {"structuredContent", value}, {"isError", error}};
}
Json app_metadata() {
  return {{"ui", {{"csp", {{"connectDomains", Json::array()}, {"resourceDomains", Json::array()},
                            {"frameDomains", Json::array()}, {"baseUriDomains", Json::array()}}},
                  {"permissions", {{"clipboardWrite", Json::object()}}},
                  {"prefersBorder", true}}}};
}
Json app_resource() {
  return {{"uri", viewer_app_uri}, {"name", "CAD Viewer"},
          {"description", "Interactive CAD workspace with durable model state and selections"},
          {"mimeType", viewer_app_mime}, {"_meta", app_metadata()}};
}
// The catalog is constant for the process: build it once, not per tools/call.
const Json& published_tools() {
  static const Json tools = tool_definitions();
  return tools;
}
const std::set<std::string>& published_tool_names() {
  static const auto names = [] {
    std::set<std::string> result;
    for (const auto& tool : published_tools()) result.insert(tool.at("name").get<std::string>());
    return result;
  }();
  return names;
}
}

std::optional<Json> McpSession::handle(const Json& request) {
  // JSON-RPC forbids replying to a response. This server sends no requests, so
  // a client result/error object is unsolicited and is dropped silently.
  if (request.is_object() && !request.contains("method") && (request.contains("result") || request.contains("error")))
    return std::nullopt;
  if (!request.is_object() || request.value("jsonrpc", Json()) != "2.0" || !request.contains("method") || !request.at("method").is_string())
    return rpc_error(nullptr, -32600, "Invalid JSON-RPC request");
  const auto method = request.at("method").get<std::string>();
  const bool notification = !request.contains("id");
  const auto id = notification ? Json(nullptr) : request.at("id");
  if (!notification && !id.is_string() && !id.is_number_integer())
    return rpc_error(nullptr, -32600, "Request id must be a string or integer");
  const auto params = request.value("params", Json::object());
  if (!params.is_object()) {
    if (notification) return std::nullopt;
    return rpc_error(id, -32602, "params must be an object");
  }
  if (notification) {
    if (method == "notifications/initialized" && state_ == State::initializing) state_ = State::ready;
    // A tools/call notification must never execute a mutating tool.
    return std::nullopt;
  }
  auto response = [&](const Json& result) { return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}; };
  try {
    if (method == "ping") return response(Json::object());
    if (method == "initialize") {
      if (state_ != State::fresh) return rpc_error(id, -32600, "Session already initialized");
      fields(params, {"protocolVersion", "capabilities", "clientInfo"}, {"_meta"});
      text_field(params, "protocolVersion");
      if (!params.at("capabilities").is_object() || !params.at("clientInfo").is_object())
        return rpc_error(id, -32602, "Invalid client capabilities or clientInfo");
      text_field(params.at("clientInfo"), "name"); text_field(params.at("clientInfo"), "version");
      state_ = State::initializing;
      return response({{"protocolVersion", "2025-11-25"},
        {"capabilities", {{"tools", {{"listChanged", false}}},
                          {"resources", {{"subscribe", false}, {"listChanged", false}}},
                          {"extensions", {{"io.modelcontextprotocol/ui", {{"mimeTypes", Json::array({viewer_app_mime})}}}}}}},
        {"serverInfo", {{"name", "agent-3d-cad"}, {"version", "0.1.0"}}},
        {"instructions", "Use cad_open to open the integrated CAD viewer; use cad_show to update that view without opening another. Read cad_context for the active model and selected geometry. Use cad_read before edits; cad_apply requires expected_revision. Model and export units are millimeters. Use cad_job for responsive asynchronous geometry work with durable request IDs. cad_view saves an offline selectable viewer. cad_drawing exports native PDF/SVG sheets and per-view 1:1 mm DXFs from a committed revision; retain its recipe to regenerate after edits. Pick references are evaluation-scoped; use geometric selectors for saved design intent. Tools also return JSON text for hosts without MCP Apps. See docs/PROTOCOL.md for model schemas."}});
    }
    if (state_ != State::ready) return rpc_error(id, -32000, "Initialize and send notifications/initialized first");
    if (method == "resources/list") {
      fields(params, {}, {"cursor", "_meta"});
      if (params.contains("cursor")) return rpc_error(id, -32602, "This server has no pagination cursor");
      return response({{"resources", Json::array({app_resource()})}});
    }
    if (method == "resources/read") {
      fields(params, {"uri"}, {"_meta"});
      const auto uri = text_field(params, "uri");
      if (uri != viewer_app_uri) return rpc_error(id, -32002, "Resource not found: " + uri);
      return response({{"contents", Json::array({{{"uri", viewer_app_uri}, {"mimeType", viewer_app_mime},
                                                {"text", viewer_app_html()}, {"_meta", app_metadata()}}})}});
    }
    if (method == "tools/list") {
      fields(params, {}, {"cursor", "_meta"});
      if (params.contains("cursor")) return rpc_error(id, -32602, "This server has no pagination cursor");
      return response({{"tools", published_tools()}});
    }
    if (method == "tools/call") {
      fields(params, {"name"}, {"arguments", "_meta"});
      const auto name = text_field(params, "name");
      if (!published_tool_names().contains(name))
        return rpc_error(id, -32602, "Unknown tool: " + name);
      try {
        return response(tool_result(service_.call(name, params.value("arguments", Json::object()))));
      } catch (const Error& e) {
        return response(tool_result({{"error", e.json()}}, true));
      } catch (const std::exception& e) {
        // Service::call already translated its exceptions (service_error); what
        // remains here, e.g. serializing the reply, is internal as in the CLI.
        return response(tool_result({{"error", Error("internal_error", e.what()).json()}}, true));
      }
    }
    return rpc_error(id, -32601, "Method not found");
  } catch (const Error& e) { return rpc_error(id, -32602, e.what()); }
  catch (const Json::exception& e) { return rpc_error(id, -32602, e.what()); }
}

void serve(Service& service, std::istream& input, std::ostream& output) {
  McpSession session(service);
  std::string line;
  bool oversized = false;
  auto reply = [&](const Json& message) { output << message.dump() << '\n'; output.flush(); };
  auto dispatch = [&] {
    // A CRLF delimiter's carriage return is framing, not message content.
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (oversized || line.size() > max_json_bytes) reply(rpc_error(nullptr, -32600, "Message exceeds 1 MiB"));
    // Blank keep-alive lines carry no message, so they never get an error reply.
    else if (line.find_first_not_of(" \t\r") != std::string::npos) {
      try {
        if (auto response = session.handle(parse_json(line))) reply(*response);
      } catch (const Error& e) { reply(rpc_error(nullptr, -32700, e.what())); }
      catch (const std::exception& e) { reply(rpc_error(nullptr, -32603, e.what())); }
    }
    line.clear(); oversized = false;
  };
  char c;
  while (input.get(c)) {
    if (c == '\n') dispatch();
    else if (!oversized) {
      // One extra byte leaves room for a CRLF carriage return on a maximal frame.
      if (line.size() > max_json_bytes) { oversized = true; line.clear(); line.shrink_to_fit(); }
      else line.push_back(c);
    }
  }
  // Accept a final JSON message at EOF even without a trailing newline.
  if (!line.empty() || oversized) dispatch();
}
}
