#include "agentcad/storage.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <vector>

namespace agentcad {
fs::path path_from_utf8(const std::string& text) {
  return fs::path(std::u8string(text.begin(), text.end()));
}
std::string path_to_utf8(const fs::path& path) {
  const auto text = path.u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}
namespace {
void io_error(const std::string& operation) {
  throw Error("storage_error", operation + ": " + std::strerror(errno));
}
void reject_symlink(const fs::path& path) {
  if (fs::is_symlink(fs::symlink_status(path))) throw Error("storage_error", "Managed paths cannot be symlinks: " + path_to_utf8(path));
}
#ifdef _WIN32
void win_error(const std::string& operation) {
  throw Error("storage_error", operation + ": Windows error " + std::to_string(GetLastError()));
}
HANDLE open_lock(const fs::path& path, bool exclusive) {
  reject_symlink(path);
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) win_error("Open lock");
  OVERLAPPED position{};
  if (!LockFileEx(handle, LOCKFILE_FAIL_IMMEDIATELY | (exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0), 0, 1, 0, &position)) {
    const auto error = GetLastError(); CloseHandle(handle);
    if (error == ERROR_LOCK_VIOLATION || error == ERROR_IO_PENDING)
      throw Error("workspace_busy", "Another operation owns this document or workspace; retry after it completes");
    SetLastError(error); win_error("Lock file");
  }
  return handle;
}
void sync_directory(const fs::path&) {
  // Windows publication uses MoveFileExW(MOVEFILE_WRITE_THROUGH). Directory
  // handles do not support FlushFileBuffers on ordinary Windows filesystems.
}
#else
int open_lock(const fs::path& path, bool exclusive) {
  const int fd = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0) io_error("Open lock");
  if (::flock(fd, (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB) != 0) {
    const int saved = errno; ::close(fd);
    if (saved == EWOULDBLOCK || saved == EAGAIN)
      throw Error("workspace_busy", "Another operation owns this document or workspace; retry after it completes");
    errno = saved; io_error("Lock file");
  }
  return fd;
}
void sync_directory(const fs::path& path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) io_error("Open directory for sync");
  const int result = ::fsync(fd);
  const int saved = errno;
  ::close(fd);
  if (result != 0) { errno = saved; io_error("Sync directory"); }
}
#endif
}

void directory(const fs::path& path) {
  reject_symlink(path);
  if (!fs::exists(path)) {
    if (!fs::create_directory(path) && !fs::is_directory(path))
      throw Error("storage_error", "Cannot create directory: " + path_to_utf8(path));
    sync_directory(path.parent_path());
  }
  if (!fs::is_directory(path)) throw Error("storage_error", "Not a directory: " + path_to_utf8(path));
}

WorkspaceLock::WorkspaceLock(const fs::path& root) {
#ifdef _WIN32
  handle_ = open_lock(root / ".lock", true);
#else
  fd_ = open_lock(root / ".lock", true);
#endif
}
WorkspaceLock::~WorkspaceLock() {
#ifdef _WIN32
  if (handle_) CloseHandle(handle_);
#else
  if (fd_ >= 0) ::close(fd_);
#endif
}
DocumentLock::DocumentLock(const fs::path& root, const std::string& id) {
  identifier(id); directory(root / ".locks");
  workspace_ = open_lock(root / ".lock", false);
  try { document_ = open_lock(root / ".locks" / (id + ".lock"), true); }
  catch (...) {
#ifdef _WIN32
    CloseHandle(workspace_);
#else
    ::close(workspace_);
#endif
    throw;
  }
}
DocumentLock::~DocumentLock() {
#ifdef _WIN32
  if (document_) CloseHandle(document_);
  if (workspace_) CloseHandle(workspace_);
#else
  if (document_ >= 0) ::close(document_);
  if (workspace_ >= 0) ::close(workspace_);
#endif
}

