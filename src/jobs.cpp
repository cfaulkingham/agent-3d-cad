#include "agentcad/jobs.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/service.hpp"
#include "agentcad/drawing.hpp"
#include "agentcad/cache.hpp"
#include "agentcad/model.hpp"
#include "agentcad/runtime.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <thread>
#include <tuple>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#ifdef __APPLE__
#include <mach/mach.h>
#endif
extern char** environ;
#endif

namespace agentcad {
namespace {
constexpr std::size_t max_result_bytes = 64 * 1024 * 1024;
// result.json and request.json wrap a bounded payload with the job identity.
constexpr std::size_t max_result_file_bytes = max_result_bytes + 64 * 1024;
constexpr std::size_t max_request_file_bytes = max_json_bytes + 64 * 1024;
constexpr int max_active_jobs = 8;
constexpr int max_workers = 4;
constexpr int job_schema_version = 2;
constexpr std::int64_t recovery_grace_ms = 5000;
// Terminal job records are collected during submit admission once they are
// older than seven days or beyond the newest 256 (docs/PROTOCOL.md).
constexpr std::int64_t terminal_retention_ms = 7LL * 24 * 60 * 60 * 1000;
constexpr std::size_t max_terminal_jobs = 256;
fs::path worker_executable;
fs::path cancellation_file;
Json request_context = Json::object();
std::int64_t job_deadline = 0;
Json current_budget = {{"timeout_ms", 30000}, {"memory_mb", 2048}};
std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
}
class WorkerWatchdog {
public:
  explicit WorkerWatchdog(const Json& limits) : thread_([timeout = limits.at("timeout_ms").get<int>(),
    memory = limits.at("memory_mb").get<int>()](std::stop_token stop) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    while (!stop.stop_requested()) {
      if (std::chrono::steady_clock::now() >= deadline) std::_Exit(124);
#ifdef __APPLE__
      task_vm_info_data_t info{}; mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
      if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        std::_Exit(126);
      // Darwin rejects RLIMIT_AS on current arm64 systems. One sampling interval
      // of overshoot is possible with physical-footprint enforcement.
      if (info.phys_footprint > static_cast<std::uint64_t>(memory) * 1024 * 1024) std::_Exit(137);
#else
      (void)memory;
#endif
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }) {}
private:
  std::jthread thread_;
};
bool terminal_name(const std::string& value) {
  return value == "succeeded" || value == "failed" || value == "cancelled" || value == "interrupted";
}
bool terminal(const Json& state) { return terminal_name(state.at("state").get<std::string>()); }
bool mutation(const std::string& tool) {
  return tool == "cad_create" || tool == "cad_apply" || tool == "cad_restore" || tool == "cad_import";
}
Json budget(const Json& value) {
  fields(value, {}, {"timeout_ms", "memory_mb"});
  Json result = {{"timeout_ms", 30000}, {"memory_mb", 2048}};
  for (auto [key, minimum, maximum] : {std::tuple{"timeout_ms", 1, 300000}, std::tuple{"memory_mb", 128, 4096}}) {
    if (!value.contains(key)) continue;
    if (!value.at(key).is_number_integer() || value.at(key) < minimum || value.at(key) > maximum)
      throw Error("invalid_argument", std::string(key) + " is outside its supported integer range");
    result[key] = value.at(key);
  }
  return result;
}
fs::path executable_path() {
  // Test programs name the service executable explicitly. Otherwise workers and
  // coordinators run the very image of this process, whatever its file name.
  if (!worker_executable.empty()) return worker_executable;
  const auto image = current_executable().image;
  if (image.empty()) throw Error("worker_failed", "Cannot find native worker executable");
  return image;
}
Json read_result(const fs::path& path) {
  std::string text;
  try { text = read_text(path, max_result_bytes); }
  catch (const Error& e) {
    if (e.code == "limit_exceeded") throw Error("limit_exceeded", "Worker output exceeds 64 MiB");
    throw Error("worker_failed", "Worker output missing or exceeds 64 MiB");
  }
  try { return Json::parse(text); }
  catch (const Json::exception&) { throw Error("worker_failed", "Worker returned malformed JSON"); }
}
void write_result(const fs::path& path, const Json& value) {
  const auto text = value.dump();
  if (text.size() > max_result_bytes) throw Error("limit_exceeded", "Worker result exceeds 64 MiB");
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(text.data(), static_cast<std::streamsize>(text.size())); stream.close();
  if (!stream) throw Error("storage_error", "Cannot write worker result");
}
struct TemporaryDirectory {
  fs::path path;
  explicit TemporaryDirectory(const fs::path& root) {
    directory(root / ".workers");
    path = temporary_directory(root / ".workers");
  }
  ~TemporaryDirectory() { std::error_code ignored; fs::remove_all(path, ignored); }
};
#ifdef _WIN32
std::wstring quote(const std::wstring& text) {
  std::wstring result = L"\""; std::size_t slashes = 0;
  for (wchar_t c : text) {
    if (c == L'\\') { ++slashes; continue; }
    if (c == L'\"') result.append(slashes * 2 + 1, L'\\'); else result.append(slashes, L'\\');
    slashes = 0; result += c;
  }
  result.append(slashes * 2, L'\\'); return result + L"\"";
}
struct Child {
  HANDLE process = nullptr;
  HANDLE group = nullptr;
  Child() = default;
  Child(const Child&) = delete;
  Child(Child&& other) noexcept : process(other.process), group(other.group) { other.process = nullptr; other.group = nullptr; }
  ~Child() { if (process) CloseHandle(process); if (group) CloseHandle(group); }
  void stop() { if (group) TerminateJobObject(group, 1); else TerminateProcess(process, 1); WaitForSingleObject(process, INFINITE); }
  bool done(int& status) {
    const auto state = WaitForSingleObject(process, 0);
    if (state == WAIT_FAILED) throw Error("worker_failed", "Cannot wait for geometry worker");
    if (state != WAIT_OBJECT_0) return false;
    DWORD code = 1;
    if (!GetExitCodeProcess(process, &code)) throw Error("worker_failed", "Cannot read geometry worker exit status");
    status = static_cast<int>(code); return true;
  }
  std::uint64_t id() const { return GetProcessId(process); }
};
Child launch(const std::vector<fs::path>& args, bool bounded, int memory_mb) {
  const auto executable = executable_path(); std::wstring command = quote(executable.wstring());
  for (const auto& argument : args) command += L" " + quote(argument.wstring());
  Child child;
  if (bounded) {
    child.group = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
    limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limit.ProcessMemoryLimit = static_cast<SIZE_T>(memory_mb) * 1024 * 1024;
    if (!child.group || !SetInformationJobObject(child.group, JobObjectExtendedLimitInformation, &limit, sizeof(limit)))
      throw Error("worker_failed", "Cannot install worker memory budget");
  }
  SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE null_handle = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
    &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (null_handle == INVALID_HANDLE_VALUE) throw Error("worker_failed", "Cannot open worker standard streams");
  // Inherit only the NUL standard streams, never a caller's pipes or lock
  // handles; a durable coordinator must not hold a captured caller pipe open
  // (POSIX likewise detaches the coordinator's stderr).
  SIZE_T attribute_bytes = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
  std::vector<unsigned char> attribute_storage(attribute_bytes);
  auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
  if (attribute_bytes == 0 || !InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_bytes)) {
    CloseHandle(null_handle); throw Error("worker_failed", "Cannot prepare worker handle inheritance");
  }
  HANDLE inherited[] = {null_handle};
  if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr)) {
    DeleteProcThreadAttributeList(attributes); CloseHandle(null_handle);
    throw Error("worker_failed", "Cannot restrict worker handle inheritance");
  }
  STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = null_handle; startup.StartupInfo.hStdOutput = null_handle; startup.StartupInfo.hStdError = null_handle;
  startup.lpAttributeList = attributes;
  PROCESS_INFORMATION process{};
  const bool created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
    CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &process);
  const auto launch_error = GetLastError();
  DeleteProcThreadAttributeList(attributes);
  CloseHandle(null_handle);
  if (!created) throw Error("worker_failed", "Cannot launch native worker: " + std::to_string(launch_error));
  child.process = process.hProcess;
  if (bounded && !AssignProcessToJobObject(child.group, child.process)) {
    TerminateProcess(child.process, 1); CloseHandle(process.hThread);
    throw Error("worker_failed", "Cannot contain worker in Windows job object");
  }
  if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
    TerminateProcess(child.process, 1); CloseHandle(process.hThread);
    throw Error("worker_failed", "Cannot start native worker thread");
  }
  CloseHandle(process.hThread); return child;
}
#else
struct Child {
  pid_t pid = -1;
  bool collected = false;
  void stop() {
    if (pid > 0 && !collected) {
      ::kill(-pid, SIGKILL);
      while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
      collected = true;
    }
  }
  bool done(int& status) {
    const auto result = ::waitpid(pid, &status, WNOHANG);
    if (result < 0 && errno == EINTR) return false;
    if (result < 0) throw Error("worker_failed", "Cannot wait for geometry worker");
    collected = result == pid;
    // Keep a signal death distinct from a worker's deliberate watchdog exit.
    if (collected) status = WIFEXITED(status) ? WEXITSTATUS(status) : 256 + WTERMSIG(status);
    return collected;
  }
  std::uint64_t id() const { return static_cast<std::uint64_t>(pid); }
};
std::vector<pid_t> background_children;
Child launch(const std::vector<fs::path>& args, bool bounded, int) {
  const auto executable = executable_path();
  std::vector<std::string> strings{executable.string()};
  for (const auto& argument : args) strings.push_back(argument.string());
  std::vector<char*> argv; for (auto& argument : strings) argv.push_back(argument.data()); argv.push_back(nullptr);
  posix_spawn_file_actions_t actions; posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
  // A durable background coordinator must not keep a caller's captured stderr
  // pipe open after the submit response has completed.
  if (!bounded) posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
  // Every worker owns a process group, allowing deadline/cancel to terminate descendants.
  posix_spawnattr_t attributes; posix_spawnattr_init(&attributes);
  posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
  posix_spawnattr_setpgroup(&attributes, 0);
  Child child;
  const auto error = posix_spawn(&child.pid, executable.c_str(), &actions, &attributes, argv.data(), environ);
  posix_spawnattr_destroy(&attributes); posix_spawn_file_actions_destroy(&actions);
  if (error) throw Error("worker_failed", "Cannot launch native worker: " + std::string(std::strerror(error)));
  return child;
}
#endif
// Job directory layout (docs/PROTOCOL.md):
//   state.json   small commit record: identity, budget, state, timestamps, error
//                and the SHA-256/size of result.json. Admission and list read only this.
//   request.json immutable submitted arguments with the job identity.
//   result.json  the result with the job identity, published before the state
//                that reports success.
// Records from the previous format embed `arguments` and `result` in state.json.
// They are normalized when read and rewritten in this layout once terminal.
struct JobRecord {
  Json state;
  std::optional<Json> arguments;  // previous format only
  std::optional<Json> result;     // previous format only
  bool legacy = false;
};
struct StoredResult {
  Json result;
  std::string sha256;
  std::size_t bytes = 0;
};
Error corrupt(const std::string& message) { return Error("job_record_corrupt", message); }
std::int64_t file_time_ms(const fs::path& path) {
  std::error_code error;
  const auto written = fs::last_write_time(path, error);
  if (error) return 0;
  const auto age = fs::file_time_type::clock::now() - written;
  return now_ms() - std::chrono::duration_cast<std::chrono::milliseconds>(age).count();
}
std::optional<std::string> document_of(const Json& arguments) {
  if (!arguments.is_object() || !arguments.contains("document_id") || !arguments.at("document_id").is_string()) return {};
  auto id = arguments.at("document_id").get<std::string>();
  try { identifier(id); } catch (const Error&) { return {}; }
  return id;
}
void check_state(const Json& state, const std::string& id) {
  if (!state.is_object()) throw corrupt("Job state is not an object");
  const auto has = [&](const char* key, bool (Json::*kind)() const noexcept) {
    return state.contains(key) && (state.at(key).*kind)();
  };
  if (state.value("schema_version", Json()) != job_schema_version || !has("job_id", &Json::is_string) || state.at("job_id") != id ||
      !has("request_id", &Json::is_string) || state.at("request_id") != id || !has("tool", &Json::is_string) ||
      !has("fingerprint", &Json::is_string) || !has("state", &Json::is_string) || !has("budget", &Json::is_object) ||
      !has("progress", &Json::is_number) || !has("submitted_at_unix_ms", &Json::is_number_integer) ||
      !has("deadline_unix_ms", &Json::is_number_integer))
    throw corrupt("Job state record is incomplete or does not match its identity");
  const auto value = state.at("state").get<std::string>();
  if (!terminal_name(value) && value != "queued" && value != "running" && value != "cancelling") throw corrupt("Job state is unknown");
  try { budget(state.at("budget")); } catch (const Error&) { throw corrupt("Job budget is invalid"); }
  if ((state.contains("updated_at_unix_ms") && !has("updated_at_unix_ms", &Json::is_number_integer)) ||
      (state.contains("retried") && !has("retried", &Json::is_boolean)) ||
      (state.contains("error") && !has("error", &Json::is_object)) ||
      (state.contains("document_id") && !(has("document_id", &Json::is_string) && document_of(state))) ||
      state.contains("result_sha256") != state.contains("result_bytes") ||
      (state.contains("result_sha256") && (!has("result_sha256", &Json::is_string) || !has("result_bytes", &Json::is_number_unsigned))))
    throw corrupt("Job state record has malformed fields");
}
// Reads one job record. Missing state is not_found; anything unreadable,
// malformed or foreign is job_record_corrupt and never another exception type.
JobRecord load_job(const fs::path& path, const std::string& id) {
  const auto file = path / "state.json";
  try {
    const auto status = fs::symlink_status(file);
    if (!fs::exists(status)) throw Error("not_found", "Job does not exist: " + id);
    if (!fs::is_regular_file(status)) throw corrupt("Job state is not a regular file");
    // Only a previous-format record embeds a result beyond the small-state bound.
    const bool large = fs::file_size(file) > max_json_bytes;
    auto raw = parse_json(read_text(file, large ? max_result_file_bytes : max_json_bytes), max_result_file_bytes);
    JobRecord record;
    if (raw.is_object() && !raw.contains("schema_version")) {
      record.legacy = true;
      if (raw.contains("arguments")) { record.arguments = raw.at("arguments"); raw.erase("arguments"); }
      if (raw.contains("result")) { record.result = raw.at("result"); raw.erase("result"); }
      if (record.arguments) if (const auto document = document_of(*record.arguments)) raw["document_id"] = *document;
      raw["schema_version"] = job_schema_version;
    } else if (large) throw corrupt("Job state exceeds 1 MiB");
    record.state = std::move(raw);
    check_state(record.state, id);
    return record;
  } catch (const Error& e) {
    if (e.code == "not_found" || e.code == "job_record_corrupt") throw;
    throw corrupt("Job state is unreadable: " + std::string(e.what()));
  } catch (const std::exception& e) { throw corrupt("Job state is unreadable: " + std::string(e.what())); }
}
Json public_job(Json state) {
  for (const auto* key : {"arguments", "fingerprint", "deadline_unix_ms", "schema_version", "document_id",
                          "result_sha256", "result_bytes", "result"})
    state.erase(key);
  return state;
}
// A damaged record keeps its identity and reads as a terminal failure. It never
// blocks admission unless a live coordinator still owns it.
Json corrupt_view(const std::string& id, std::int64_t timestamp, const Error& error) {
  return {{"job_id", id}, {"request_id", id}, {"tool", ""}, {"budget", Json::object()}, {"state", "failed"},
    {"progress", 0.0}, {"submitted_at_unix_ms", 0}, {"updated_at_unix_ms", timestamp}, {"error", error.json()}};
}
void write_request(const fs::path& path, const Json& state, const Json& arguments) {
  atomic_text(path / "request.json", Json{{"schema_version", job_schema_version}, {"job_id", state.at("job_id")},
    {"tool", state.at("tool")}, {"fingerprint", state.at("fingerprint")}, {"arguments", arguments}}.dump(), max_request_file_bytes);
}
Json request_arguments(const fs::path& path, const JobRecord& record) {
  if (record.arguments) {
    if (!record.arguments->is_object()) throw corrupt("Job arguments are not an object");
    return *record.arguments;
  }
  Json request;
  try { request = parse_json(read_text(path / "request.json", max_request_file_bytes), max_request_file_bytes); }
  catch (const Error& e) { throw corrupt("Job request is missing or unreadable: " + std::string(e.what())); }
  const auto& state = record.state;
  if (!request.is_object() || request.value("schema_version", Json()) != job_schema_version ||
      request.value("job_id", Json()) != state.at("job_id") || request.value("tool", Json()) != state.at("tool") ||
      !request.contains("arguments") || !request.at("arguments").is_object() ||
      request.value("fingerprint", Json()) != state.at("fingerprint") ||
      Json(request_fingerprint(state.at("tool").get<std::string>(), request.at("arguments"))) != state.at("fingerprint"))
    throw corrupt("Job request does not match its state record");
  return request.at("arguments");
}
// Publishes result.json, then records its digest in the (not yet saved) state.
void attach_result(const fs::path& path, Json& state, const Json& result) {
  const auto text = Json{{"schema_version", job_schema_version}, {"job_id", state.at("job_id")},
    {"fingerprint", state.at("fingerprint")}, {"result", result}}.dump();
  if (text.size() > max_result_file_bytes) throw Error("limit_exceeded", "Job result exceeds 64 MiB");
  atomic_text(path / "result.json", text, max_result_file_bytes);
  state["result_sha256"] = sha256(text); state["result_bytes"] = text.size();
}
// With a recorded digest, result.json must match it exactly. Without one (the
// coordinator stopped before its state flipped) the file's identity must match.
StoredResult read_stored_result(const fs::path& path, const Json& state) {
  const auto text = read_text(path / "result.json", max_result_file_bytes);
  auto digest = sha256(text);
  if (state.contains("result_sha256") && (state.at("result_sha256") != digest || state.at("result_bytes") != text.size()))
    throw corrupt("Job result does not match its recorded digest");
  Json envelope;
  try { envelope = Json::parse(text); } catch (const Json::exception&) { throw corrupt("Job result is not valid JSON"); }
  if (!envelope.is_object() || envelope.value("schema_version", Json()) != job_schema_version ||
      envelope.value("job_id", Json()) != state.at("job_id") || envelope.value("fingerprint", Json()) != state.at("fingerprint") ||
      !envelope.contains("result"))
    throw corrupt("Job result does not belong to this job");
  return {std::move(envelope.at("result")), std::move(digest), text.size()};
}
// Callers hold the admission lock, which serializes every state.json write.
void save_job(const fs::path& path, JobRecord& record, bool touch = true) {
  if (record.legacy) {
    // Request and result are durable before the small record that commits them.
    if (record.arguments) write_request(path, record.state, *record.arguments);
    if (record.result) attach_result(path, record.state, *record.result);
  }
  if (touch) record.state["updated_at_unix_ms"] = now_ms();
  atomic_text(path / "state.json", record.state.dump());
  record.legacy = false; record.arguments.reset(); record.result.reset();
}
// Rewrites a terminal previous-format record once, so admission never parses an
// embedded result again. Failure leaves it readable through the legacy path.
void migrate_job(const fs::path& path, JobRecord& record) {
  if (!record.legacy || !terminal(record.state)) return;
  try { save_job(path, record, false); } catch (const std::exception&) {}
}
std::unique_ptr<WorkspaceLock> wait_lock(const fs::path& path) {
  for (int attempt = 0;; ++attempt) {
    try { return std::make_unique<WorkspaceLock>(path); }
    catch (const Error& e) {
      if (e.code != "workspace_busy" || attempt >= 500) throw;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
}
std::optional<Json> committed_result(const fs::path& workspace, const Json& state) {
  if (!mutation(state.at("tool").get<std::string>()) || !state.contains("document_id")) return {};
  return Store(workspace).request_replay(state.at("document_id").get<std::string>(),
    state.at("request_id").get<std::string>(), state.at("fingerprint").get<std::string>());
}
// Recovery and reporting treat an unreadable receipt as no committed result.
std::optional<Json> committed_result_or_none(const fs::path& workspace, const Json& state) {
  try { return committed_result(workspace, state); } catch (const std::exception&) { return {}; }
}
bool coordinator_running(const fs::path& path) {
  try { WorkspaceLock probe(path); return false; }
  catch (const Error& e) { return e.code == "workspace_busy"; }
}
// Called under admission. A nonterminal record whose coordinator no longer owns
// it is resolved from durable evidence only: result.json, then a committed receipt.
void recover_job(const fs::path& workspace, const fs::path& path, JobRecord& record) {
  auto& state = record.state;
  if (terminal(state) || now_ms() - state.at("submitted_at_unix_ms").get<std::int64_t>() < recovery_grace_ms) return;
  std::unique_ptr<WorkspaceLock> owner;
  try { owner = std::make_unique<WorkspaceLock>(path); }
  catch (const Error& e) { if (e.code == "workspace_busy") return; throw; }
  std::optional<StoredResult> stored;
  try { stored = read_stored_result(path, state); } catch (const std::exception&) {}
  if (stored) {
    state["result_sha256"] = stored->sha256; state["result_bytes"] = stored->bytes;
    state["state"] = "succeeded"; state["progress"] = 1.0; state.erase("error");
  } else if (const auto result = committed_result_or_none(workspace, state)) {
    state["state"] = "succeeded"; state["progress"] = 1.0; state.erase("error");
    // Without result.json, reporting rereads the committed receipt.
    try { attach_result(path, state, *result); } catch (const std::exception&) {}
  } else {
    state["state"] = fs::exists(path / "cancel") ? "cancelled" : "interrupted";
    state["error"] = Error("job_interrupted", "Coordinator stopped before a durable result; resubmit the same request_id to retry safely").json();
  }
  save_job(path, record);
}
// The public job, with its result when succeeded. A missing or damaged result is
// reported explicitly; a committed mutation is reread from its receipt.
Json job_view(const fs::path& workspace, const fs::path& path, const JobRecord& record) {
  auto view = public_job(record.state);
  if (record.state.at("state") != "succeeded") return view;
  if (record.result) { view["result"] = *record.result; return view; }
  try { view["result"] = read_stored_result(path, record.state).result; }
  catch (const std::exception& e) {
    if (const auto result = committed_result_or_none(workspace, record.state)) view["result"] = *result;
    else { view["state"] = "failed"; view["error"] = corrupt("Job result is missing or corrupt: " + std::string(e.what())).json(); }
  }
  return view;
}
struct JobEntry {
  std::string id;
  fs::path path;
  std::optional<JobRecord> record;
  Json view;                 // null for an incomplete submission (not yet a job)
  bool active = false;       // counts toward admission
  bool collectable = false;  // terminal, corrupt or incomplete directory
  bool removed = false;
  std::int64_t timestamp = 0;
};
// Reads every job's small state record under admission. No record can make this
// throw: damaged entries are reported, and count as active only while a live
// coordinator still owns them.
std::vector<JobEntry> scan_jobs(const fs::path& workspace, const fs::path& root) {
  std::vector<JobEntry> entries;
  std::error_code error;
  fs::directory_iterator iterator(root, error);
  for (; !error && iterator != fs::directory_iterator(); iterator.increment(error)) {
    JobEntry entry;
    entry.id = path_to_utf8(iterator->path().filename()); entry.path = iterator->path();
    // The admission lock, temporaries and collection leftovers are not jobs.
    try { identifier(entry.id); } catch (const Error&) { continue; }
    std::error_code status_error;
    const auto status = fs::symlink_status(entry.path, status_error);
    if (status_error || !fs::is_directory(status)) {
      entry.view = corrupt_view(entry.id, 0, corrupt(fs::is_symlink(status) ?
        "Managed job directories cannot be symlinks" : "Job record is not a directory"));
      entries.push_back(std::move(entry)); continue;
    }
    if (!fs::exists(fs::symlink_status(entry.path / "state.json", status_error))) {
      entry.collectable = true; entry.timestamp = file_time_ms(entry.path);
      entries.push_back(std::move(entry)); continue;
    }
    try {
      auto record = load_job(entry.path, entry.id);
      // A failed recovery leaves the stored state for a later pass.
      try { recover_job(workspace, entry.path, record); } catch (const std::exception&) {}
      entry.active = !terminal(record.state);
      entry.collectable = !entry.active;
      entry.timestamp = record.state.value("updated_at_unix_ms", record.state.at("submitted_at_unix_ms").get<std::int64_t>());
      entry.view = public_job(record.state);
      entry.record = std::move(record);
    } catch (const Error& e) {
      entry.active = coordinator_running(entry.path);
      entry.collectable = !entry.active;
      entry.timestamp = file_time_ms(entry.path / "state.json");
      entry.view = corrupt_view(entry.id, entry.timestamp, e.code == "job_record_corrupt" ? e : corrupt(e.what()));
    }
    entries.push_back(std::move(entry));
  }
  if (error) throw Error("storage_error", "Cannot list job records: " + error.message());
  return entries;
}
// Collection runs under admission, so no submit can start a coordinator for a
// candidate; the ownership probe skips a coordinator that is still exiting.
// Renaming first makes each removal atomic for readers; leftovers are retried.
bool remove_job(const fs::path& root, const fs::path& path) {
  if (coordinator_running(path)) return false;
  std::random_device random;
  const auto trash = root / (".trash-" + std::to_string(random()) + "-" + std::to_string(random()));
  std::error_code error;
  fs::rename(path, trash, error);
  if (error) return false;
  fs::remove_all(trash, error);
  return true;
}
void collect_jobs(const fs::path& root, std::vector<JobEntry>& entries, const std::string& keep) {
  std::vector<fs::path> leftovers;
  std::error_code error;
  for (fs::directory_iterator it(root, error); !error && it != fs::directory_iterator(); it.increment(error)) {
    std::error_code status_error;
    if (path_to_utf8(it->path().filename()).rfind(".trash-", 0) == 0 && fs::is_directory(fs::symlink_status(it->path(), status_error)))
      leftovers.push_back(it->path());
  }
  for (const auto& leftover : leftovers) fs::remove_all(leftover, error);
  std::vector<JobEntry*> candidates;
  for (auto& entry : entries) if (entry.collectable && entry.id != keep) candidates.push_back(&entry);
  std::sort(candidates.begin(), candidates.end(), [](const JobEntry* a, const JobEntry* b) { return a->timestamp > b->timestamp; });
  const auto now = now_ms();
  for (std::size_t rank = 0; rank < candidates.size(); ++rank) {
    auto& entry = *candidates[rank];
    if (rank < max_terminal_jobs && now - entry.timestamp <= terminal_retention_ms) continue;
    entry.removed = remove_job(root, entry.path);
  }
  entries.erase(std::remove_if(entries.begin(), entries.end(), [](const JobEntry& entry) { return entry.removed; }), entries.end());
}
}

void set_worker_executable(const fs::path& executable) { worker_executable = fs::absolute(executable); }
void check_job_cancelled() {
  if (!cancellation_file.empty() && fs::exists(cancellation_file)) throw Error("job_cancelled", "Job was cancelled before publication");
  if (job_deadline && now_ms() >= job_deadline) throw Error("job_timeout", "Job exceeded its wall-time budget before publication");
}
Json job_request_context() { return request_context; }

// SHA-256 binds a caller's request identity to canonical JSON, independently of
// implementation-defined std::hash values and without a runtime crypto library.
std::string request_fingerprint(const std::string& tool, const Json& arguments) {
  auto normalized = arguments; normalized.erase("request_id");
  return sha256(Json{{"tool", tool}, {"arguments", normalized}}.dump());
}
std::string sha256(const std::string& text) {
  static constexpr std::array<std::uint32_t,64> constants = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
  std::array<std::uint32_t,8> hash = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  std::vector<std::uint8_t> bytes(text.begin(), text.end()); const auto bits = static_cast<std::uint64_t>(bytes.size()) * 8;
  bytes.push_back(0x80); while (bytes.size() % 64 != 56) bytes.push_back(0);
  for (int i = 7; i >= 0; --i) bytes.push_back(static_cast<std::uint8_t>(bits >> (i * 8)));
  auto rotate = [](std::uint32_t value, int amount) { return (value >> amount) | (value << (32 - amount)); };
  for (std::size_t offset = 0; offset < bytes.size(); offset += 64) {
    std::array<std::uint32_t,64> words{};
    for (int i = 0; i < 16; ++i) for (int j = 0; j < 4; ++j) words[i] = (words[i] << 8) | bytes[offset + i*4+j];
    for (int i = 16; i < 64; ++i) {
      const auto a = words[i-15], b = words[i-2];
      words[i] = words[i-16] + (rotate(a,7)^rotate(a,18)^(a>>3)) + words[i-7] + (rotate(b,17)^rotate(b,19)^(b>>10));
    }
    auto [a,b,c,d,e,f,g,h] = hash;
    for (int i = 0; i < 64; ++i) {
      const auto first = h + (rotate(e,6)^rotate(e,11)^rotate(e,25)) + ((e&f)^(~e&g)) + constants[i] + words[i];
      const auto second = (rotate(a,2)^rotate(a,13)^rotate(a,22)) + ((a&b)^(a&c)^(b&c));
      h=g; g=f; f=e; e=d+first; d=c; c=b; b=a; a=first+second;
    }
    hash[0]+=a; hash[1]+=b; hash[2]+=c; hash[3]+=d; hash[4]+=e; hash[5]+=f; hash[6]+=g; hash[7]+=h;
  }
  std::ostringstream result; result << std::hex << std::setfill('0');
  for (auto value : hash) result << std::setw(8) << value;
  return result.str();
}

// A cached per-view projection is trusted only if it carries everything a drawing
// needs from it, including what it cost against the drawing-wide limits.
bool valid_projection_entry(const std::optional<Json>& entry) {
  return entry && entry->contains("views") && entry->at("views").size()==1 && entry->contains("tolerance_mm") &&
    entry->contains("budget") && entry->at("budget").is_object();
}
Json projection_definition(const Json& view) {
  auto definition=view; definition.erase("id"); return definition;
}
// All workers run the same executable; the exit status and result file are the
// only things a coordinator trusts, whether it started one worker or several.
Json worker_response(const fs::path& directory, int status) {
  if (status == 124)
    throw Error("job_timeout", "Geometry worker exceeded its wall-time budget");
  if (status == 137)
    throw Error("memory_limit", "Geometry worker exceeded its memory budget");
  if (!fs::exists(directory / "output.json"))
    throw Error("worker_failed", "Geometry worker exited without a result", {{"exit_status", status}, {"memory_mb", current_budget.at("memory_mb")}});
  auto response = read_result(directory / "output.json");
  if (response.contains("error")) {
    const auto& error = response.at("error");
    throw Error(error.at("code"), error.at("message"), error.value("details", Json::object()));
  }
  if (status != 0) throw Error("worker_failed", "Geometry worker failed", {{"exit_status", status}});
  return response;
}
std::unique_ptr<WorkspaceLock> try_worker_slot(const fs::path& workspace) {
  for (int index = 0; index < max_workers; ++index) {
    const auto path = workspace / ".workers" / (".slot-" + std::to_string(index)); directory(path);
    try { return std::make_unique<WorkspaceLock>(path); }
    catch (const Error& e) { if (e.code != "workspace_busy") throw; }
  }
  return nullptr;
}
std::int64_t remaining_ms(std::chrono::steady_clock::time_point started) {
  const auto total = current_budget.at("timeout_ms").get<std::int64_t>();
  const auto used = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
  return total - used;
}
// A cache entry a coordinator publishes only after the whole evaluation succeeds.
struct PendingPublication {
  std::shared_ptr<TemporaryDirectory> files;
  std::string file, key;
};
// Views projected ahead of the final worker, by separate processes.
struct ParallelProjection {
  Json supplied = Json::array();   // [{index, view, budget, tolerance_mm}]
  Json summary;                    // the model summary a projecting worker returned
  std::vector<PendingPublication> publications;
  int workers = 1;                 // peak number of workers running at once
};
// Hidden-line removal dominates a drawing and is independent per view, so views
// that are not cached are projected by separate worker processes. Every process is
// still serial inside and bounded like any other worker; this only uses worker
// slots that are idle at the moment, never waits for one, and never holds more than
// the workspace-wide four. The caller's own slot runs the first view.
ParallelProjection project_in_parallel(const fs::path& workspace, const Json& model, const Json& drawing,
                                       const std::vector<std::size_t>& missing,
                                       std::chrono::steady_clock::time_point started) {
  struct Active {
    std::size_t view;
    std::shared_ptr<TemporaryDirectory> files;
    std::unique_ptr<Child> child;
    std::unique_ptr<WorkspaceLock> slot;   // null: the caller's own slot
  };
  ParallelProjection result;
  std::deque<std::size_t> pending(missing.begin(), missing.end());
  std::vector<Active> active;
  bool own_slot_free = true, geometry_publication = false;
  std::size_t peak = 0;
  try {
    while (!pending.empty() || !active.empty()) {
      check_job_cancelled();
      if (remaining_ms(started) <= 0) throw Error("job_timeout", "Geometry workers exceeded their wall-time budget");
      while (!pending.empty()) {
        std::unique_ptr<WorkspaceLock> slot;
        if (!own_slot_free) { slot = try_worker_slot(workspace); if (!slot) break; }
        const auto view = pending.front(); pending.pop_front();
        auto files = std::make_shared<TemporaryDirectory>(workspace);
        auto limits = current_budget; limits["timeout_ms"] = std::max<std::int64_t>(1, remaining_ms(started));
        const Json single = {{"kind","projection"},{"drawing",{{"views",Json::array({drawing.at("views").at(view)})},
          {"hidden_lines",drawing.at("hidden_lines")}}}};
        atomic_text(files->path / "input.json", Json{{"model", model}, {"request", single}, {"budget", limits},
          {"cache_root",path_to_utf8(workspace/".cache")}}.dump());
        auto child = std::make_unique<Child>(launch({"--internal-geometry-worker", files->path / "input.json", files->path / "output.json"},
          true, current_budget.at("memory_mb")));
        active.push_back({view, files, std::move(child), std::move(slot)});
        try { atomic_text(files->path / "process.json", Json{{"pid",active.back().child->id()}}.dump()); }
        catch (...) { /* Only a diagnostic for operators; the worker is already running. */ }
        own_slot_free = false;
        peak = std::max(peak, active.size());
      }
      for (auto it = active.begin(); it != active.end();) {
        int status = 0;
        if (!it->child->done(status)) { ++it; continue; }
        const auto response = worker_response(it->files->path, status);
        const auto& produced = response.at("result").at("projected").at(0);
        result.supplied.push_back({{"index",it->view},{"view",produced.at("view")},{"budget",produced.at("budget")},
          {"tolerance_mm",produced.at("tolerance_mm")}});
        if (result.summary.is_null()) result.summary = response.at("result").at("summary");
        const auto& diagnostics = response.at("cache");
        if (!diagnostics.at("projection_hits").at(0).get<bool>())
          result.publications.push_back({it->files, "projection-0.cache", diagnostics.at("projection_keys").at(0).get<std::string>()});
        if (!geometry_publication && !diagnostics.at("geometry_hit").get<bool>()) {
          geometry_publication = true;
          result.publications.push_back({it->files, "geometry.cache", diagnostics.at("geometry_key").get<std::string>()});
        }
        if (!it->slot) own_slot_free = true;
        it = active.erase(it);
      }
      if (!pending.empty() || !active.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  } catch (...) { for (auto& worker : active) worker.child->stop(); throw; }
  result.workers = static_cast<int>(peak);
  return result;
}

Json evaluate_model(const fs::path& workspace, const Json& model, const Json& request, Json* cache_diagnostics) {
  check_job_cancelled();
  const auto started = std::chrono::steady_clock::now();
  directory(workspace / ".workers");
  std::unique_ptr<WorkspaceLock> slot;
  while (!slot) {
    check_job_cancelled();
    slot = try_worker_slot(workspace);
    if (!slot) {
      if (request_context.empty()) throw Error("queue_full", "All four geometry workers are occupied; retry or submit a job");
      if (std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(current_budget.at("timeout_ms").get<int>()))
        throw Error("job_timeout", "Job exceeded its wall-time budget while queued");
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  // Views of one drawing that are not cached are projected by separate workers
  // first, when at least two are missing; a malformed request skips this and
  // reaches the single worker, which reports it.
  std::optional<ParallelProjection> parallel;
  std::vector<std::size_t> missing;
  const Json* drawing = nullptr;
  try {
    if (request.is_object() && request.value("kind", std::string()) == "drawing" && request.contains("drawing")) {
      drawing = &request.at("drawing");
      const auto& views = drawing->at("views");
      if (views.is_array() && views.size() >= 2 && views.size() <= 6 && drawing->at("hidden_lines").is_boolean()) {
        const auto geometry_key = geometry_cache_key(model);
        for (std::size_t i = 0; i < views.size(); ++i) {
          if (!views.at(i).is_object()) { missing.clear(); break; }
          const auto key = projection_cache_key(geometry_key, {{"views",Json::array({projection_definition(views.at(i))})},
            {"hidden_lines",drawing->at("hidden_lines")}});
          if (!valid_projection_entry(read_cache(workspace / ".cache", key))) missing.push_back(i);
        }
      }
    }
  } catch (const Json::exception&) { missing.clear(); }
  if (missing.size() >= 2) parallel = project_in_parallel(workspace, model, *drawing, missing, started);
  TemporaryDirectory files(workspace);
  atomic_text(files.path / "input.json", Json{{"model", model}, {"request", request}, {"budget", current_budget},
    {"cache_root",path_to_utf8(workspace/".cache")}}.dump());
  if (parallel) atomic_text(files.path / "precomputed.json", Json{{"views",parallel->supplied},{"summary",parallel->summary}}.dump());
  auto child = launch({"--internal-geometry-worker", files.path / "input.json", files.path / "output.json"}, true, current_budget.at("memory_mb"));
  try {
    atomic_text(files.path / "process.json", Json{{"pid",child.id()}}.dump());
    int status = 0;
    while (!child.done(status)) {
      check_job_cancelled();
      if (std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(current_budget.at("timeout_ms").get<int>()))
        throw Error("job_timeout", "Geometry worker exceeded its wall-time budget");
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check_job_cancelled();
    const auto response = worker_response(files.path, status);
    // Only the coordinator publishes shared cache files. Never publish results
    // from failed, timed-out or cancelled workers. Cache entries carry no HEAD,
    // document identity, output path, or evaluation identity.
    check_job_cancelled();
    const auto& produced=response.at("cache");
    if(produced.contains("geometry_key")) {
      check_job_cancelled();
      publish_cache(workspace/".cache",files.path/"geometry.cache",produced.at("geometry_key"));
    }
    if(produced.contains("projection_keys") && produced.at("projection_keys").size()<=6)
      for(std::size_t i=0;i<produced.at("projection_keys").size();++i) {
        check_job_cancelled();
        publish_cache(workspace/".cache",files.path/("projection-"+std::to_string(i)+".cache"),produced.at("projection_keys").at(i));
      }
    // The views projected ahead of the final worker, published like any other.
    if(parallel) for(const auto& entry:parallel->publications) {
      check_job_cancelled();
      publish_cache(workspace/".cache",entry.files->path/entry.file,entry.key);
    }
    check_job_cancelled();
    if(cache_diagnostics) { *cache_diagnostics=response.at("cache"); (*cache_diagnostics)["projection_workers"]=parallel ? parallel->workers : 1; }
    return response.at("result");
  } catch (...) { child.stop(); throw; }
}

int geometry_worker_main(const fs::path& input, const fs::path& output) {
  try {
    const auto payload = parse_json(read_text(input)); fields(payload, {"model", "request", "budget"}, {"cache_root"});
    const auto limits = budget(payload.at("budget"));
#if !defined(_WIN32) && !defined(__APPLE__)
    const auto bytes = static_cast<rlim_t>(limits.at("memory_mb").get<int>()) * 1024 * 1024;
    struct rlimit memory{bytes, bytes};
    if (::setrlimit(RLIMIT_AS, &memory) != 0) throw Error("worker_failed", "Cannot install worker memory budget");
    struct rlimit core{0,0}; ::setrlimit(RLIMIT_CORE, &core);
    // Enforce a second wall deadline even if the coordinator itself dies.
    ::alarm(static_cast<unsigned>((limits.at("timeout_ms").get<int>() + 999) / 1000));
#ifdef __linux__
    if (::prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) throw Error("worker_failed", "Cannot install worker parent-death guard");
#endif
#endif
    WorkerWatchdog watchdog(limits);
    const auto& request = payload.at("request");
    fields(request, {"kind"}, {"feature_id", "format", "path", "drawing", "identity"});
    const auto kind = text_field(request, "kind");
    atomic_text(output.parent_path() / "building.json", Json{{"phase","building"}}.dump());
    validate_model(payload.at("model"));
    const auto cache_root=payload.contains("cache_root") ? path_from_utf8(text_field(payload,"cache_root")) : output.parent_path()/"unused-cache";
    const auto geometry_key=geometry_cache_key(payload.at("model"));
    auto cached=read_cache(cache_root,geometry_key);
    if(cached && (!cached->contains("snapshot") || !cached->contains("summary"))) cached.reset();
    Json diagnostics={{"geometry_key",geometry_key},{"geometry_hit",cached.has_value()},{"projection_hit",false}};
    std::unique_ptr<BuiltModel> built;
    auto geometry=[&]() -> BuiltModel& {
      if(!built && cached) {
        try { built=std::make_unique<BuiltModel>(payload.at("model"),cached->at("snapshot")); }
        catch(const std::exception&) { cached.reset(); diagnostics["geometry_hit"]=false; }
      }
      if(!built) built=std::make_unique<BuiltModel>(payload.at("model"));
      return *built;
    };
    const auto feature = request.value("feature_id", std::string{});
    Json result=Json::object();
    if (kind == "topology" || kind == "view") result["topology"] = geometry().topology(feature);
    if (kind == "view") result["mesh"] = geometry().mesh(feature);
    // Views the coordinator projected ahead of this worker, in other processes.
    std::map<std::size_t,Json> supplied;
    std::optional<Json> precomputed;
    if (const auto path=input.parent_path()/"precomputed.json"; fs::exists(path)) {
      precomputed=parse_json(read_text(path,max_result_bytes),max_result_bytes);
      for (const auto& entry:precomputed->at("views")) supplied.emplace(entry.at("index").get<std::size_t>(),entry);
    }
    // Resolves each requested view from the cache, from `supplied`, or by projecting it now.
    // Each view is cached on its own: its key covers the geometry, the view's
    // definition (not its presentation name) and the hidden-line choice, so a
    // new, removed, reordered or edited view projects only itself.
    struct Projection { std::vector<Json> views, budgets; Json tolerance; };
    const auto project_views=[&](const Json& requested,const Json& hidden_lines) {
      Projection out; out.views.resize(requested.size()); out.budgets.resize(requested.size());
      Json keys=Json::array(), hits=Json::array(), missing=Json::array();
      std::vector<std::size_t> missing_index;
      for (std::size_t i=0;i<requested.size();++i) {
        keys.push_back(projection_cache_key(geometry_key,{{"views",Json::array({projection_definition(requested.at(i))})},{"hidden_lines",hidden_lines}}));
        if (const auto given=supplied.find(i); given!=supplied.end()) {
          out.views[i]=given->second.at("view"); out.budgets[i]=given->second.at("budget"); out.tolerance=given->second.at("tolerance_mm");
          hits.push_back(false); continue;
        }
        auto entry=read_cache(cache_root,keys.at(i).get<std::string>());
        if (valid_projection_entry(entry)) {
          out.views[i]=entry->at("views").at(0); out.tolerance=entry->at("tolerance_mm"); out.budgets[i]=entry->at("budget"); hits.push_back(true);
        } else { hits.push_back(false); missing.push_back(requested.at(i)); missing_index.push_back(i); }
      }
      diagnostics["projection_keys"]=keys; diagnostics["projection_hits"]=hits;
      diagnostics["projection_hit"]=missing.empty() && supplied.empty();
      if (!missing.empty()) {
        const auto fresh=geometry().drawing({{"views",missing},{"hidden_lines",hidden_lines}});
        out.tolerance=fresh.at("tolerance_mm");
        for (std::size_t j=0;j<missing_index.size();++j) {
          const auto i=missing_index[j];
          out.views[i]=fresh.at("views").at(j); out.budgets[i]=fresh.at("view_budgets").at(j);
          stage_cache(output.parent_path()/("projection-"+std::to_string(i)+".cache"),keys.at(i).get<std::string>(),
            {{"views",Json::array({out.views[i]})},{"tolerance_mm",out.tolerance},{"budget",out.budgets[i]}});
        }
      }
      return out;
    };
    if (kind == "projection") {
      // Internal: one coordinator-assigned view; returns the raw projection and its cost.
      const auto& drawing=request.at("drawing");
      const auto projection=project_views(drawing.at("views"),drawing.at("hidden_lines"));
      result["projected"]=Json::array();
      for (std::size_t i=0;i<projection.views.size();++i)
        result["projected"].push_back({{"view",projection.views[i]},{"budget",projection.budgets[i]},{"tolerance_mm",projection.tolerance}});
    }
    if (kind == "drawing") {
      const auto& drawing=request.at("drawing");
      const auto& requested=drawing.at("views");
      auto projection=project_views(requested,drawing.at("hidden_lines"));
      Json assembled={{"views",Json::array()},{"tolerance_mm",projection.tolerance}};
      // Recipe names are presentation, and can change without projecting again.
      for (std::size_t i=0;i<projection.views.size();++i) {
        projection.views[i]["id"]=requested.at(i).at("id"); assembled["views"].push_back(std::move(projection.views[i]));
      }
      check_drawing_totals(requested,Json(projection.budgets),text_field(payload.at("model"),"output"));
      result["drawing"]=render_drawing(assembled,drawing,request.at("identity"));
    }
    if (kind == "export") geometry().export_file(path_from_utf8(text_field(request,"path")), text_field(request,"format"));
    else if (kind != "summary" && kind != "topology" && kind != "view" && kind != "drawing" && kind != "projection") throw Error("invalid_argument", "Unknown geometry worker request");
    // A coordinator that projected views ahead of this worker also supplies the model
    // summary and publishes the geometry entry, so this worker need not rebuild it.
    result["summary"] = cached && feature.empty() ? cached->at("summary") :
      precomputed && feature.empty() ? precomputed->at("summary") : geometry().summary(feature);
    if(!cached && !precomputed) {
      try { stage_cache(output.parent_path()/"geometry.cache",geometry_key,
        {{"snapshot",geometry().snapshot()},{"summary",geometry().summary()}}); }
      catch(const std::exception&) { /* Oversize snapshots simply rebuild next time. */ }
    }
    write_result(output, {{"result", result},{"cache",diagnostics}}); return 0;
  } catch (const Error& e) { try { write_result(output, {{"error", e.json()}}); } catch (...) {} }
  catch (const std::exception& e) { try { write_result(output, {{"error", Error("worker_failed", e.what()).json()}}); } catch (...) {} }
  return 1;
}

Json dispatch_job(const fs::path& workspace, const Json& arguments) {
#ifndef _WIN32
  for (auto it = background_children.begin(); it != background_children.end();) {
    if (::waitpid(*it, nullptr, WNOHANG) == *it) it = background_children.erase(it); else ++it;
  }
#endif
  const auto action = text_field(arguments, "action");
  if (action == "submit") fields(arguments, {"action", "request_id", "tool", "arguments"}, {"budget"});
  else if (action == "list") fields(arguments, {"action"});
  else if (action == "get" || action == "cancel") fields(arguments, {"action", "job_id"});
  else throw Error("invalid_argument", "Job action must be submit, get, cancel or list");
  directory(workspace / "jobs"); const auto root = workspace / "jobs"; WorkspaceLock admission(root);
  if (action == "list") {
    auto entries = scan_jobs(workspace, root);
    Json jobs = Json::array();
    for (auto& entry : entries) {
      if (entry.record) migrate_job(entry.path, *entry.record);
      if (!entry.view.is_null() && jobs.size() < 1000) jobs.push_back(entry.view);
    }
    return {{"jobs",jobs}, {"limit",1000}};
  }
  const auto id = text_field(arguments, action == "submit" ? "request_id" : "job_id");
  if (action == "submit") portable_identifier(id); else identifier(id);
  const auto path = root / id;
  if (fs::is_symlink(fs::symlink_status(path))) throw Error("storage_error", "Managed job directories cannot be symlinks");
  if (action != "submit") {
    JobRecord record;
    try { record = load_job(path, id); }
    catch (const Error& e) {
      if (e.code != "job_record_corrupt") throw;
      return corrupt_view(id, file_time_ms(path / "state.json"), e);
    }
    recover_job(workspace, path, record);
    if (action == "cancel" && !terminal(record.state)) {
      // A cancellation and commit share the document lock. Whichever obtains it
      // first defines whether the durable mutation can publish.
      std::unique_ptr<DocumentLock> publication;
      if (record.state.contains("document_id"))
        publication = std::make_unique<DocumentLock>(workspace, record.state.at("document_id").get<std::string>());
      if (const auto result = committed_result(workspace, record.state)) {
        record.state["state"] = "succeeded"; record.state["progress"] = 1.0;
        try { attach_result(path, record.state, *result); } catch (const std::exception&) {}
      } else { atomic_text(path / "cancel", "cancel\n"); record.state["state"] = "cancelling"; }
      save_job(path, record);
    }
    auto view = job_view(workspace, path, record);
    migrate_job(path, record);
    return view;
  }
  const auto tool = text_field(arguments, "tool");
  if (!mutation(tool) && tool != "cad_query" && tool != "cad_export" && tool != "cad_drawing" && tool != "cad_bom" && tool != "cad_preview" && tool != "cad_view")
    throw Error("invalid_argument", "This tool cannot be submitted as a geometry job");
  auto input = arguments.at("arguments"); if (!input.is_object()) throw Error("invalid_argument", "Job arguments must be an object");
  if (input.contains("request_id") && input.at("request_id") != id)
    throw Error("invalid_argument", "Nested request_id must match the job request_id");
  if (mutation(tool)) input["request_id"] = id;
  // Malformed arguments fail now, with the code a direct call would return.
  validate_tool_arguments(tool, input);
  const auto fingerprint = request_fingerprint(tool, input);
  const auto limits = budget(arguments.value("budget", Json::object()));
  bool retry = false;
  std::error_code status_error;
  if (fs::exists(fs::symlink_status(path / "state.json", status_error))) {
    JobRecord record;
    try { record = load_job(path, id); }
    catch (const Error& e) {
      // A damaged record keeps its identity; a deliberate retry uses a new request_id.
      if (e.code != "job_record_corrupt") throw;
      return corrupt_view(id, file_time_ms(path / "state.json"), e);
    }
    if (record.state.at("fingerprint") != fingerprint || record.state.at("budget") != limits)
      throw Error("request_conflict", "request_id already belongs to a different job request");
    recover_job(workspace, path, record);
    if (record.state.at("state") != "interrupted") {
      auto view = job_view(workspace, path, record);
      migrate_job(path, record);
      return view;
    }
    retry = true;
  }
  auto entries = scan_jobs(workspace, root);
  collect_jobs(root, entries, id);
  int active = 0;
  for (auto& entry : entries) {
    if (entry.active) ++active;
    if (entry.record) migrate_job(entry.path, *entry.record);
  }
  if (active >= max_active_jobs) throw Error("queue_full", "At most eight geometry jobs may be queued or running");
  directory(path);
  // A retried identity never trusts an earlier attempt's result file.
  if (retry) { std::error_code ignored; fs::remove(path / "result.json", ignored); }
  const auto submitted = now_ms();
  JobRecord record;
  record.state = {{"schema_version", job_schema_version}, {"job_id", id}, {"request_id", id}, {"tool", tool},
    {"fingerprint", fingerprint}, {"budget", limits}, {"state", "queued"}, {"progress", 0.0},
    {"submitted_at_unix_ms", submitted}, {"deadline_unix_ms", submitted + limits.at("timeout_ms").get<int>()}, {"retried", retry}};
  if (const auto document = document_of(input)) record.state["document_id"] = *document;
  write_request(path, record.state, input);
  save_job(path, record);
  try {
    auto child = launch({"--internal-job-worker", workspace, id}, false, 0);
#ifndef _WIN32
    background_children.push_back(child.pid);
#endif
  } catch (const Error& e) { record.state["state"] = "interrupted"; record.state["error"] = e.json(); save_job(path, record); throw; }
  return public_job(record.state);
}

int job_worker_main(const fs::path& workspace, const std::string& job_id) {
  const auto root = workspace / "jobs"; const auto path = root / job_id;
  // Stdout is reserved; a detached coordinator's stderr is the null device.
  const auto log = [&](const std::string& message) {
    try { std::cerr << Json{{"job_id", job_id}, {"coordinator_error", message}}.dump() << '\n'; } catch (...) {}
  };
  std::unique_ptr<WorkspaceLock> owner;
  try { identifier(job_id); owner = std::make_unique<WorkspaceLock>(path); }
  catch (const std::exception& e) { log(std::string("Cannot own job: ") + e.what()); return 1; }
  JobRecord record;
  bool loaded = false;
  try {
    Json input;
    {
      const auto admission = wait_lock(root);
      record = load_job(path, job_id); loaded = true;
      if (terminal(record.state)) return 0;
      input = request_arguments(path, record);
    }
    cancellation_file = path / "cancel"; current_budget = budget(record.state.at("budget"));
    job_deadline = record.state.at("deadline_unix_ms").get<std::int64_t>();
    request_context = {{"request_id", job_id}, {"fingerprint", record.state.at("fingerprint")}};
    const auto tool = record.state.at("tool").get<std::string>();
    std::optional<Json> result;
    try {
      if (const auto committed = committed_result(workspace, record.state)) result = *committed;
      else {
        check_job_cancelled();
        {
          const auto admission = wait_lock(root);
          record.state["state"] = "running"; record.state["progress"] = 0.1; save_job(path, record);
        }
        Service service(workspace);
        result = service.call(tool, input);
      }
    } catch (const Error& e) {
      // Publication may have succeeded before an OS durability/reporting failure.
      if (const auto committed = committed_result_or_none(workspace, record.state)) result = *committed;
      else { record.state["state"] = e.code == "job_cancelled" ? "cancelled" : "failed"; record.state["error"] = e.json(); }
    } catch (const std::exception& e) {
      record.state["state"] = "failed"; record.state["error"] = Error("internal_error", e.what()).json();
    }
    if (result) {
      // The result is durable before the state that reports it. A committed
      // mutation stays successful even without the file: its receipt is reread.
      try { attach_result(path, record.state, *result); }
      catch (const Error& e) {
        if (!(mutation(tool) && committed_result_or_none(workspace, record.state))) {
          result.reset(); record.state["state"] = "failed"; record.state["error"] = e.json();
        }
      }
      if (result) { record.state["state"] = "succeeded"; record.state["progress"] = 1.0; record.state.erase("error"); }
    }
    {
      // A brief contender may hold admission while reading; never lose a
      // completed result merely because polling overlaps this atomic state write.
      const auto admission = wait_lock(root);
      save_job(path, record);
    }
    return record.state.at("state") == "succeeded" ? 0 : 1;
  } catch (const std::exception& e) {
    log(std::string("Coordinator failed: ") + e.what());
    if (!loaded) return 1;
    // A durable result.json or committed receipt is recovered as success.
    // Anything else is persisted as an explicit failure.
    if (record.state.at("state") == "succeeded") return 1;
    if (!terminal(record.state)) {
      record.state["state"] = "failed";
      const auto* error = dynamic_cast<const Error*>(&e);
      record.state["error"] = error ? error->json() : Error("internal_error", e.what()).json();
    }
    try { const auto admission = wait_lock(root); save_job(path, record); }
    catch (const std::exception& again) { log(std::string("Cannot persist coordinator failure: ") + again.what()); }
    return 1;
  }
}
}
