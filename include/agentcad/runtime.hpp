#pragma once
#include <filesystem>
#include <string>

namespace agentcad {
// Locate resources relative to the installed executable. Existing explicit
// resource settings are preserved for developer SDKs.
void configure_runtime();

// The running executable. `path` locates bundled resources; `image` is what an
// internal worker process must execute so it runs the same binary, whatever its
// file name. Both are empty when the operating system cannot report them.
struct ExecutableLocation {
  std::filesystem::path path;
  std::filesystem::path image;
};
ExecutableLocation current_executable();
// Linux /proc/self/exe link text. An unlinked or replaced (upgraded in place)
// image reads "<path> (deleted)": resources stay beside <path>, while the
// running image remains executable only through /proc/self/exe.
ExecutableLocation resolve_proc_self_exe(const std::string& link);
}
