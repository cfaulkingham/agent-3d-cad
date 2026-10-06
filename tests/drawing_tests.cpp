#include "agentcad/jobs.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/service.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <thread>

using namespace agentcad;
namespace {
int checks=0;
void require(bool condition,const std::string& message) { ++checks;if(!condition)throw std::runtime_error(message); }
void near(double actual,double expected,double tolerance=1e-6) {
  require(std::abs(actual-expected)<tolerance,"Expected "+std::to_string(expected)+", got "+std::to_string(actual));
}
void fails(const std::string& code,const std::function<void()>& operation) {
  try {operation();}catch(const Error& error){require(error.code==code,"Expected "+code+", got "+error.code+": "+error.what());return;}
  throw std::runtime_error("Expected "+code);
}
struct Temporary {
  fs::path path;
  Temporary():path(fs::temp_directory_path()/path_from_utf8("agentcad-drawing-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-零")){directory(path);}
  ~Temporary(){std::error_code ignored;fs::remove_all(path,ignored);}
};
Json box() {return {{"schema_version",1},{"units","mm"},{"parameters",{{"height",6},{"bore_r",2},{"cx",10},{"cy",5}}},
  {"features",Json::array({{{"id","base"},{"type","box"},{"size",Json::array({20,10,Json{{"parameter","height"}}})}}})},{"output","base"}};}
Json bore() {
  auto model=box();model["features"].push_back({{"id","bored"},{"type","hole"},{"input","base"},
    {"origin",Json::array({Json{{"parameter","cx"}},Json{{"parameter","cy"}},-1})},{"axis",{0,0,1}},
    {"radius",{{"parameter","bore_r"}}},{"depth",20}});model["output"]="bored";return model;
}
const Json& view(const Json& projection,const std::string& id) {
  for(const auto& item:projection.at("views"))if(item.at("id")==id)return item;
  throw std::runtime_error("Missing projected view "+id);
}
void extent(const Json& item,double width,double height) {
  const auto& b=item.at("bounds_mm");near(b[2].get<double>()-b[0].get<double>(),width);near(b[3].get<double>()-b[1].get<double>(),height);
}
std::size_t circles(const Json& item,double x,double y,double radius) {
  return std::count_if(item.at("entities").begin(),item.at("entities").end(),[&](const Json& e){
    return e.at("kind")=="circle"&&std::abs(e.at("radius").get<double>()-radius)<1e-6&&
      std::abs(e.at("center")[0].get<double>()-x)<1e-6&&std::abs(e.at("center")[1].get<double>()-y)<1e-6;});
}
Json call(Service& service,const std::string& tool,const Json& arguments) {
  for(int attempt=0;;++attempt){try{return service.call(tool,arguments);}catch(const Error& e){if(e.code!="workspace_busy"||attempt==1000)throw;}
    std::this_thread::sleep_for(std::chrono::milliseconds(5));}
}
std::set<fs::path> exported_files(const fs::path& root) {
  std::set<fs::path> result;for(const auto& entry:fs::recursive_directory_iterator(root/"exports"))if(entry.is_regular_file())result.insert(entry.path());return result;
}
void pdf_check(const std::string& content) {
  require(content.starts_with("%PDF-"),"native PDF header");
  const auto start=content.rfind("startxref");require(start!=std::string::npos,"PDF has cross-reference trailer");
  std::istringstream tail(content.substr(start+9));std::size_t offset=0;tail>>offset;
  require(offset<content.size()&&content.compare(offset,4,"xref")==0,"PDF startxref points to actual cross-reference table");
  std::istringstream table(content.substr(offset+4));std::size_t first=0,count=0;table>>first>>count;
  require(first==0&&count>=5,"PDF xref covers page, font and drawing content");
  for(std::size_t i=0;i<count;++i){std::size_t position;int generation;char state;table>>position>>generation>>state;
    require(bool(table),"PDF xref entry parses");
    if(i)require(state=='n'&&position<content.size()&&content.compare(position,(std::to_string(i)+" 0 obj").size(),std::to_string(i)+" 0 obj")==0,"PDF xref points to correct indirect object");}
  require(content.rfind("%%EOF")!=std::string::npos,"PDF EOF marker present");
}
using Dxf=std::vector<std::pair<int,std::string>>;
Dxf dxf_pairs(const std::string& content) {
  std::istringstream input(content);Dxf result;std::string code,value;
  while(std::getline(input,code)){if(!std::getline(input,value))throw std::runtime_error("DXF has incomplete code/value pair");
    if(!value.empty()&&value.back()=='\r')value.pop_back();result.emplace_back(std::stoi(code),value);}
  return result;
}
void dxf_check(const std::string& content,bool* exact_circle) {
  const auto pairs=dxf_pairs(content);bool units=false,width=false;std::size_t lines=0;
  std::set<std::string> handles,tables;std::vector<std::string> owners;
  for(std::size_t i=0;i<pairs.size();++i)if(pairs[i].first==0) {
    const auto& type=pairs[i].second;
    if(type=="SECTION"||type=="ENDSEC"||type=="ENDTAB"||type=="EOF")continue;
    std::string handle,owner,name;bool symbol_table=false;
    for(std::size_t j=i+1;j<pairs.size()&&pairs[j].first!=0;++j) {
      const auto& [code,value]=pairs[j];
      if(code==5){require(handle.empty(),"DXF record has exactly one handle");handle=value;}
      if(code==330)owner=value;
      if(code==2)name=value;
      if(code==100&&value=="AcDbSymbolTable")symbol_table=true;
    }
    require(!handle.empty()&&handle!="0"&&handles.insert(handle).second,"DXF records and entities have unique nonzero handles");
    require(!owner.empty(),"DXF record declares an owner");owners.push_back(owner);
    if(type=="TABLE"){require(symbol_table,"DXF table header declares AcDbSymbolTable subclass");tables.insert(name);}
  }
  require(tables.contains("LTYPE")&&tables.contains("LAYER")&&tables.contains("STYLE"),"DXF contains named line, layer and text-style tables");
  for(const auto& owner:owners)require(owner=="0"||handles.contains(owner),"DXF owner handle resolves to an emitted record");
  for(std::size_t i=0;i<pairs.size();++i){const auto& [code,value]=pairs[i];
    if(code==9&&value=="$INSUNITS"&&i+1<pairs.size())units=pairs[i+1].first==70&&std::stoi(pairs[i+1].second)==4;
    if(code==0&&value=="CIRCLE")for(std::size_t j=i+1;j<pairs.size()&&pairs[j].first!=0;++j)if(pairs[j].first==40&&std::abs(std::stod(pairs[j].second)-2)<1e-6)*exact_circle=true;
    if(code==0&&value=="LINE") {
      ++lines;double start=0,end=0;bool has_start=false,has_end=false;
      for(std::size_t j=i+1;j<pairs.size()&&pairs[j].first!=0;++j){if(pairs[j].first==10){start=std::stod(pairs[j].second);has_start=true;}if(pairs[j].first==11){end=std::stod(pairs[j].second);has_end=true;}}
      if(has_start&&has_end&&std::abs(std::abs(end-start)-20)<1e-6)width=true;
    }
  }
  require(units,"DXF declares model units as millimeters");require(lines>=4,"DXF preserves vector outline line entities");
  require(width,"DXF uses true 20 mm geometry even when sheet scale is two");
}
void geometry_tests() {
  const Json views=Json::array({{{"id","front"},{"orientation","front"}},{{"id","top"},{"orientation","top"}},{{"id","right"},{"orientation","right"}}});
  BuiltModel solid(box());const auto projected=solid.drawing({{"views",views},{"hidden_lines",true}});
  near(projected.at("tolerance_mm"),.02);extent(view(projected,"front"),20,6);extent(view(projected,"top"),20,10);extent(view(projected,"right"),10,6);
  for(const auto& item:projected.at("views"))for(const auto& entity:item.at("entities"))if(entity.at("kind")=="line")require(entity.at("points").size()==2,"line projection retains exactly two endpoints");
  BuiltModel drilled(bore());const auto hidden=drilled.drawing({{"views",views},{"hidden_lines",true}});
  const auto& front=view(hidden,"front");bool bore_side=false;
  for(const auto& entity:front.at("entities"))if(entity.at("kind")=="line"&&entity.at("hidden")==true){const auto& p=entity.at("points");
    const double x=p[0][0];if(std::abs(x-p[1][0].get<double>())<1e-6&&(std::abs(x-8)<1e-6||std::abs(x-12)<1e-6)&&std::abs(p[0][1].get<double>()-p[1][1].get<double>())>5.9)bore_side=true;}
  require(bore_side,"HLR front view includes hidden through-bore walls");
  require(circles(view(hidden,"top"),10,5,2)==1,"top HLR retains one exact bore circle after coincident-edge deduplication");
  const auto visible=drilled.drawing({{"views",views},{"hidden_lines",false}});
  for(const auto& item:visible.at("views"))for(const auto& entity:item.at("entities"))require(entity.at("hidden")==false,"hidden_lines false excludes hidden geometry");
  const auto section=drilled.drawing({{"views",Json::array({{{"id","cut"},{"orientation","section"},{"section",{{"axis","z"},{"offset",3}}}}})},{"hidden_lines",true}});
  extent(view(section,"cut"),20,10);require(circles(view(section,"cut"),10,5,2)==1,"exact section intersects bore at requested plane");
  fails("empty_section",[&]{drilled.drawing({{"views",Json::array({{{"id","cut"},{"orientation","section"},{"section",{{"axis","z"},{"offset",100}}}}})}});});
  auto stepped=box();stepped["features"]=Json::array({{{"id","base"},{"type","box"},{"size",{20,10,3}}},
    {{"id","top"},{"type","box"},{"size",{10,6,3}},{"origin",{0,0,3}}},
    {{"id","step"},{"type","fuse"},{"left","base"},{"right","top"}}});stepped["output"]="step";
  BuiltModel stair(stepped);const auto cuts=stair.drawing({{"views",Json::array({
    {{"id","low"},{"orientation","section"},{"section",{{"axis","z"},{"offset",1}}}},
    {{"id","high"},{"orientation","section"},{"section",{{"axis","z"},{"offset",4}}}}})}});
  extent(view(cuts,"low"),20,10);extent(view(cuts,"high"),10,6);
  auto half=box();half["features"]=Json::array({{{"id","cylinder"},{"type","cylinder"},{"radius",5},{"height",6}},
    {{"id","tool"},{"type","box"},{"size",{6,12,8}},{"origin",{0,-6,-1}}},
    {{"id","half"},{"type","cut"},{"left","cylinder"},{"right","tool"}}});half["output"]="half";
  BuiltModel semicylinder(half);const auto arcs=semicylinder.drawing({{"views",Json::array({{{"id","top"},{"orientation","top"}}})}});
  bool analytic_arc=false;
  for(const auto& entity:view(arcs,"top").at("entities"))if(entity.at("kind")=="arc"&&std::abs(entity.at("radius").get<double>()-5)<1e-6) {
    near(entity.at("end_deg").get<double>()-entity.at("start_deg").get<double>(),180);analytic_arc=true;
  }
  require(analytic_arc,"trimmed circular edges stay exact analytic arcs in projection");
}
void service_tests() {
  Temporary temporary;Service service(temporary.path);call(service,"cad_create",{{"document_id","part"},{"model",bore()}});
  Json recipe={{"title","Plate & fixture <A>"},{"sheet","A4"},{"scale",2},{"material","Aluminum"},{"notes",{"All dimensions in mm"}},
    {"views",Json::array({{{"id","front"},{"orientation","front"}},{{"id","top"},{"orientation","top"}}})},
    {"dimensions",Json::array({{{"view","front"},{"kind","width"}},{{"view","front"},{"kind","height"}},
      {{"view","top"},{"kind","diameter"},{"center",Json::array({Json{{"parameter","cx"}},Json{{"parameter","cy"}}})},{"radius",{{"parameter","bore_r"}}}},
      {{"view","front"},{"kind","horizontal"},{"from",{0,0}},{"to",{20,0}}}})}};
  const auto result=call(service,"cad_drawing",{{"document_id","part"},{"revision",1},{"drawing",recipe}});
  require(result.at("document_id")=="part"&&result.at("revision")==1&&result.at("units")=="mm","drawing retains committed identity and units");near(result.at("scale"),2);
  require(result.at("sheet_mm")==Json::array({297,210}),"A4 landscape sheet dimensions");near(result.at("projection_tolerance_mm"),.02);
  const auto saved=parse_json(read_text(path_from_utf8(result.at("recipe_path").get<std::string>())));
  require(saved.at("drawing")==recipe,"sidecar preserves parameter references in replayable recipe");
  require(saved.at("resolved_drawing").at("dimensions")[2].at("radius")==2,"sidecar also records resolved dimension evidence");
  require(saved.at("model_sha256").get<std::string>().size()==64,"sidecar records source model identity");
  const auto manifest=parse_json(read_text(path_from_utf8(result.at("path").get<std::string>())));
  require(manifest.at("document_id")=="part"&&manifest.at("revision")==1,"manifest is independently readable");
  std::set<std::string> formats;bool exact_circle=false;std::map<fs::path,std::string> originals;
  for(const auto& artifact:result.at("artifacts")) {
    const auto path=path_from_utf8(artifact.at("path").get<std::string>());const auto content=read_text(path,64*1024*1024);originals.emplace(path,content);
    require(fs::file_size(path)==artifact.at("bytes").get<std::uintmax_t>()&&fs::file_size(path)>0,"drawing manifest byte counts match independent files");
    const auto format=artifact.at("format").get<std::string>();formats.insert(format);
    if(format=="pdf")pdf_check(content);
    if(format=="svg")require(content.find("<svg")!=std::string::npos&&content.find("Plate &amp; fixture &lt;A&gt;")!=std::string::npos&&content.find("<A>")==std::string::npos,"SVG is native vector output with escaped annotation text");
    if(format=="dxf")dxf_check(content,&exact_circle);
  }
  require(formats==std::set<std::string>({"svg","pdf","dxf"})&&result.at("artifacts").size()==4,"default formats include sheet SVG/PDF and separate per-view DXF");require(exact_circle,"DXF uses an exact CIRCLE entity with nominal 2 mm bore radius");
  const auto& dimensions=result.at("dimensions");require(dimensions.size()==4,"all requested attached dimensions returned");near(dimensions[0].at("value_mm"),20);near(dimensions[1].at("value_mm"),6);near(dimensions[2].at("value_mm"),4);near(dimensions[3].at("value_mm"),20);
  const auto before=exported_files(temporary.path);
  auto missing=recipe;missing["dimensions"][2]["center"]={100,100};
  fails("drawing_reference_not_found",[&]{call(service,"cad_drawing",{{"document_id","part"},{"revision",1},{"drawing",missing}});});
  missing=recipe;missing["dimensions"][3]["from"]={1,1};
  fails("drawing_reference_not_found",[&]{call(service,"cad_drawing",{{"document_id","part"},{"revision",1},{"drawing",missing}});});
  auto invalid=recipe;invalid["views"].push_back(invalid["views"][0]);
  fails("invalid_drawing",[&]{call(service,"cad_drawing",{{"document_id","part"},{"revision",1},{"drawing",invalid}});});
  require(exported_files(temporary.path)==before,"failed drawing requests publish no artifacts");
  require(call(service,"cad_read",{{"document_id","part"}}).at("revision")==1,"failed drawing requests preserve HEAD");
  call(service,"cad_apply",{{"document_id","part"},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","height"},{"value",8}},{{"op","set_parameter"},{"name","bore_r"},{"value",3}}})}});
  Service reopened(temporary.path);const auto regenerated=call(reopened,"cad_drawing",{{"document_id","part"},{"revision",2},{"drawing",saved.at("drawing")}});
  near(regenerated.at("dimensions")[1].at("value_mm"),8);near(regenerated.at("dimensions")[2].at("value_mm"),6);
  require(regenerated.at("path")!=result.at("path"),"regeneration publishes a fresh artifact set");
  for(const auto& [path,content]:originals)require(read_text(path,64*1024*1024)==content,"regeneration preserves original revision artifacts");
  const auto historical=call(reopened,"cad_drawing",{{"document_id","part"},{"revision",1},{"drawing",saved.at("drawing")}});near(historical.at("dimensions")[1].at("value_mm"),6);near(historical.at("dimensions")[2].at("value_mm"),4);
  const auto queued=call(reopened,"cad_job",{{"action","submit"},{"request_id","drawing_job"},{"tool","cad_drawing"},{"arguments",{{"document_id","part"},{"revision",2},{"drawing",{{"formats",{"svg"}}}}}}});
  require(queued.at("state")=="queued","drawing jobs submit without waiting for geometry");Json done;
  for(int attempt=0;attempt<3000;++attempt){done=call(reopened,"cad_job",{{"action","get"},{"job_id","drawing_job"}});if(done.at("state")=="succeeded"||done.at("state")=="failed")break;std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  require(done.at("state")=="succeeded"&&done.at("result").at("artifacts").size()==1,"isolated drawing job succeeds with selected format");
  auto rings=box();rings["features"]=Json::array({{{"id","outer"},{"type","cylinder"},{"radius",1.03},{"height",5}},{{"id","inner"},{"type","cylinder"},{"radius",1},{"height",7},{"origin",{0,0,-1}}},{{"id","ring"},{"type","cut"},{"left","outer"},{"right","inner"}}});rings["output"]="ring";
  call(reopened,"cad_create",{{"document_id","rings"},{"model",rings}});
  fails("drawing_reference_ambiguous",[&]{call(reopened,"cad_drawing",{{"document_id","rings"},{"revision",1},{"drawing",{{"views",Json::array({{{"id","top"},{"orientation","top"}}})},{"dimensions",Json::array({{{"view","top"},{"kind","radius"},{"center",{0,0}},{"radius",1.015},{"tolerance",.05}}})}}}});});
  // Both concentric circles fall inside the attachment tolerance even though
  // one is nearer. Choosing that nearer circle would silently change intent.
  fails("drawing_reference_ambiguous",[&]{call(reopened,"cad_drawing",{{"document_id","rings"},{"revision",1},{"drawing",{{"views",Json::array({{{"id","top"},{"orientation","top"}}})},
    {"dimensions",Json::array({{{"view","top"},{"kind","horizontal"},{"from",{1.012,0}},{"to",{-1.03,0}}}})}}}});});
}
}
int main(){try{configure_kernel_logging();set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));geometry_tests();service_tests();std::cout<<"drawing: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<"FAILED: "<<error.what()<<'\n';return 1;}}
