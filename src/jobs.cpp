#include "agentcad/jobs.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/service.hpp"
#include "agentcad/drawing.hpp"
#include "agentcad/cache.hpp"
#include "agentcad/model.hpp"
#include "agentcad/runtime.hpp"
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
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
constexpr int max_active_jobs = 8;
constexpr int max_workers = 4;
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
bool terminal(const Json& state) {
  const auto value = state.at("state").get<std::string>();
  return value == "succeeded" || value == "failed" || value == "cancelled" || value == "interrupted";
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
  if (!fs::is_regular_file(path) || fs::is_symlink(path) || fs::file_size(path) > max_result_bytes)
    throw Error("worker_failed", "Worker output missing or exceeds 64 MiB");
  std::ifstream stream(path, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(stream)), {});
  if (text.size() > max_result_bytes) throw Error("limit_exceeded", "Worker output exceeds 64 MiB");
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
    path = temporary_file(root / ".workers");
    fs::remove(path); directory(path);
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
  STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = null_handle; startup.hStdOutput = null_handle; startup.hStdError = null_handle;
  PROCESS_INFORMATION process{};
  const bool created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
    CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &process);
  CloseHandle(null_handle);
  if (!created) throw Error("worker_failed", "Cannot launch native worker: " + std::to_string(GetLastError()));
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
Json public_job(Json state) {
  state.erase("arguments"); state.erase("fingerprint"); state.erase("deadline_unix_ms");
  return state;
}
void save_job(const fs::path& path, Json& state) {
  state["updated_at_unix_ms"] = now_ms();
  const auto temporary = temporary_file(path);
  try { write_result(temporary, state); publish_file(temporary, path / "state.json"); }
  catch (...) { std::error_code ignored; fs::remove(temporary, ignored); throw; }
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
  if (state.at("tool") != "cad_create" && state.at("tool") != "cad_apply" && state.at("tool") != "cad_restore" && state.at("tool") != "cad_import") return {};
  return Store(workspace).request_replay(state.at("arguments").at("document_id"),
    state.at("request_id"), state.at("fingerprint"));
}
void recover_job(const fs::path& workspace, const fs::path& path, Json& state) {
  if (terminal(state) || now_ms() - state.at("submitted_at_unix_ms").get<std::int64_t>() < 5000) return;
  std::unique_ptr<WorkspaceLock> owner;
  try { owner = std::make_unique<WorkspaceLock>(path); }
  catch (const Error& e) { if (e.code == "workspace_busy") return; throw; }
  if (const auto result = committed_result(workspace, state)) {
    state["state"] = "succeeded"; state["progress"] = 1.0; state["result"] = *result;
  } else {
    state["state"] = fs::exists(path / "cancel") ? "cancelled" : "interrupted";
    state["error"] = Error("job_interrupted", "Coordinator stopped before a durable result; resubmit the same request_id to retry safely").json();
  }
  save_job(path, state);
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

Json evaluate_model(const fs::path& workspace, const Json& model, const Json& request, Json* cache_diagnostics) {
  check_job_cancelled();
  const auto started = std::chrono::steady_clock::now();
  directory(workspace / ".workers");
  std::unique_ptr<WorkspaceLock> slot;
  while (!slot) {
    check_job_cancelled();
    for (int index = 0; index < max_workers; ++index) {
      const auto path = workspace / ".workers" / (".slot-" + std::to_string(index)); directory(path);
      try { slot = std::make_unique<WorkspaceLock>(path); break; }
      catch (const Error& e) { if (e.code != "workspace_busy") throw; }
    }
    if (!slot) {
      if (request_context.empty()) throw Error("queue_full", "All four geometry workers are occupied; retry or submit a job");
      if (std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(current_budget.at("timeout_ms").get<int>()))
        throw Error("job_timeout", "Job exceeded its wall-time budget while queued");
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  TemporaryDirectory files(workspace);
  atomic_text(files.path / "input.json", Json{{"model", model}, {"request", request}, {"budget", current_budget},
    {"cache_root",path_to_utf8(workspace/".cache")}}.dump());
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
    if (status == 124)
      throw Error("job_timeout", "Geometry worker exceeded its wall-time budget");
    if (status == 137)
      throw Error("memory_limit", "Geometry worker exceeded its memory budget");
    if (!fs::exists(files.path / "output.json"))
      throw Error("worker_failed", "Geometry worker exited without a result", {{"exit_status", status}, {"memory_mb", current_budget.at("memory_mb")}});
    const auto response = read_result(files.path / "output.json");
    if (response.contains("error")) {
      const auto& error = response.at("error");
      throw Error(error.at("code"), error.at("message"), error.value("details", Json::object()));
    }
    if (status != 0) throw Error("worker_failed", "Geometry worker failed", {{"exit_status", status}});
    // Only the coordinator publishes shared cache files. Never publish results
    // from failed, timed-out or cancelled workers. Cache entries carry no HEAD,
    // document identity, output path, or evaluation identity.
    check_job_cancelled();
    for(const auto* type:{"geometry","projection"}) {
      if(response.at("cache").contains(std::string(type)+"_key")) {
        check_job_cancelled();
        publish_cache(workspace/".cache",files.path/(std::string(type)+".cache"),
          response.at("cache").at(std::string(type)+"_key"));
      }
    }
    check_job_cancelled();
    if(cache_diagnostics) *cache_diagnostics=response.at("cache");
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
    if (kind == "drawing") {
      const auto& drawing=request.at("drawing");
      const Json spec={{"views",drawing.at("views")},{"hidden_lines",drawing.at("hidden_lines")}};
      auto key_spec=spec;
      for(auto& view:key_spec["views"]) view.erase("id");
      const auto projection_key=projection_cache_key(geometry_key,key_spec);
      diagnostics["projection_key"]=projection_key;
      auto projected=read_cache(cache_root,projection_key);
      if(projected && (!projected->contains("views") || projected->at("views").size()!=drawing.at("views").size())) projected.reset();
      diagnostics["projection_hit"]=projected.has_value();
      if(!projected) {
        projected=geometry().drawing(spec);
        stage_cache(output.parent_path()/"projection.cache",projection_key,*projected);
      }
      // Recipe names are presentation, and can change without projecting again.
      for(std::size_t i=0;i<drawing.at("views").size();++i)
        projected->at("views").at(i)["id"]=drawing.at("views").at(i).at("id");
      result["drawing"]=render_drawing(*projected,drawing,request.at("identity"));
    }
    if (kind == "export") geometry().export_file(path_from_utf8(text_field(request,"path")), text_field(request,"format"));
    else if (kind != "summary" && kind != "topology" && kind != "view" && kind != "drawing") throw Error("invalid_argument", "Unknown geometry worker request");
    result["summary"] = cached && feature.empty() ? cached->at("summary") : geometry().summary(feature);
    if(!cached) {
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
    Json jobs = Json::array();
    for (const auto& entry : fs::directory_iterator(root)) {
      if (entry.is_symlink()) throw Error("storage_error", "Managed job directories cannot be symlinks");
      if (!entry.is_directory() || !fs::exists(entry.path() / "state.json")) continue;
      auto state = read_result(entry.path() / "state.json"); recover_job(workspace, entry.path(), state);
      auto item = public_job(state); item.erase("result"); jobs.push_back(item);
      if (jobs.size() >= 1000) break;
    }
    return {{"jobs",jobs}, {"limit",1000}};
  }
  const auto id = text_field(arguments, action == "submit" ? "request_id" : "job_id"); identifier(id);
  const auto path = root / id;
  if (fs::is_symlink(fs::symlink_status(path))) throw Error("storage_error", "Managed job directories cannot be symlinks");
  if (action != "submit") {
    auto state = read_result(path / "state.json"); recover_job(workspace, path, state);
    if (action == "cancel" && !terminal(state)) {
      // A cancellation and commit share the document lock. Whichever obtains it
      // first defines whether the durable mutation can publish.
      std::unique_ptr<DocumentLock> publication;
      if (state.at("arguments").contains("document_id"))
        publication = std::make_unique<DocumentLock>(workspace, state.at("arguments").at("document_id"));
      if (const auto result = committed_result(workspace, state)) {
        state["state"] = "succeeded"; state["result"] = *result; state["progress"] = 1.0;
      } else { atomic_text(path / "cancel", "cancel\n"); state["state"] = "cancelling"; }
      save_job(path, state);
    }
    return public_job(state);
  }
  const auto tool = text_field(arguments, "tool");
    if (tool != "cad_create" && tool != "cad_apply" && tool != "cad_restore" && tool != "cad_import" && tool != "cad_query" && tool != "cad_export" && tool != "cad_drawing" && tool != "cad_bom" && tool != "cad_preview" && tool != "cad_view")
    throw Error("invalid_argument", "This tool cannot be submitted as a geometry job");
  auto input = arguments.at("arguments"); if (!input.is_object()) throw Error("invalid_argument", "Job arguments must be an object");
  if (input.contains("request_id") && input.at("request_id") != id)
    throw Error("invalid_argument", "Nested request_id must match the job request_id");
  if (tool == "cad_create" || tool == "cad_apply" || tool == "cad_restore" || tool == "cad_import") input["request_id"] = id;
  const auto fingerprint = request_fingerprint(tool, input);
  const auto limits = budget(arguments.value("budget", Json::object()));
  bool retry = false;
  if (fs::exists(path / "state.json")) {
    auto state = read_result(path / "state.json");
    if (state.at("fingerprint") != fingerprint || state.at("budget") != limits)
      throw Error("request_conflict", "request_id already belongs to a different job request");
    recover_job(workspace, path, state);
    if (state.at("state") != "interrupted") return public_job(state);
    retry = true;
  }
  int active = 0;
  for (const auto& entry : fs::directory_iterator(root)) {
    if (entry.is_symlink()) throw Error("storage_error", "Managed job directories cannot be symlinks");
    if (!entry.is_directory() || !fs::exists(entry.path() / "state.json")) continue;
    auto state = read_result(entry.path() / "state.json"); recover_job(workspace, entry.path(), state);
    if (!terminal(state)) ++active;
  }
  if (active >= max_active_jobs) throw Error("queue_full", "At most eight geometry jobs may be queued or running");
  directory(path);
  Json state = {{"job_id", id}, {"request_id", id}, {"tool", tool}, {"arguments", input}, {"fingerprint", fingerprint},
    {"budget", limits}, {"state", "queued"}, {"progress", 0.0}, {"submitted_at_unix_ms", now_ms()},
    {"deadline_unix_ms", now_ms() + limits.at("timeout_ms").get<int>()}, {"retried", retry}};
  save_job(path, state);
  try {
    auto child = launch({"--internal-job-worker", workspace, id}, false, 0);
#ifndef _WIN32
    background_children.push_back(child.pid);
#endif
  } catch (const Error& e) { state["state"] = "interrupted"; state["error"] = e.json(); save_job(path, state); throw; }
  return public_job(state);
}

int job_worker_main(const fs::path& workspace, const std::string& job_id) {
  const auto root = workspace / "jobs"; identifier(job_id); const auto path = root / job_id;
  try {
    WorkspaceLock owner(path);
    auto state = read_result(path / "state.json");
    if (terminal(state)) return 0;
    cancellation_file = path / "cancel"; current_budget = budget(state.at("budget"));
    job_deadline = state.at("deadline_unix_ms"); request_context = {{"request_id",job_id}, {"fingerprint",state.at("fingerprint")}};
    try {
      if (const auto result = committed_result(workspace, state)) {
        state["result"] = *result; state["state"] = "succeeded"; state["progress"] = 1.0;
      } else {
        check_job_cancelled();
        {
          const auto admission = wait_lock(root);
          state["state"] = "running"; state["progress"] = 0.1; save_job(path, state);
        }
        Service service(workspace);
        state["result"] = service.call(state.at("tool"), state.at("arguments"));
        state["state"] = "succeeded"; state["progress"] = 1.0;
      }
    } catch (const Error& e) {
      // Publication may have succeeded before an OS durability/reporting failure.
      if (const auto result = committed_result(workspace, state)) {
        state["result"] = *result; state["state"] = "succeeded"; state["progress"] = 1.0;
      } else { state["state"] = e.code == "job_cancelled" ? "cancelled" : "failed"; state["error"] = e.json(); }
    } catch (const std::exception& e) { state["state"] = "failed"; state["error"] = Error("internal_error", e.what()).json(); }
    // A brief contender may hold admission while reading; never lose a completed
    // result merely because polling overlaps this atomic state write.
    for (int attempts = 0;; ++attempts) {
      try { WorkspaceLock admission(root); save_job(path, state); break; }
      catch (const Error& e) {
        if (e.code != "workspace_busy" || attempts >= 500) throw;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }
    return state.at("state") == "succeeded" ? 0 : 1;
  } catch (const std::exception&) { return 1; }
}
}
