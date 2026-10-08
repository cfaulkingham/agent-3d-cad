#include "agentcad/process.hpp"
#include "agentcad/hash.hpp"
#include <array>
#include <chrono>
#include <fstream>
#include <optional>
#include <thread>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif
extern char** environ;
#endif

namespace agentcad {
namespace {
constexpr std::size_t disk_limit=128*1024*1024,log_limit=2*1024*1024;
void storage_bounds(const fs::path& root) {
  std::uintmax_t bytes=0,logs=0;std::size_t files=0;
  for(const auto& item:fs::recursive_directory_iterator(root)) {
    if(++files>4096||item.is_symlink())throw Error("limit_exceeded","Slicer staging exceeds entry bounds or contains a symlink");
    if(item.is_regular_file()) {
      const auto size=item.file_size();bytes+=size;
      const auto name=path_to_utf8(item.path().filename());
      if(name.ends_with(".log"))logs+=size;
      if(bytes>disk_limit||logs>log_limit)throw Error("limit_exceeded","Slicer exceeded 128 MiB staging or 2 MiB aggregate log budget");
    }else if(!item.is_directory())throw Error("limit_exceeded","Slicer staging contains a special file");
  }
}
#ifdef _WIN32
std::wstring quoted(const std::wstring& value) {
  std::wstring result=L"\"";std::size_t slashes=0;
  for(const auto c:value) {
    if(c==L'\\'){++slashes;continue;}
    result.append(c==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;result+=c;
  }
  result.append(slashes*2,L'\\');return result+L"\"";
}
struct NativeChild {
  HANDLE process=nullptr;
  ~NativeChild(){if(process){if(WaitForSingleObject(process,0)==WAIT_TIMEOUT)TerminateProcess(process,1);WaitForSingleObject(process,INFINITE);CloseHandle(process);}}
  bool done(int& status){if(WaitForSingleObject(process,0)==WAIT_TIMEOUT)return false;DWORD result=1;if(!GetExitCodeProcess(process,&result))throw Error("slicer_failed","Cannot read slicer exit status");status=static_cast<int>(result);return true;}
};
void start(NativeChild& child,const fs::path& exe,const std::vector<std::string>& args,const fs::path& cwd,const fs::path& out,const fs::path& err) {
  SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
  HANDLE handles[3]={CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr),
    CreateFileW(out.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr),
    CreateFileW(err.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)};
  auto close=[&](){for(const auto handle:handles)if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);};
  for(const auto handle:handles)if(handle==INVALID_HANDLE_VALUE){close();throw Error("slicer_failed","Cannot prepare slicer standard streams");}
  SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<unsigned char> memory(bytes);
  auto attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(memory.data());
  if(!InitializeProcThreadAttributeList(attributes,1,0,&bytes)){close();throw Error("slicer_failed","Cannot prepare slicer inheritance");}
  if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr)){DeleteProcThreadAttributeList(attributes);close();throw Error("slicer_failed","Cannot restrict slicer inheritance");}
  STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput=handles[0];startup.StartupInfo.hStdOutput=handles[1];startup.StartupInfo.hStdError=handles[2];startup.lpAttributeList=attributes;
  auto command=quoted(exe.wstring());for(const auto& arg:args)command+=L" "+quoted(path_from_utf8(arg).wstring());
  PROCESS_INFORMATION created{};
  const auto ok=CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,cwd.c_str(),&startup.StartupInfo,&created);
  const auto error=GetLastError();DeleteProcThreadAttributeList(attributes);close();
  if(!ok)throw Error("slicer_failed","Cannot launch slicer",{{"os_error",error}});
  child.process=created.hProcess;CloseHandle(created.hThread);
}
std::uint64_t group_memory() {
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
  if(!QueryInformationJobObject(nullptr,JobObjectExtendedLimitInformation,&info,sizeof(info),nullptr))throw Error("worker_failed","Cannot inspect native process job memory");
  return info.PeakJobMemoryUsed;
}
#else
struct NativeChild {
  pid_t pid=-1;bool collected=false;
  ~NativeChild(){if(pid>0&&!collected){::kill(pid,SIGKILL);while(::waitpid(pid,nullptr,0)<0&&errno==EINTR){}}}
  bool done(int& status){const auto n=::waitpid(pid,&status,WNOHANG);if(n<0&&errno==EINTR)return false;if(n<0)throw Error("slicer_failed","Cannot wait for slicer");if(n!=pid)return false;collected=true;status=WIFEXITED(status)?WEXITSTATUS(status):256+WTERMSIG(status);return true;}
};
void start(NativeChild& child,const fs::path& exe,const std::vector<std::string>& args,const fs::path& cwd,const fs::path& out,const fs::path& err) {
  std::vector<std::string> words{exe.string()};words.insert(words.end(),args.begin(),args.end());std::vector<char*> argv;
  for(auto& word:words)argv.push_back(word.data());argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
  int error=posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
  if(!error)error=posix_spawn_file_actions_addopen(&actions,STDOUT_FILENO,out.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
  if(!error)error=posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,err.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
#if defined(__APPLE__) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
  if(!error) {
    if(__builtin_available(macOS 26.0,*))error=posix_spawn_file_actions_addchdir(&actions,cwd.c_str());
    else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      error=posix_spawn_file_actions_addchdir_np(&actions,cwd.c_str());
#pragma clang diagnostic pop
    }
  }
#else
  if(!error)error=posix_spawn_file_actions_addchdir_np(&actions,cwd.c_str());
#endif
  // Inherit the supervisor's process group. The coordinator retains its leader
  // until it has terminated all descendants, including after abnormal exit.
  if(!error)error=posix_spawn(&child.pid,exe.c_str(),&actions,nullptr,argv.data(),environ);
  posix_spawn_file_actions_destroy(&actions);
  if(error)throw Error("slicer_failed","Cannot launch slicer: "+std::string(std::strerror(error)));
}
std::uint64_t group_memory() {
  std::uint64_t total=0;
#ifdef __APPLE__
  std::array<pid_t,1024> pids{};
  const auto bytes=proc_listpids(PROC_PGRP_ONLY,static_cast<std::uint32_t>(getpgrp()),pids.data(),sizeof(pids));
  if(bytes<0||bytes>=static_cast<int>(sizeof(pids)))throw Error("limit_exceeded","Cannot bound native process group");
  for(int i=0;i<bytes/static_cast<int>(sizeof(pid_t));++i)if(pids[i]>0) {
    rusage_info_v2 info{};
    if(proc_pid_rusage(pids[i],RUSAGE_INFO_V2,reinterpret_cast<rusage_info_t*>(&info))==0)total+=info.ri_phys_footprint;
    else if(::kill(pids[i],0)==0)throw Error("worker_failed","Cannot inspect process-group memory");
  }
#else
  std::size_t entries=0;
  for(const auto& item:fs::directory_iterator("/proc")) {
    if(++entries>65536)throw Error("limit_exceeded","Cannot bound process inventory");
    const auto name=item.path().filename().string();if(name.empty()||name.find_first_not_of("0123456789")!=std::string::npos)continue;
    const auto pid=static_cast<pid_t>(std::stol(name));if(::getpgid(pid)!=::getpgrp())continue;
    std::ifstream status(item.path()/"statm");std::uint64_t virtual_pages=0,resident=0;
    if(status>>virtual_pages>>resident)total+=resident*static_cast<std::uint64_t>(::sysconf(_SC_PAGESIZE));
    else if(::kill(pid,0)==0)throw Error("worker_failed","Cannot inspect process-group memory");
  }
#endif
  return total;
}
#endif
}
Json supervise_process(const Json& request,const fs::path& control,const std::function<void()>& checkpoint) {
  fields(request,{"executable","expected_sha256","arguments","cwd","log_prefix","timeout_ms","memory_mb","parent_pid"});
  const auto exe=path_from_utf8(text_field(request,"executable")),cwd=path_from_utf8(text_field(request,"cwd"));
  const auto prefix=text_field(request,"log_prefix");if(prefix!="probe"&&prefix!="slice")throw Error("invalid_argument","Invalid internal log prefix");
  if(!exe.is_absolute()||!cwd.is_absolute()||!request.at("arguments").is_array()||request.at("arguments").size()>64)throw Error("invalid_argument","Malformed internal process paths/arguments");
  std::vector<std::string> args;for(const auto& argument:request.at("arguments")) {
    if(!argument.is_string())throw Error("invalid_argument","Native argv must be strings");
    auto value=argument.get<std::string>();if(value.size()>4096||value.find('\0')!=std::string::npos)throw Error("invalid_argument","Native argv exceeds text bounds");args.push_back(std::move(value));
  }
  const auto started=std::chrono::steady_clock::now();
  auto check=[&](){checkpoint();if(std::chrono::steady_clock::now()-started>std::chrono::milliseconds(request.at("timeout_ms").get<int>()))throw Error("job_timeout","Slicer exceeded its wall budget");};
  const auto hash=sha256_file(exe,512*1024*1024,check);
  if(hash!=text_field(request,"expected_sha256"))throw Error("artifact_mismatch","Installed slicer executable changed");
  NativeChild child;start(child,exe,args,cwd,cwd/(prefix+".stdout.log"),cwd/(prefix+".stderr.log"));
#ifdef _WIN32
  const Json process={{"pid",GetProcessId(child.process)}};
#else
  const Json process={{"pid",child.pid},{"process_group",getpgrp()}};
#endif
  atomic_text(control/"external-process.json",process.dump());
  int status=0;std::uint64_t peak=0;
  while(true) {
    check();storage_bounds(cwd);peak=std::max(peak,group_memory());
    if(peak>request.at("memory_mb").get<std::uint64_t>()*1024*1024)throw Error("memory_limit","Slicer process group exceeded its memory budget");
    if(child.done(status))break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  check();storage_bounds(cwd);
  if(sha256_file(exe,512*1024*1024,check)!=hash)throw Error("artifact_mismatch","Slicer executable changed during execution");
  return {{"exit_status",status},{"elapsed_ms",std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()},
    {"peak_group_memory_bytes",peak},{"executable_sha256",hash},{"argv",args}};
}
}
