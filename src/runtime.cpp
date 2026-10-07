#include "agentcad/runtime.hpp"
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <stdexcept>
#include <system_error>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <pwd.h>
#include <unistd.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace agentcad {
std::filesystem::path workspace_in_documents(const std::filesystem::path& documents) {
  if (!documents.is_absolute()) throw std::runtime_error("Cannot locate the user's Documents folder");
  return documents / "Agent CAD";
}
std::filesystem::path default_workspace() {
#ifdef _WIN32
  PWSTR documents = nullptr;
  const auto status = SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DONT_VERIFY, nullptr, &documents);
  if (FAILED(status)) { CoTaskMemFree(documents); throw std::runtime_error("Cannot locate the user's Documents folder"); }
  const std::filesystem::path result(documents);
  CoTaskMemFree(documents);
  return workspace_in_documents(result);
#else
  std::filesystem::path home;
  if (const auto* value = std::getenv("HOME")) home = value;
  if (!home.is_absolute()) {
    // GUI-launched plugins may not inherit shell environment settings.
    const auto size = ::sysconf(_SC_GETPW_R_SIZE_MAX);
    std::vector<char> buffer(static_cast<std::size_t>(size > 0 ? size : 16384));
    struct passwd entry{}; struct passwd* found = nullptr;
    if (::getpwuid_r(::getuid(), &entry, buffer.data(), buffer.size(), &found) == 0 && found && found->pw_dir)
      home = found->pw_dir;
  }
  return workspace_in_documents(home / "Documents");
#endif
}
std::filesystem::path plugin_workspace_setting(const std::filesystem::path& setting) {
  // Claude leaves an optional empty user_config substitution literally unset.
  // Interpret only that host marker; never expand arbitrary path templates.
  if (setting.empty() || setting == "${user_config.workspace}") return default_workspace();
  if (!setting.is_absolute()) throw std::runtime_error("Plugin project folder must be an absolute path");
  return setting;
}
namespace {
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

ExecutableLocation resolve_proc_self_exe(const std::string& link) {
  constexpr std::string_view deleted = " (deleted)";
  if (link.size() > deleted.size() && link.compare(link.size() - deleted.size(), deleted.size(), deleted) == 0)
    return {link.substr(0, link.size() - deleted.size()), "/proc/self/exe"};
  return {link, link};
}

ExecutableLocation current_executable() {
#ifdef _WIN32
  // A running Windows image cannot be deleted or replaced at its path.
  std::vector<wchar_t> buffer(32768);
  const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size()) return {};
  const std::filesystem::path path(std::wstring(buffer.data(), length));
  return {path, path};
#elif defined(__APPLE__)
  std::uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buffer(size);
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
  std::error_code error;
  auto path = std::filesystem::weakly_canonical(buffer.data(), error);
  if (error) path = std::filesystem::absolute(buffer.data(), error);
  if (error) return {};
  return {path, path};
#else
  std::vector<char> buffer(32768);
  const auto length = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
  if (length <= 0 || static_cast<std::size_t>(length) == buffer.size()) return {};
  return resolve_proc_self_exe(std::string(buffer.data(), static_cast<std::size_t>(length)));
#endif
}

void configure_runtime() {
  const auto executable = current_executable().path;
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
