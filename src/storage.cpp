#include "agentcad/storage.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <thread>
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
// A missing entry is a status, not an error; any other inspection failure is a
// storage_error rather than an escaping std::filesystem exception.
fs::file_status entry_status(const fs::path& path) {
  std::error_code error;
  const auto status = fs::symlink_status(path, error);
  if (status.type() == fs::file_type::none)
    throw Error("storage_error", "Cannot inspect " + path_to_utf8(path) + ": " + error.message());
  return status;
}
void reject_symlink(const fs::path& path) {
  if (fs::is_symlink(entry_status(path))) throw Error("storage_error", "Managed paths cannot be symlinks: " + path_to_utf8(path));
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
void close_lock(HANDLE& handle) { if (handle) CloseHandle(handle); handle = nullptr; }
#else
// Darwin's fsync() can leave data in the drive's volatile cache; F_FULLFSYNC
// asks the device to flush it. Filesystems without support fall back to fsync.
int durable_sync(int fd) {
#ifdef __APPLE__
  if (::fcntl(fd, F_FULLFSYNC) == 0) return 0;
#endif
  return ::fsync(fd);
}
void close_lock(int& fd) { if (fd >= 0) ::close(fd); fd = -1; }
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
  const int result = durable_sync(fd);
  const int saved = errno;
  ::close(fd);
  if (result != 0) { errno = saved; io_error("Sync directory"); }
}
#endif
}

