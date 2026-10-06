#include "agentcad/kernel.hpp"
#include "agentcad/mcp.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/runtime.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace {
int run(int argc, const char* const* argv) {
  using namespace agentcad;
  try {
    configure_runtime();
    configure_kernel_logging();
    if (argc == 4 && std::string(argv[1]) == "--internal-geometry-worker")
      return geometry_worker_main(path_from_utf8(argv[2]), path_from_utf8(argv[3]));
    if (argc == 4 && std::string(argv[1]) == "--internal-job-worker")
      return job_worker_main(path_from_utf8(argv[2]), argv[3]);
    if (argc == 2 && std::string(argv[1]) == "--version") {
      std::cout << Json{{"name", "agent-3d-cad"}, {"version", "0.1.0"}, {"kernel", "OpenCascade"}, {"kernel_version", kernel_version()}}.dump() << '\n';
      return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "tools") { std::cout << tool_definitions().dump(2) << '\n'; return 0; }
    if (argc < 2) throw Error("usage", "Use: agent-3d-cad serve --workspace PATH | call TOOL --workspace PATH --input FILE | tools | --version");
    const std::string command = argv[1];
    std::string tool, workspace, input;
    int index = 2;
    if (command == "call" && index < argc) tool = argv[index++];
    if (command != "call" && command != "serve") throw Error("usage", "Unknown command: " + command);
    while (index < argc) {
      const std::string flag = argv[index++];
      if (index == argc) throw Error("usage", "Missing value for: " + flag);
      if (flag == "--workspace" && workspace.empty()) workspace = argv[index++];
      else if (flag == "--input" && input.empty() && command == "call") input = argv[index++];
      else throw Error("usage", "Unknown or repeated flag: " + flag);
    }
    if (workspace.empty() || (command == "call" && (tool.empty() || input.empty())))
      throw Error("usage", "An explicit --workspace and, for call, --input are required");
    Service service(path_from_utf8(workspace));
    if (command == "serve") serve(service, std::cin, std::cout);
    else {
      std::string content;
      if (input == "-") {
        char c;
        while (std::cin.get(c)) {
          if (content.size() >= max_json_bytes) throw Error("limit_exceeded", "Input exceeds 1 MiB");
          content.push_back(c);
        }
      } else content = read_text(path_from_utf8(input));
      std::cout << service.call(tool, parse_json(content)).dump(2) << '\n';
    }
    return 0;
  } catch (const Error& e) { std::cerr << Json{{"error", e.json()}}.dump() << '\n'; }
  catch (const std::exception& e) { std::cerr << Json{{"error", Error("internal_error", e.what()).json()}}.dump() << '\n'; }
  return 1;
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  try {
    std::vector<std::string> text;
    text.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index)
      text.push_back(agentcad::path_to_utf8(agentcad::fs::path(argv[index])));
    std::vector<const char*> arguments;
    arguments.reserve(text.size());
    for (const auto& argument : text) arguments.push_back(argument.c_str());
    return run(argc, arguments.data());
  } catch (const std::exception&) {
    std::cerr << "{\"error\":{\"code\":\"invalid_argument\",\"message\":\"Command-line arguments must be valid Unicode\",\"details\":{}}}\n";
    return 1;
  }
}
#else
int main(int argc, char** argv) { return run(argc, argv); }
#endif