std::string read_text(const fs::path& path, std::size_t max_bytes) {
#ifdef _WIN32
  reject_symlink(path);
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)
      throw Error("not_found", "File not found: " + path_to_utf8(path));
    win_error("Open input file");
  }
  BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER size{};
  if (!GetFileInformationByHandle(handle, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
      !GetFileSizeEx(handle, &size) || size.QuadPart < 0 || static_cast<std::uint64_t>(size.QuadPart) > max_bytes) {
    CloseHandle(handle); throw Error("storage_error", "Input must be a regular file within the requested byte limit");
  }
  std::string result; char buffer[4096]; DWORD count;
  while (true) {
    if (!ReadFile(handle, buffer, sizeof(buffer), &count, nullptr)) { CloseHandle(handle); win_error("Read file"); }
    if (count == 0) break;
    result.append(buffer, count);
    if (result.size() > max_bytes) { CloseHandle(handle); throw Error("limit_exceeded", "File exceeds the requested byte limit"); }
  }
  CloseHandle(handle); return result;
#else
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0) {
    if (errno == ENOENT) throw Error("not_found", "File not found: " + path_to_utf8(path));
    io_error("Open input file");
  }
  struct stat st{};
  if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || static_cast<std::uint64_t>(st.st_size) > max_bytes) {
    ::close(fd); throw Error("storage_error", "Input must be a regular file within the requested byte limit");
  }
  std::string result;
  char buffer[4096];
  while (true) {
    const auto size = ::read(fd, buffer, sizeof(buffer));
    if (size < 0) {
      if (errno == EINTR) continue;
      const int saved = errno; ::close(fd); errno = saved; io_error("Read file");
    }
    if (size == 0) break;
    result.append(buffer, static_cast<std::size_t>(size));
    if (result.size() > max_bytes) { ::close(fd); throw Error("limit_exceeded", "File exceeds the requested byte limit"); }
  }
  ::close(fd);
  return result;
#endif
}

fs::path temporary_file(const fs::path& parent) {
#ifdef _WIN32
  std::random_device random;
  for (int attempt = 0; attempt < 128; ++attempt) {
    auto path = parent / (".pending-" + std::to_string(random()) + "-" + std::to_string(random()));
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE) { CloseHandle(handle); return path; }
    if (GetLastError() != ERROR_FILE_EXISTS) win_error("Create temporary file");
  }
  throw Error("storage_error", "Cannot allocate a temporary file");
#else
  std::string pattern = (parent / ".pending-XXXXXX").string();
  std::vector<char> bytes(pattern.begin(), pattern.end()); bytes.push_back('\0');
  const int fd = ::mkstemp(bytes.data());
  if (fd < 0) io_error("Create temporary file");
  ::close(fd);
  return fs::path(bytes.data());
#endif
}

fs::path temporary_directory(const fs::path& parent) {
#ifdef _WIN32
  std::random_device random;
  for (int attempt = 0; attempt < 128; ++attempt) {
    auto path = parent / (".pending-" + std::to_string(random()) + "-" + std::to_string(random()));
    if (CreateDirectoryW(path.c_str(), nullptr)) return path;
    if (GetLastError() != ERROR_ALREADY_EXISTS) win_error("Create temporary directory");
  }
  throw Error("storage_error", "Cannot allocate a temporary directory");
#else
  std::string pattern = (parent / ".pending-XXXXXX").string();
  std::vector<char> bytes(pattern.begin(), pattern.end()); bytes.push_back('\0');
  if (!::mkdtemp(bytes.data())) io_error("Create temporary directory");
  return fs::path(bytes.data());
#endif
}

void publish_file(const fs::path& temporary, const fs::path& target) {
  reject_symlink(target);
#ifdef _WIN32
  reject_symlink(temporary);
  HANDLE handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) win_error("Open temporary file for sync");
  if (!FlushFileBuffers(handle)) { CloseHandle(handle); win_error("Sync file"); }
  CloseHandle(handle);
  if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) win_error("Publish file");
#else
  const int fd = ::open(temporary.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) io_error("Open temporary file for sync");
  const int result = ::fsync(fd);
  const int saved = errno; ::close(fd);
  if (result != 0) { errno = saved; io_error("Sync file"); }
  if (::rename(temporary.c_str(), target.c_str()) != 0) io_error("Publish file");
  // A failure here has an uncertain durability outcome: the rename already occurred.
  sync_directory(target.parent_path());
#endif
}

