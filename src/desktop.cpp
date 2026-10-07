#include "agentcad/desktop.hpp"
#include "agentcad/runtime.hpp"
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>
extern char** environ;
#endif

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

Json launch_desktop(const fs::path& workspace, const std::string& view_id) {
  const auto root = current_executable().path.parent_path().parent_path();
#ifdef __APPLE__
  const auto desktop = root / "desktop/Agent CAD.app/Contents/MacOS/agent-cad-viewer";
#elif defined(_WIN32)
  const auto desktop = root / "desktop/agent-cad-viewer.exe";
#else
  const auto desktop = root / "desktop/agent-cad-viewer";
#endif
  if (!fs::is_regular_file(desktop)) throw Error("viewer_not_installed",
    "Install the desktop archive to use the standalone viewer. The core archive supports cad_view offline HTML.",
    {{"expected_path", path_to_utf8(desktop)}});
  const auto absolute = fs::absolute(workspace).lexically_normal();
#ifdef _WIN32
  const auto quote = [](const std::wstring& s) {
    std::wstring result = L"\""; std::size_t slashes = 0;
    for (auto c : s) {
      if (c == L'\\') { ++slashes; continue; }
      result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\'); slashes = 0;
      result += c;
    }
    result.append(slashes * 2, L'\\'); return result + L'"';
  };
  auto command = quote(desktop.wstring()) + L" --workspace " + quote(absolute.wstring()) +
    L" --view " + quote(path_from_utf8(view_id).wstring());
  STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
  if (!CreateProcessW(desktop.c_str(), command.data(), nullptr, nullptr, FALSE,
                      CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS, nullptr, nullptr, &startup, &process))
    throw Error("viewer_launch_failed", "Could not start the desktop viewer", {{"os_error", GetLastError()}});
  CloseHandle(process.hThread); CloseHandle(process.hProcess);
#else
  std::string command = path_to_utf8(desktop), location = path_to_utf8(absolute), view = view_id;
  char* args[] = {command.data(), const_cast<char*>("--workspace"), location.data(), const_cast<char*>("--view"), view.data(), nullptr};
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) throw Error("viewer_launch_failed", "Cannot initialize viewer process");
  int status = 0;
  for (int fd = 0; fd < 3 && !status; ++fd)
    status = posix_spawn_file_actions_addopen(&actions, fd, "/dev/null", fd == 0 ? O_RDONLY : O_WRONLY, 0);
  pid_t pid = 0;
  if (!status) status = posix_spawn(&pid, command.c_str(), &actions, nullptr, args, environ);
  posix_spawn_file_actions_destroy(&actions);
  if (status) throw Error("viewer_launch_failed", "Could not start the desktop viewer", {{"os_error", status}});
#endif
  return {{"launched", true}, {"workspace", path_to_utf8(absolute)}, {"view_id", view_id}};
}
}
