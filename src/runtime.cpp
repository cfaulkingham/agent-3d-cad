#include "agentcad/runtime.hpp"
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace agentcad {
namespace {
std::filesystem::path executable_path() {
#ifdef _WIN32
  std::vector<wchar_t> buffer(32768);
  const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size()) return {};
  return std::filesystem::path(std::wstring(buffer.data(), length));
#elif defined(__APPLE__)
  std::uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buffer(size);
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
  return std::filesystem::weakly_canonical(buffer.data());
#else
  std::vector<char> buffer(32768);
  const auto length = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
  if (length <= 0 || static_cast<std::size_t>(length) == buffer.size()) return {};
  return std::string(buffer.data(), static_cast<std::size_t>(length));
#endif
}
void default_environment(const char* name, const std::filesystem::path& path) {
  if (std::getenv(name) || !std::filesystem::exists(path)) return;
#ifdef _WIN32
  const std::string narrow_name(name);
  const std::wstring wide_name(narrow_name.begin(), narrow_name.end());
  _wputenv_s(wide_name.c_str(), path.c_str());
#else
  ::setenv(name, path.c_str(), 0);
#endif
}
}
void configure_runtime() {
  const auto executable = executable_path();
  if (executable.empty()) return;
  const auto root = executable.parent_path().parent_path() / "share" / "agent-3d-cad" / "occt";
  if (!std::filesystem::is_directory(root)) return;
  default_environment("CSF_SHMessage", root / "SHMessage");
  default_environment("CSF_XSMessage", root / "XSMessage");
  default_environment("CSF_STEPDefaults", root / "XSTEPResource");
  default_environment("CSF_IGESDefaults", root / "XSTEPResource");
  default_environment("CSF_StandardDefaults", root / "StdResource");
  default_environment("CSF_PluginDefaults", root / "StdResource");
  default_environment("CSF_XCAFDefaults", root / "StdResource");
}
}