void atomic_text(const fs::path& path, const std::string& text, std::size_t max_bytes) {
  if (text.size() > max_bytes)
    throw Error("limit_exceeded", max_bytes == max_json_bytes ? "Saved JSON exceeds 1 MiB" : "Saved file exceeds its byte limit");
  const auto temporary = temporary_file(path.parent_path());
  try {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size())); stream.flush();
    if (!stream) throw Error("storage_error", "Could not write temporary file");
    stream.close();
    if (!stream) throw Error("storage_error", "Could not close temporary file");
    publish_file(temporary, path);
  } catch (...) { std::error_code unused; fs::remove(temporary, unused); throw; }
}

Store::Store(fs::path workspace) {
  if (workspace.empty()) throw Error("invalid_argument", "An explicit workspace is required");
  fs::create_directories(workspace);
  root_ = fs::canonical(workspace);
  // Directory creation is idempotent. Readers starting in another process must
  // not acquire the writer lock merely to open an already existing workspace.
  directory(root_ / "documents"); directory(root_ / "exports");
}

fs::path Store::document_dir(const std::string& id) const {
  identifier(id);
  reject_symlink(root_ / "documents");
  const auto path = root_ / "documents" / id;
  reject_symlink(path); reject_symlink(path / "revisions");
  return path;
}

Json Store::read(const std::string& id, std::optional<std::uint64_t> revision) const {
  const auto path = document_dir(id);
  // HEAD is also the commit boundary: orphan revisions are never readable.
  const auto head = parse_json(read_text(path / "HEAD.json"));
  fields(head, {"revision"});
  const auto current = revision_number(head.at("revision"));
  const auto requested = revision.value_or(current);
  if (requested > current) throw Error("not_found", "Revision has not been committed");
  auto record = parse_json(read_text(path / "revisions" / (std::to_string(requested) + ".json")));
  fields(record, {"schema_version", "document_id", "revision", "kernel_version", "model"}, {"receipt"});
  if (record.at("schema_version") != 1 || record.at("document_id") != id || record.at("revision") != requested)
    throw Error("storage_error", "Revision record does not match its identity");
  if (record.at("kernel_version") != kernel_version()) throw Error("kernel_mismatch", "This document requires a different kernel version");
  validate_model(record.at("model"));
  record.erase("receipt");
  return record;
}

Json Store::commit(const std::string& id, const Json& model, bool create, const Json& receipt) {
  const auto path = document_dir(id);
  const bool exists = fs::exists(path / "HEAD.json");
  if (create && exists) throw Error("already_exists", "Document already exists: " + id);
  if (!create && !exists) throw Error("not_found", "Document does not exist: " + id);
  const auto next = exists ? revision_number(read(id).at("revision")) + 1 : 1;
  if (next > 9007199254740991ULL) throw Error("limit_exceeded", "Revision counter exhausted");
  directory(path); directory(path / "revisions");
  Json record = {{"schema_version", 1}, {"document_id", id}, {"revision", next},
                 {"kernel_version", kernel_version()}, {"model", model}};
  if (!receipt.empty()) {
    fields(receipt, {"request_id", "fingerprint", "result"});
    identifier(text_field(receipt, "request_id"));
    if (!receipt.at("result").is_object()) throw Error("invalid_argument", "Receipt result must be an object");
    record["receipt"] = receipt;
  }
  // A snapshot beyond HEAD from a previous interrupted write is uncommitted.
  // It is safe to replace that exact next snapshot while holding the workspace lock.
  atomic_text(path / "revisions" / (std::to_string(next) + ".json"), record.dump(2) + "\n");
  atomic_text(path / "HEAD.json", Json{{"revision", next}}.dump() + "\n");
  record.erase("receipt");
  return record;
}

std::optional<Json> Store::request_replay(const std::string& id, const std::string& request_id,
                                          const std::string& fingerprint) const {
  identifier(request_id);
  Json head;
  try { head = read(id); } catch (const Error& e) { if (e.code == "not_found") return {}; throw; }
  const auto path = document_dir(id);
  for (auto revision = revision_number(head.at("revision")); revision > 0; --revision) {
    const auto raw = parse_json(read_text(path / "revisions" / (std::to_string(revision) + ".json")));
    if (!raw.contains("receipt") || raw.at("receipt").at("request_id") != request_id) continue;
    const auto& receipt = raw.at("receipt");
    if (receipt.at("fingerprint") != fingerprint)
      throw Error("request_conflict", "request_id was already committed with different arguments");
    auto result = read(id, revision);
    result.update(receipt.at("result"));
    return result;
  }
  return {};
}
}
