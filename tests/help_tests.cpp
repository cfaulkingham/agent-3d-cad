#include "agentcad/help.hpp"
#include "agentcad/mcp.hpp"
#include "agentcad/model.hpp"
#include <iostream>
#include <set>
using namespace agentcad;
namespace {
int checks=0;
void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
template<class F> void fails(const std::string& code,F fn){try{fn();}catch(const Error& e){require(e.code==code,"Incorrect help error");return;}throw std::runtime_error("Expected help error");}
}
int main(){const auto root=temporary_directory(fs::temp_directory_path());try{
  Service service(root);
  const auto before=service.call("cad_list",Json::object());
  const auto search=service.call("cad_help",{{"query","round the edges"},{"limit",5}});
  bool fillet=false;
  for(const auto& hit:search.at("matches"))fillet|=hit.at("topic")=="feature/fillet";
  require(fillet,"Natural-language round search finds the fillet feature among five results");
  require(search.at("matches").size()==5&&search.at("truncated")==true,"Search is bounded and reports omissions");
  require(service.call("cad_help",{{"query","round the edges"},{"limit",5}})==search,"Search is deterministic");
  const auto sheet_search=service.call("cad_help",{{"query","four view sheet"},{"limit",5}});
  require(sheet_search.at("matches")[0].at("topic")=="guide/rendering/shared-scale-verification-sheet","Render-sheet guidance outranks incidental sheet-metal category words");
  const auto hollow=service.call("cad_help",{{"query","hollow the part"},{"limit",5}});
  bool shell=false;for(const auto& hit:hollow.at("matches"))shell|=hit.at("topic")=="feature/shell";
  require(shell,"Hollow search finds the shell feature");
  require(service.call("cad_help",{{"query","XYZZYnonexistent"}}).at("matches").empty(),"No invented matches for unknown query");
  const auto detail=service.call("cad_help",{{"topic","feature/fillet"}}).at("entry");
  require(detail.at("schemas").at("input").at("properties").at("type").at("const")=="fillet","Exact feature schema discriminator");
  require(detail.at("schemas").at("input").at("$defs").contains("selector"),"Feature schema includes its referenced selector");
  require(!detail.at("related").empty(),"Feature points to bundled guidance");
  const auto tools=tool_definitions();
  for(const auto& tool:tools){
    const auto entry=service.call("cad_help",{{"topic","tool/"+text_field(tool,"name")}}).at("entry");
    require(entry.at("schemas").at("input")==tool.at("inputSchema")&&entry.at("schemas").at("output")==tool.at("outputSchema"),"Help returns the actual published tool schemas");
  }
  const auto definitions=model_definitions();
  for(const auto* kind:{"feature","operation"})for(const auto& variant:definitions.at(kind).at("oneOf")){
    const auto& schema=variant.contains("$ref")?definitions.at(text_field(variant,"$ref").substr(8)):variant;
    const auto field=std::string(kind)=="feature"?"type":"op";const auto& choice=schema.at("properties").at(field);
    const auto names=choice.contains("const")?Json::array({choice.at("const")}):choice.at("enum");
    for(const auto& name:names)require(service.call("cad_help",{{"topic",std::string(kind)+"/"+name.get<std::string>()}}).contains("entry"),"Every native feature and edit operation is discoverable");
  }
  const auto guide=service.call("cad_help",{{"topic","guide/native-help/rounding-edges-with-fillet"}}).at("entry");
  require(text_field(guide,"content").find("expected_count")!=std::string::npos,"Bundled fillet recipe includes selection cardinality");
  require(guide.at("source")=="docs/NATIVE_HELP.md","Guidance identifies its source");
  std::string content;std::size_t offset=0;bool paged=false;
  do{
    const auto page=service.call("cad_help",{{"topic","guide/protocol/features"},{"offset",offset}}).at("entry");
    content+=text_field(page,"content");require(!invalid_utf8_offset(text_field(page,"content")),"Help pages preserve UTF-8");
    if(page.at("next_offset").is_null())break;
    offset=page.at("next_offset");paged=true;
  }while(true);
  require(paged&&content.size()>12000,"Long guide paginates without discarding its remainder");
  for(const auto& g:help_guides())if(g.path=="docs/PROTOCOL.md"){
    const auto start=g.text.find("## Features\n"),end=g.text.find("\n### ",start);
    require(content==g.text.substr(start,end-start+1),"Paginated help exactly reproduces the compiled source section");
  }
  for(const auto& bad:Json::array({Json{{"query","a"},{"topic","b"}},Json{{"topic","a"},{"limit",2}},Json{{"offset",0}},Json{{"limit",0}},Json{{"query",std::string(257,'a')}},Json{{"query",""}},Json{{"path","/tmp/help"}}}))
    fails("invalid_argument",[&]{service.call("cad_help",bad);});
  fails("not_found",[&]{service.call("cad_help",{{"topic","../../secret"}});});
  fails("invalid_argument",[&]{service.call("cad_help",{{"topic","feature/fillet"},{"offset",999999}});});
  require(service.call("cad_list",Json::object())==before,"Help never creates or mutates documents");
  McpSession session(service);
  session.handle({{"jsonrpc","2.0"},{"id",1},{"method","initialize"},{"params",{{"protocolVersion","2025-11-25"},{"capabilities",Json::object()},{"clientInfo",{{"name","help-test"},{"version","1"}}}}}});
  session.handle({{"jsonrpc","2.0"},{"method","notifications/initialized"}});
  const auto response=session.handle({{"jsonrpc","2.0"},{"id",2},{"method","tools/call"},{"params",{{"name","cad_help"},{"arguments",{{"query","round the edges"},{"limit",5}}}}}});
  require(response->at("result").at("structuredContent")==search&&!response->at("result").at("isError").get<bool>(),"MCP and Service return the same searchable help");
  fs::remove_all(root);std::cout<<"help: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){fs::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}}
