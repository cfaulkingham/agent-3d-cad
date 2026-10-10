#pragma once
#include "agentcad/json.hpp"
#include <chrono>
#include <filesystem>
#include <optional>

namespace agentcad {
namespace fs = std::filesystem;
// JSON and command-line text use UTF-8 on every platform. Native Windows paths
// use UTF-16 internally; never route them through the active ANSI code page.
fs::path path_from_utf8(const std::string& text);
std::string path_to_utf8(const fs::path& path);
// Serializes writers across CLI and MCP processes; released by the OS on exit.
class WorkspaceLock {
public:
  explicit WorkspaceLock(const fs::path& root);
  ~WorkspaceLock();
  WorkspaceLock(const WorkspaceLock&) = delete;
  WorkspaceLock& operator=(const WorkspaceLock&) = delete;
private:
#ifdef _WIN32
  void* handle_ = nullptr;
#else
  int fd_ = -1;
#endif
};

// Lock policy. LockWait::none fails immediately with workspace_busy (viewer,
// job admission and other callers with their own retry). LockWait::publication
// is used by Service mutations and artifact publication after expensive work:
// it retries with 2-50 ms backoff, holding neither lock between attempts, and
// throws workspace_busy (details.waited_ms) once publication_lock_wait elapses.
inline constexpr std::chrono::milliseconds publication_lock_wait{5000};
enum class LockWait { none, publication };
// Independent document writers; cooperates with the legacy workspace lock.
class DocumentLock {
public:
  DocumentLock(const fs::path& root, const std::string& id, LockWait wait = LockWait::none);
  ~DocumentLock();
  DocumentLock(const DocumentLock&) = delete;
  DocumentLock& operator=(const DocumentLock&) = delete;
private:
#ifdef _WIN32
  void* workspace_ = nullptr;
  void* document_ = nullptr;
#else
  int workspace_ = -1;
  int document_ = -1;
#endif
};

void directory(const fs::path& path);
std::string read_text(const fs::path& path, std::size_t max_bytes = max_json_bytes);
void atomic_text(const fs::path& path, const std::string& text, std::size_t max_bytes = max_json_bytes);
Json read_payload_json(const fs::path& path, std::size_t max_bytes = max_json_bytes);
void atomic_payload_json(const fs::path& path, const Json& value, std::size_t max_bytes = max_json_bytes);
fs::path temporary_file(const fs::path& directory);
// Creates a new owner-only directory with a unique name beneath parent.
fs::path temporary_directory(const fs::path& parent);
void publish_file(const fs::path& temporary, const fs::path& target);

class Store {
public:
  explicit Store(fs::path workspace);
  const fs::path& root() const { return root_; }
  Json read(const std::string& id, std::optional<std::uint64_t> revision = {}) const;
  // Caller holds DocumentLock (or WorkspaceLock), rechecks revision, then publishes.
  Json commit(const std::string& id, const Json& model, bool create, const Json& receipt = Json::object());
  std::optional<Json> request_replay(const std::string& id, const std::string& request_id,
                                     const std::string& fingerprint) const;
private:
  fs::path root_;
  fs::path document_dir(const std::string& id) const;
};
}
