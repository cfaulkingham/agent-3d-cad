#include "agentcad/client_config.hpp"
#include "agentcad/runtime.hpp"
#include <iostream>

namespace agentcad {
void print_client_config(const std::string& client, const fs::path& workspace) {
  const auto exe = current_executable().path;
  if (exe.empty()) throw Error("not_found", "Cannot locate the installed executable");
  const auto path = path_to_utf8(fs::absolute(workspace).lexically_normal());
  const auto command = path_to_utf8(exe);
  const Json args = {"serve", "--workspace", path};
  if (client == "claude")
    std::cout << Json{{"mcpServers", {{"agent-3d-cad", {{"command", command}, {"args", args}}}}}}.dump(2) << '\n';
  else if (client == "opencode")
    std::cout << Json{{"$schema", "https://opencode.ai/config.json"}, {"mcp", {{"agent-3d-cad", {
      {"type", "local"}, {"command", Json::array({command, "serve", "--workspace", path})}, {"enabled", true}}}}}}.dump(2) << '\n';
  else if (client == "codex") {
    // JSON's escaped basic strings/arrays are also valid TOML for UTF-8 paths.
    std::cout << "[mcp_servers.agent-3d-cad]\ncommand = " << Json(command).dump()
              << "\nargs = " << args.dump() << '\n';
  } else throw Error("usage", "Choose --client claude, opencode or codex");
}
}