void directory(const fs::path& path) {
  auto status = entry_status(path);
  if (fs::is_symlink(status)) throw Error("storage_error", "Managed paths cannot be symlinks: " + path_to_utf8(path));
  if (status.type() == fs::file_type::not_found) {
    std::error_code error;
    fs::create_directory(path, error);
    // Another process may create it concurrently; only the final state matters.
    status = entry_status(path);
    if (!fs::is_directory(status))
      throw Error("storage_error", "Cannot create directory: " + path_to_utf8(path) + (error ? ": " + error.message() : ""));
    sync_directory(path.parent_path());
  }
  if (!fs::is_directory(status)) throw Error("storage_error", "Not a directory: " + path_to_utf8(path));
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
DocumentLock::DocumentLock(const fs::path& root, const std::string& id, LockWait wait) {
  identifier(id); directory(root / ".locks");
  const auto bound = wait == LockWait::publication ? publication_lock_wait : std::chrono::milliseconds::zero();
  const auto deadline = std::chrono::steady_clock::now() + bound;
  std::chrono::steady_clock::duration pause = std::chrono::milliseconds(2);
  for (;;) {
    try {
      // Each attempt takes both locks in order (workspace shared, then document
      // exclusive) or neither, so a waiter holds nothing while it sleeps and
      // cannot deadlock against WorkspaceLock owners or other documents.
      workspace_ = open_lock(root / ".lock", false);
      try { document_ = open_lock(root / ".locks" / (id + ".lock"), true); return; }
      catch (...) { close_lock(workspace_); throw; }
    } catch (const Error& e) {
      if (e.code != "workspace_busy") throw;
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        if (bound.count() == 0) throw;
        throw Error("workspace_busy", "Another operation held this document for over " + std::to_string(bound.count()) +
          " ms; retry after it completes", {{"waited_ms", bound.count()}});
      }
      std::this_thread::sleep_for(std::min(pause, deadline - now));
      pause = std::min<std::chrono::steady_clock::duration>(pause * 2, std::chrono::milliseconds(50));
    }
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
  const int result = durable_sync(fd);
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
  std::error_code error;
  fs::create_directories(workspace, error);
  if (!error) root_ = fs::canonical(workspace, error);
  if (error) throw Error("storage_error", "Cannot open workspace " + path_to_utf8(workspace) + ": " + error.message());
  // Directory creation is idempotent. Readers starting in another process must
  // not acquire the writer lock merely to open an already existing workspace.
  directory(root_ / "documents"); directory(root_ / "exports");
}

fs::path Store::document_dir(const std::string& id) const {
  identifier(id);
  reject_symlink(root_ / "documents");
  const auto path = root_ / "documents" / id;
  reject_symlink(path); reject_symlink(path / "revisions"); reject_symlink(path / "receipts");
  return path;
}

// Receipt index: documents/<id>/receipts/<sha256(request_id)>.json names the
// revision that committed a request, and receipts/coverage.json records the
// revision through which every receipt is indexed. Revisions stay the source
// of truth: an entry is used only after the revision it names is confirmed to
// carry that request's receipt; anything else falls back to a verified scan.
namespace {
constexpr std::size_t receipt_index_bytes = 4096;
fs::path receipt_entry(const fs::path& document, const std::string& request_id) {
  return document / "receipts" / (sha256(request_id) + ".json");
}
std::uint64_t head_revision(const fs::path& document) {
  const auto head = parse_json(read_text(document / "HEAD.json"));
  fields(head, {"revision"});
  return revision_number(head.at("revision"));
}
// The receipt a revision file records for request_id, if any.
std::optional<Json> revision_receipt(const fs::path& document, std::uint64_t revision, const std::string& request_id) {
  const auto raw = parse_json(read_text(document / "revisions" / (std::to_string(revision) + ".json")));
  if (!raw.is_object() || !raw.contains("receipt")) return {};
  const auto& receipt = raw.at("receipt");
  if (!receipt.is_object() || receipt.value("request_id", Json()) != request_id) return {};
  if (!receipt.contains("fingerprint") || !receipt.contains("result") || !receipt.at("result").is_object())
    throw Error("storage_error", "Revision " + std::to_string(revision) + " has a malformed receipt");
  return receipt;
}
std::uint64_t indexed_through(const fs::path& document) {
  try {
    const auto coverage = parse_json(read_text(document / "receipts" / "coverage.json", receipt_index_bytes));
    fields(coverage, {"schema_version", "indexed_through_revision"});
    if (coverage.at("schema_version") != 1) return 0;
    return revision_number(coverage.at("indexed_through_revision"));
  } catch (const Error&) { return 0; } // Missing (older builds) or damaged: scan everything.
}
struct Found { std::uint64_t revision; Json receipt; };
struct IndexLookup { std::optional<Found> found; bool rescan = false; };
IndexLookup lookup_index(const fs::path& document, const std::string& request_id, std::uint64_t head) {
  Json entry;
  try { entry = parse_json(read_text(receipt_entry(document, request_id), receipt_index_bytes)); }
  catch (const Error& e) { return {std::nullopt, e.code != "not_found"}; }
  try {
    fields(entry, {"schema_version", "request_id", "revision"});
    if (entry.at("schema_version") == 1 && entry.at("request_id") == request_id) {
      const auto revision = revision_number(entry.at("revision"));
      if (revision <= head)
        if (auto receipt = revision_receipt(document, revision, request_id)) return {Found{revision, std::move(*receipt)}, false};
    }
  } catch (const Error&) {}
  // Damaged, forged, or left beyond HEAD by an interrupted commit: never trusted.
  return {std::nullopt, true};
}
void write_receipt_entry(const fs::path& document, const std::string& request_id, std::uint64_t revision) {
  atomic_text(receipt_entry(document, request_id),
    Json{{"schema_version", 1}, {"request_id", request_id}, {"revision", revision}}.dump() + "\n");
}
// Coverage only bounds later scans, so it is replaced without fsync: losing
// or tearing it after a crash just means a longer scan, never a missed receipt,
// because every entry it covers was made durable before its HEAD.
void write_coverage(const fs::path& document, std::uint64_t revision) {
  const auto target = document / "receipts" / "coverage.json";
  std::error_code ignored;
  try {
    const auto temporary = temporary_file(target.parent_path());
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream << Json{{"schema_version", 1}, {"indexed_through_revision", revision}}.dump() << '\n';
    stream.close();
    if (stream) fs::rename(temporary, target, ignored);
    if (!stream || ignored) fs::remove(temporary, ignored);
  } catch (const std::exception&) {}
}
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
  const bool exists = fs::exists(entry_status(path / "HEAD.json"));
  if (create && exists) throw Error("already_exists", "Document already exists: " + id);
  if (!create && !exists) throw Error("not_found", "Document does not exist: " + id);
  const auto next = exists ? revision_number(read(id).at("revision")) + 1 : 1;
  if (next > 9007199254740991ULL) throw Error("limit_exceeded", "Revision counter exhausted");
  directory(path); directory(path / "revisions"); directory(path / "receipts");
  Json record = {{"schema_version", 1}, {"document_id", id}, {"revision", next},
                 {"kernel_version", kernel_version()}, {"model", model}};
  std::string request_id;
  if (!receipt.empty()) {
    fields(receipt, {"request_id", "fingerprint", "result"});
    request_id = text_field(receipt, "request_id"); identifier(request_id);
    if (!receipt.at("result").is_object()) throw Error("invalid_argument", "Receipt result must be an object");
    record["receipt"] = receipt;
  }
  // Index every committed receipt first: documents written by older builds, or
  // whose coverage update was lost, are backfilled from their revisions once.
  const auto committed = next - 1;
  if (const auto through = std::min(indexed_through(path), committed); through < committed) {
    std::map<std::string, std::uint64_t> receipts;
    for (auto revision = through + 1; revision <= committed; ++revision) {
      const auto raw = parse_json(read_text(path / "revisions" / (std::to_string(revision) + ".json")));
      if (raw.is_object() && raw.contains("receipt") && raw.at("receipt").is_object() &&
          raw.at("receipt").contains("request_id") && raw.at("receipt").at("request_id").is_string())
        receipts[raw.at("receipt").at("request_id").get<std::string>()] = revision;
    }
    for (const auto& [indexed, revision] : receipts) write_receipt_entry(path, indexed, revision);
  }
  // The entry is durable before HEAD names its revision, so every committed
  // receipt at or below coverage is indexed. An interrupted commit leaves an
  // entry beyond HEAD, which lookups reject.
  if (!request_id.empty()) write_receipt_entry(path, request_id, next);
  // A snapshot beyond HEAD from a previous interrupted write is uncommitted.
  // It is safe to replace that exact next snapshot while holding the workspace lock.
  atomic_text(path / "revisions" / (std::to_string(next) + ".json"), record.dump(2) + "\n");
  atomic_text(path / "HEAD.json", Json{{"revision", next}}.dump() + "\n");
  write_coverage(path, next);
  record.erase("receipt");
  return record;
}

std::optional<Json> Store::request_replay(const std::string& id, const std::string& request_id,
                                          const std::string& fingerprint) const {
  identifier(request_id);
  const auto path = document_dir(id);
  // Read HEAD before the index: an entry written for a revision at or below
  // this HEAD is already durable, so lock-free readers (job recovery) agree.
  std::uint64_t head = 0;
  try { head = head_revision(path); } catch (const Error& e) { if (e.code == "not_found") return {}; throw; }
  auto lookup = lookup_index(path, request_id, head);
  if (!lookup.found) {
    // A missing entry leaves only revisions above coverage to check (normally
    // none); an untrustworthy entry rescans all history.
    const auto floor = lookup.rescan ? 0 : std::min(indexed_through(path), head);
    for (auto revision = head; revision > floor && !lookup.found; --revision)
      if (auto receipt = revision_receipt(path, revision, request_id)) lookup.found = Found{revision, std::move(*receipt)};
  }
  if (!lookup.found) return {};
  if (lookup.found->receipt.at("fingerprint") != fingerprint)
    throw Error("request_conflict", "request_id was already committed with different arguments");
  auto result = read(id, lookup.found->revision);
  result.update(lookup.found->receipt.at("result"));
  return result;
}
}
