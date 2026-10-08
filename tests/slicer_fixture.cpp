// Native process-control fixture only. This does not simulate slicing accuracy.
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif
namespace fs=std::filesystem;
using Json=nlohmann::json;
int fixture(const std::vector<std::string>& args) {
  if(args.size()==2&&args[1]=="--help") {
    std::cout<<(args[0].find("wrong-version")!=std::string::npos?"OrcaSlicer-2.4.1:\n":"OrcaSlicer-2.4.2:\n");return 0;
  }
  if(args.size()==2&&args[1]=="--fixture-descendant") {std::this_thread::sleep_for(std::chrono::seconds(60));return 0;}
  const auto value=[&](const char* key){for(std::size_t i=1;i+1<args.size();++i)if(args[i]==key)return args[i+1];throw std::runtime_error("Missing fixture argument");};
  try {
    const auto settings=value("--load-settings");Json machine;std::ifstream(fs::u8path(settings.substr(0,settings.find(';'))))>>machine;
    const auto name=machine.at("name").get<std::string>();
    if(name=="Slow"||name=="Lingering"||name=="Crash") {
#ifdef _WIN32
      const auto exe=fs::absolute(fs::u8path(args[0]));auto command=L"\""+exe.wstring()+L"\" --fixture-descendant";STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
      if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&child))return 9;
      std::ofstream("descendant.pid")<<child.dwProcessId;CloseHandle(child.hProcess);CloseHandle(child.hThread);
#else
      const auto child=fork();if(child<0)return 9;
      if(!child){execl(args[0].c_str(),args[0].c_str(),"--fixture-descendant",static_cast<char*>(nullptr));_exit(9);}
      std::ofstream("descendant.pid")<<child;
#endif
      if(name=="Slow")std::this_thread::sleep_for(std::chrono::seconds(60));
      if(name=="Crash")return 11;
    }
    if(name=="Memory") {std::vector<std::vector<char>> memory;for(int i=0;i<128;++i){memory.emplace_back(16*1024*1024,'x');std::this_thread::sleep_for(std::chrono::milliseconds(5));}}
    if(name=="Logs") {for(int i=0;i<10000;++i)std::cout<<std::string(1000,'x')<<std::endl;return 0;}
    if(name=="Failure")return 7;
    if(value("--arrange")!="1"||value("--orient")!="0"||value("--slice")!="0")return 8;
    Json filament;std::ifstream(fs::u8path(value("--load-filaments")))>>filament;
    Json process;std::ifstream(fs::u8path(settings.substr(settings.find(';')+1)))>>process;
    Json effective=machine;effective.update(filament);
    effective["printer_settings_id"]=machine.at("name");effective["print_settings_id"]=process.at("name");effective["filament_settings_id"]=Json::array({filament.at("name")});effective["curr_bed_type"]="High Temp Plate";
    if(name=="WrongSettings")effective["curr_bed_type"]="Cool Plate";
    std::ofstream(fs::u8path(value("--export-settings")))<<effective.dump();
    const auto output=fs::u8path(value("--outputdir"));fs::create_directories(output);std::ofstream out(output/"plate_1.gcode");
    if(name=="NoOutput"){out.close();fs::remove(output/"plate_1.gcode");return 0;}
    out<<"; NATIVE TEST FIXTURE ONLY\nG21\nG90\nM83\nM104S210\nM140S60\nG1X1Y1Z.2E1\n";
    if(name=="BadBounds")out<<"G91\nG1X200E1\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 3;}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
  std::vector<std::string> args;
  for(int i=0;i<argc;++i){const auto utf8=fs::path(argv[i]).u8string();args.emplace_back(reinterpret_cast<const char*>(utf8.data()),utf8.size());}
  return fixture(args);
}
#else
int main(int argc,char** argv){return fixture({argv,argv+argc});}
#endif
