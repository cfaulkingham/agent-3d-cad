#include "agentcad/download.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/mcp.hpp"
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>

using namespace agentcad;
namespace {
int checks=0;
std::string stage;
void require(bool yes,const std::string& message){++checks;if(!yes)throw std::runtime_error(message);}
void fails(const std::string& code,const std::function<void()>& action){try{action();}catch(const Error& error){require(error.code==code,"Expected "+code+", got "+error.code+": "+error.what());return;}throw std::runtime_error("Expected "+code);}
struct Temporary {fs::path root=temporary_directory(fs::temp_directory_path());~Temporary(){std::error_code ignored;fs::remove_all(root,ignored);}};
Json model(){return {{"schema_version",1},{"units","mm"},{"parameters",{{"width",60}}},{"features",Json::array({
  {{"id","block"},{"type","box"},{"size",Json::array({Json{{"parameter","width"}},40,8})}},
  {{"id","copies"},{"type","pattern"},{"input","block"},{"count",3},{"step",{100,0,0}}}
})},{"output","copies"}};}
std::string decode(const std::string& encoded){
  const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string result;unsigned bits=0;int count=0;
  for(const auto ch:encoded){if(ch=='=')break;const auto value=alphabet.find(ch);if(value==std::string::npos)throw std::runtime_error("Invalid base64 alphabet");bits=(bits<<6)|static_cast<unsigned>(value);count+=6;if(count>=8){count-=8;result+=static_cast<char>((bits>>count)&255);}}
  return result;
}
Json rpc(const std::string& method,Json params=Json::object()){return {{"jsonrpc","2.0"},{"id",1},{"method",method},{"params",params}};}
Json links_and_bytes(Service& service,const Json& result,const std::vector<fs::path>& paths){
  require(result.at("download_errors").empty(),"Native exports have no download errors");require(result.at("downloads").size()==paths.size(),"Every requested artifact has a resource link");
  for(std::size_t i=0;i<paths.size();++i){const auto& link=result.at("downloads")[i];const auto read=service.read_resource(link.at("uri"));
    require(link.at("type")=="resource_link"&&link.at("name")==path_to_utf8(paths[i].filename()),"Resource descriptor names native artifact");
    require(read.at("contents").size()==1&&read.at("contents")[0].at("uri")==link.at("uri")&&read.at("contents")[0].at("mimeType")==link.at("mimeType"),"Resource read preserves URI and MIME");
    const auto bytes=decode(read.at("contents")[0].at("blob"));require(bytes==read_text(paths[i],download_file_limit)&&bytes.size()==link.at("size"),"Independent base64 decoding recovers exact file bytes");
  }return result.at("downloads");
}
void exports(){
  Temporary temp;Service service(temp.root);service.call("cad_create",{{"document_id","part"},{"model",model()}});
  std::string old_uri,old_blob;fs::path original;
  for(const auto* format:{"step","stl","3mf"}){
    stage=std::string("export ")+format;
    const auto result=service.call("cad_export",{{"document_id","part"},{"revision",1},{"format",format}});
    std::vector<fs::path> exported_paths={path_from_utf8(result.at("path"))};if(result.contains("layout_path"))exported_paths.push_back(path_from_utf8(result.at("layout_path")));
    auto links=links_and_bytes(service,result,exported_paths);
    if(std::string(format)=="step"){old_uri=links[0].at("uri");old_blob=service.read_resource(old_uri).dump();original=path_from_utf8(result.at("path"));}
  }
  stage="packed plates";const auto plates=service.call("cad_export",{{"document_id","part"},{"revision",1},{"format","3mf"},{"layout",{{"bed_mm",{75,55}},{"margin_mm",5},{"spacing_mm",3}}}});
  std::vector<fs::path> paths;for(const auto& plate:plates.at("plates"))paths.push_back(path_from_utf8(plate.at("path")));paths.push_back(path_from_utf8(plates.at("layout_path")));
  require(paths.size()==4,"Three physical plates plus layout report");links_and_bytes(service,plates,paths);
  stage="drawing";const auto drawing=service.call("cad_drawing",{{"document_id","part"},{"revision",1},{"drawing",{{"formats",{"pdf","svg","dxf"}},{"views",Json::array({{{"id","top"},{"orientation","top"}}})}}}});
  paths.clear();for(const auto& item:drawing.at("artifacts"))paths.push_back(path_from_utf8(item.at("path")));links_and_bytes(service,drawing,paths);
  stage="edit and reopen";service.call("cad_apply",{{"document_id","part"},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","width"},{"value",50}}})}});
  atomic_text(original,"Modified original export");Service reopened(temp.root);require(reopened.read_resource(old_uri).dump()==old_blob,"Captured old revision survives restart, edits and changes to original export");
  stage="MCP";McpSession session(reopened);require(session.handle(rpc("initialize",{{"protocolVersion","2025-11-25"},{"capabilities",Json::object()},{"clientInfo",{{"name","test"},{"version","1"}}}}))->contains("result"),"MCP initializes");
  session.handle({{"jsonrpc","2.0"},{"method","notifications/initialized"}});
  require(session.handle(rpc("resources/read",{{"uri",old_uri}}))->at("result").dump()==old_blob,"MCP serves source-qualified bytes");
  const auto exported=session.handle(rpc("tools/call",{{"name","cad_export"},{"arguments",{{"document_id","part"},{"revision",2},{"format","stl"}}}}))->at("result");
  require(exported.at("content").size()==2&&exported.at("content")[1].at("type")=="resource_link","MCP publishes native resource links beside structured output");
  for(const auto& uri:{std::string("file:///etc/passwd"),old_uri+"/extra",old_uri.substr(0,old_uri.rfind('/')+1)+"../HEAD.json"})require(session.handle(rpc("resources/read",{{"uri",uri}}))->at("error").at("code")==-32002,"Arbitrary or malformed URI rejected");
  stage="job";const auto job=service.call("cad_job",{{"action","submit"},{"request_id","download_job"},{"tool","cad_export"},{"arguments",{{"document_id","part"},{"revision",2},{"format","step"}}}});
  Json done;for(int i=0;i<1000;++i){try{done=service.call("cad_job",{{"action","get"},{"job_id",job.at("job_id")}});if(done.at("state")=="succeeded"||done.at("state")=="failed")break;}catch(const Error& error){if(error.code!="workspace_busy")throw;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
  require(done.at("state")=="succeeded","Isolated export job succeeds");links_and_bytes(reopened,done.at("result"),{path_from_utf8(done.at("result").at("path"))});
  stage="corruption";const auto key=old_uri.substr(std::string("cad-export://").size(),64);const auto data=temp.root/"downloads"/(key+".bin"),metadata=temp.root/"downloads"/(key+".json");
  const auto bytes=read_text(data,download_file_limit),manifest=read_text(metadata);
  atomic_text(data,"altered");fails("artifact_mismatch",[&]{reopened.read_resource(old_uri);});atomic_text(data,bytes,download_file_limit);
  auto bad=parse_json(manifest);bad["size"]=1;atomic_text(metadata,bad.dump());fails("artifact_mismatch",[&]{reopened.read_resource(old_uri);});atomic_text(metadata,manifest);
  const auto revision=temp.root/"documents"/"part"/"revisions"/"1.json";const auto record=read_text(revision);bad=parse_json(record);bad["model"]["parameters"]["width"]=55;atomic_text(revision,bad.dump());
  fails("artifact_mismatch",[&]{reopened.read_resource(old_uri);});atomic_text(revision,record);
#ifndef _WIN32
  fs::remove(data);fs::create_symlink(original,data);fails("storage_error",[&]{reopened.read_resource(old_uri);});fs::remove(data);atomic_text(data,bytes,download_file_limit);
  const auto folder=temp.root/"downloads";fs::rename(folder,temp.root/"saved-downloads");fs::create_directory_symlink(temp.root/"saved-downloads",folder);fails("storage_error",[&]{reopened.read_resource(old_uri);});fs::remove(folder);fs::rename(temp.root/"saved-downloads",folder);
#endif
  require(reopened.read_resource(old_uri).dump()==old_blob,"Restored captured resource remains independently verifiable");
}
void limits(){
  stage="limits";
  Temporary temp;Store store(temp.root);Service service(temp.root);service.call("cad_create",{{"document_id","part"},{"model",model()}});
  const auto outside=temp.root/"outside.step",empty=temp.root/"exports"/"empty.step",large=temp.root/"exports"/"large.step";
  atomic_text(outside,"outside");atomic_text(empty,"");atomic_text(large,"x");fs::resize_file(large,download_file_limit+1);
  for(const auto& path:{outside,empty,large}){Json result={{"document_id","part"},{"revision",1},{"path",path_to_utf8(path)}};attach_export_downloads(store,result);require(result.at("downloads").empty()&&result.at("download_errors").size()==1,"Invalid/oversized capture retains path and reports explicit delivery error");}
  require(service.call("cad_read",{{"document_id","part"}}).at("revision")==1,"Delivery failures do not change the model");
}
}
int main(){try{configure_kernel_logging();set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));exports();limits();std::cout<<checks<<" download checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<stage<<": "<<error.what()<<'\n';return 1;}}
