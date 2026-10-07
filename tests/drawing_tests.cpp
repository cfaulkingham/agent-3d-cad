#include "agentcad/jobs.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/service.hpp"
#include "agentcad/drawing.hpp"
#include <algorithm>
#include <array>
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
Json rendered(const Json& model,const Json& recipe) {
  BuiltModel solid(model);const auto spec=normalize_drawing(recipe,model);
  return render_drawing(solid.drawing({{"views",spec.at("views")},{"hidden_lines",spec.at("hidden_lines")}}),spec,
    {{"document_id","layout_part"},{"revision",1},{"kernel_version",kernel_version()}});
}
const Json& placement(const Json& drawing,const std::string& id) {
  for(const auto& p:drawing.at("view_layouts")) if(p.at("view")==id) return p;
  throw std::runtime_error("Missing view placement");
}
std::string artifact(const Json& drawing,const std::string& name) {
  for(const auto& f:drawing.at("files")) if(f.at("name")==name) return f.at("content");
  throw std::runtime_error("Missing rendered artifact");
}
void paint_order_tests() {
  // Different 3D edges can have coincident projections. Exercise the renderer
  // directly so the kernel's exact-edge deduplication cannot remove this case.
  Json entities=Json::array({
    {{"kind","circle"},{"hidden",false},{"center",{5,5}},{"radius",2}},
    {{"kind","arc"},{"hidden",false},{"center",{12,5}},{"radius",3},{"start_deg",30},{"end_deg",210}}});
  for(std::size_t i=0;i<2;++i) {auto duplicate=entities[i];duplicate["hidden"]=true;entities.push_back(std::move(duplicate));}
  const Json projected={{"views",Json::array({{{"id","front"},{"orientation","front"},{"bounds_mm",{0,0,20,10}},{"entities",entities}}})}};
  for(const bool hidden:{true,false}) {
    const auto spec=normalize_drawing({{"views",Json::array({{{"id","front"},{"orientation","front"}}})},
      {"formats",{"svg","pdf","dxf"}},{"dimensions",Json::array()},{"scale",1},{"hidden_lines",hidden}},box());
    const auto drawing=render_drawing(projected,spec,{{"document_id","paint_order"},{"revision",1},{"kernel_version",kernel_version()}});
    const std::size_t expected=hidden?4:2;
    auto expected_hidden=[&](std::size_t i){return hidden && i<2;};

    const auto svg=artifact(drawing,"drawing.svg");
    std::istringstream svg_input(svg);std::string line;
    std::vector<std::pair<std::string,std::string>> svg_curves;
    bool seen_center=false;
    while(std::getline(svg_input,line)) {
      if(line.find("data-layer=\"CENTER\"")!=std::string::npos) seen_center=true;
      if(!line.starts_with("<circle ") && !line.starts_with("<path ")) continue;
      require(!seen_center,"SVG center annotations follow geometry");
      const auto layer=line.find(" data-layer=");require(layer!=std::string::npos,"SVG curve declares its layer");
      svg_curves.emplace_back(line.substr(layer),line.substr(0,layer));
    }
    require(seen_center && svg_curves.size()==expected,"SVG preserves curved geometry and center annotations");
    for(std::size_t i=0;i<svg_curves.size();++i) {
      const auto& style=svg_curves[i].first;
      require(style.find(expected_hidden(i)?"data-layer=\"HIDDEN\"":"data-layer=\"VISIBLE\"")!=std::string::npos,"SVG paints hidden curves before visible curves");
      require((style.find("stroke-dasharray=")!=std::string::npos)==expected_hidden(i),"SVG visible curves paint continuously over hidden dashes");
    }
    if(hidden) for(std::size_t i=0;i<2;++i) require(svg_curves[i].second==svg_curves[i+2].second,"SVG visible strokes exactly coincide with the earlier hidden curves");

    const auto pdf=artifact(drawing,"drawing.pdf");
    const auto start=pdf.find("stream\n"),end=pdf.find("endstream",start);
    require(start!=std::string::npos && end!=std::string::npos,"PDF drawing stream is present");
    std::istringstream pdf_input(pdf.substr(start+7,end-start-7));
    std::vector<std::pair<std::string,std::string>> pdf_curves;std::string style,path;
    while(std::getline(pdf_input,line)) {
      if(line.find(" G ")!=std::string::npos) {style=line;path.clear();}
      else if(line=="S") {if(path.find(" c\n")!=std::string::npos) pdf_curves.emplace_back(style,path);path.clear();}
      else path+=line+'\n';
    }
    require(pdf_curves.size()==expected,"PDF preserves every requested curved stroke");
    for(std::size_t i=0;i<pdf_curves.size();++i) {
      const auto& stroke=pdf_curves[i].first;
      require(stroke.starts_with(expected_hidden(i)?"0.3 G ":"0 G "),"PDF paints gray hidden curves before black visible curves");
      require((stroke.find("[] 0 d")!=std::string::npos)!=expected_hidden(i),"PDF visible strokes reset the hidden dash pattern");
    }
    if(hidden) for(std::size_t i=0;i<2;++i) require(pdf_curves[i].second==pdf_curves[i+2].second,"PDF visible paths exactly cover coincident hidden curves");

    const auto pairs=dxf_pairs(artifact(drawing,"front.dxf"));
    std::vector<std::pair<std::string,Dxf>> dxf_curves;seen_center=false;
    for(std::size_t i=0;i<pairs.size();++i) if(pairs[i].first==0) {
      std::string layer;Dxf coordinates;
      for(std::size_t j=i+1;j<pairs.size() && pairs[j].first!=0;++j) {
        if(pairs[j].first==8) layer=pairs[j].second;
        if(pairs[j].first==10 || pairs[j].first==20 || pairs[j].first==30 || pairs[j].first==40 || pairs[j].first==50 || pairs[j].first==51) coordinates.push_back(pairs[j]);
      }
      if(layer=="CENTER") seen_center=true;
      if(pairs[i].second!="CIRCLE" && pairs[i].second!="ARC") continue;
      require(!seen_center,"DXF center annotations follow geometry");
      coordinates.insert(coordinates.begin(),pairs[i]);dxf_curves.emplace_back(layer,std::move(coordinates));
    }
    require(seen_center && dxf_curves.size()==expected,"DXF retains analytic curves and center annotations");
    for(std::size_t i=0;i<dxf_curves.size();++i) require(dxf_curves[i].first==(expected_hidden(i)?"HIDDEN":"VISIBLE"),"DXF emits hidden curves before visible curves");
    if(hidden) for(std::size_t i=0;i<2;++i) require(dxf_curves[i].second==dxf_curves[i+2].second,"DXF coincident curves retain identical analytic geometry and stable order");
  }
  require(projected.at("views")[0].at("entities")==entities,"Paint ordering leaves the cached projection unchanged");
}
std::vector<std::array<double,4>> hatch_lines(const std::string& content) {
  const auto pairs=dxf_pairs(content);std::vector<std::array<double,4>> result;
  for(std::size_t i=0;i<pairs.size();++i) if(pairs[i]==std::pair<int,std::string>{0,"LINE"}) {
    std::array<double,4> p{};bool hatch=false;
    for(std::size_t j=i+1;j<pairs.size() && pairs[j].first!=0;++j) {
      const auto& [code,value]=pairs[j];if(code==8) hatch=value=="HATCH";
      if(code==10) p[0]=std::stod(value);if(code==20) p[1]=std::stod(value);
      if(code==11) p[2]=std::stod(value);if(code==21) p[3]=std::stod(value);
    }
    if(hatch) result.push_back(p);
  }
  return result;
}
void layout_hatch_tests() {
  const Json orthographic=Json::array({{{"id","side"},{"orientation","right"}},{{"id","plan"},{"orientation","top"}},{{"id","elevation"},{"orientation","front"}}});
  auto translated=box();translated["features"][0]["origin"]={-50,30,-15};
  for(const auto& layout:{"third_angle","first_angle"}) {
    const auto drawing=rendered(translated,{{"layout",layout},{"views",orthographic},{"scale",2},
      {"dimensions",Json::array({{{"view","elevation"},{"kind","height"}},{{"view","plan"},{"kind","width"}}})}});
    const auto& front=placement(drawing,"elevation");const auto& top=placement(drawing,"plan");const auto& right=placement(drawing,"side");
    near(front.at("origin_mm")[0],top.at("origin_mm")[0]);near(front.at("origin_mm")[1],right.at("origin_mm")[1]);
    const bool third=std::string(layout)=="third_angle";
    require((top.at("cell_mm")[1].get<double>()<front.at("cell_mm")[1].get<double>())==third,"top view placed on declared projection side regardless of recipe order");
    require((right.at("cell_mm")[0].get<double>()>front.at("cell_mm")[0].get<double>())==third,"right view placed on declared projection side");
    require(artifact(drawing,"drawing.svg").find(third?"Third-angle":"First-angle")!=std::string::npos,"sheet identifies projection convention");
    // Every transformed geometry bound must remain inside its allocated cell.
    for(const auto& p:drawing.at("view_layouts")) {
      const auto& cell=p.at("cell_mm");const auto& origin=p.at("origin_mm");
      const bool side=p.at("view")=="side",plan=p.at("view")=="plan";
      const double x0=side?30:-50,x1=side?40:-30,y0=plan?30:-15,y1=plan?40:-9;
      require(origin[0].get<double>()+2*x0>=cell[0].get<double>() && origin[0].get<double>()+2*x1<=cell[0].get<double>()+cell[2].get<double>(),"aligned geometry fits cell horizontally");
      require(origin[1].get<double>()-2*y1>=cell[1].get<double>() && origin[1].get<double>()-2*y0<=cell[1].get<double>()+cell[3].get<double>(),"aligned geometry fits cell vertically");
    }
  }
  require(normalize_drawing(Json::object(),box()).at("layout")=="third_angle","default four-view preset uses third angle");
  require(normalize_drawing({{"views",orthographic}},box()).at("layout")=="grid","existing explicit-view recipes preserve grid order");
  fails("invalid_drawing",[&]{normalize_drawing({{"layout","third_angle"},{"views",Json::array({{{"id","top"},{"orientation","top"}}})}},box());});
  auto duplicate=orthographic;duplicate.push_back({{"id","another"},{"orientation","front"}});
  fails("invalid_drawing",[&]{normalize_drawing({{"layout","first_angle"},{"views",duplicate}},box());});
  auto extras=orthographic;extras.push_back({{"id","cut"},{"orientation","section"},{"section",{{"axis","z"},{"offset",3}}}});
  extras.push_back({{"id","iso"},{"orientation","isometric"}});
  const auto five=rendered(bore(),{{"layout","third_angle"},{"sheet","A3"},{"views",extras}});
  require(placement(five,"iso").at("cell_mm")[0]<placement(five,"cut").at("cell_mm")[0],"isometric stays in reserved cell even after a section in recipe order");
  Json section={{"id","cut"},{"orientation","section"},{"section",{{"axis","z"},{"offset",3}}}};
  for(double scale:{.5,2.0,5.0}) {
    const auto drawing=rendered(bore(),{{"scale",scale},{"views",Json::array({section})}});
    const auto lines=hatch_lines(artifact(drawing,"cut.dxf"));require(!lines.empty(),"section emits hatch segments in DXF");
    require(artifact(drawing,"drawing.svg").find("data-layer=\"HATCH\"")!=std::string::npos,"SVG emits the same hatch layer");pdf_check(artifact(drawing,"drawing.pdf"));
    std::set<long long> offsets;
    for(const auto& p:lines) {
      near(p[2]-p[0],p[3]-p[1],2e-6);
      const double offset=(p[1]-p[0])/std::sqrt(2.0)*scale/2.5;
      near(offset,std::round(offset),2e-6);offsets.insert(std::llround(offset));
      for(double t:{0.0,.1,.5,.9,1.0}) {
        const double x=p[0]+(p[2]-p[0])*t,y=p[1]+(p[3]-p[1])*t;
        require(x>=-1e-6 && x<=20+1e-6 && y>=-1e-6 && y<=10+1e-6,"hatching stays within section outer boundary");
        require(std::hypot(x-10,y-5)>=2-1e-6,"hatching never crosses bore void");
      }
    }
    require(offsets.size()>2,"section contains multiple regularly spaced hatch lines");
  }
  section["hatch"]=false;
  const auto bare=rendered(bore(),{{"views",Json::array({section})}});
  require(hatch_lines(artifact(bare,"cut.dxf")).empty(),"explicit hatch false retains outline-only section");
  require(artifact(bare,"drawing.svg").find("data-layer=\"HATCH\"")==std::string::npos,"outline-only SVG has no hatch strokes");
  section["hatch"]=true;section["section"]={{"axis","x"},{"offset",10}};
  const auto split=rendered(bore(),{{"views",Json::array({section})}});
  const auto split_lines=hatch_lines(artifact(split,"cut.dxf"));require(!split_lines.empty(),"section through bore axis hatches disconnected material regions");
  for(const auto& p:split_lines) {
    require((p[0]<=3+1e-6 && p[2]<=3+1e-6) || (p[0]>=7-1e-6 && p[2]>=7-1e-6),"axial section leaves through-bore channel empty");
  }
  auto ring=box();ring["features"]=Json::array({{{"id","outer"},{"type","cylinder"},{"radius",5},{"height",6}},
    {{"id","inner"},{"type","cylinder"},{"radius",2},{"height",8},{"origin",{0,0,-1}}},
    {{"id","ring"},{"type","cut"},{"left","outer"},{"right","inner"}}});ring["output"]="ring";
  section["section"]={{"axis","z"},{"offset",3}};
  const auto annulus=rendered(ring,{{"scale",5},{"views",Json::array({section})}});
  const auto ring_lines=hatch_lines(artifact(annulus,"cut.dxf"));require(!ring_lines.empty(),"annular section hatches analytic circular regions");
  for(const auto& p:ring_lines) for(double t:{0.0,.25,.5,.75,1.0}) {
    const double r=std::hypot(p[0]+t*(p[2]-p[0]),p[1]+t*(p[3]-p[1]));
    require(r>=2-1e-6 && r<=5+1e-6,"analytic hatch clipping preserves annular material including negative coordinates");
  }
  // A material island inside a cavity must be included, not mistaken for a hole.
  auto island=ring;island["features"].push_back({{"id","pin"},{"type","cylinder"},{"radius",1},{"height",6}});
  island["features"].push_back({{"id","together"},{"type","fuse"},{"left","ring"},{"right","pin"}});island["output"]="together";
  const auto nested=rendered(island,{{"scale",5},{"views",Json::array({section})}});
  bool island_hatched=false;
  for(const auto& p:hatch_lines(artifact(nested,"cut.dxf"))) {
    const double r=std::hypot((p[0]+p[2])/2,(p[1]+p[3])/2);island_hatched|=r<1;
    require(r<=1+1e-6 || (r>=2-1e-6 && r<=5+1e-6),"island hatch retains surrounding annular void");
  }
  require(island_hatched,"nested material island is hatched");
  auto overlap=box();overlap["features"].push_back({{"id","copies"},{"type","pattern"},{"input","base"},{"count",2},{"step",{10,0,0}}});overlap["output"]="copies";
  const auto united=rendered(overlap,{{"scale",2},{"views",Json::array({section})}});
  const auto union_lines=hatch_lines(artifact(united,"cut.dxf"));bool overlap_hatched=false;
  for(const auto& p:union_lines) for(double t:{.1,.5,.9}) {
    const double x=p[0]+t*(p[2]-p[0]),y=p[1]+t*(p[3]-p[1]);
    require(x>=-1e-6 && x<=30+1e-6 && y>=-1e-6 && y<=10+1e-6,"overlapping material is clipped to union");
    overlap_hatched|=x>10 && x<20;
  }
  require(overlap_hatched,"overlapping solids do not cancel hatching by parity");
  // Exact circular seams can lie on a hatch scan line at these radii.
  for(double radius:{2.5*std::sqrt(2.0),5*std::sqrt(2.0)}) {
    auto circle=box();circle["features"]=Json::array({{{"id","base"},{"type","cylinder"},{"radius",radius},{"height",6}}});
    require(!hatch_lines(artifact(rendered(circle,{{"scale",1},{"views",Json::array({section})}}),"cut.dxf")).empty(),"circle seam on scan line retains even crossing count");
  }
  auto cylinder=box();cylinder["features"]=Json::array({{{"id","base"},{"type","cylinder"},{"radius",5},{"height",6}}});
  auto tangent=section;tangent["section"]={{"axis","x"},{"offset",5}};
  fails("invalid_drawing",[&]{rendered(cylinder,{{"views",Json::array({tangent})}});});
  tangent["hatch"]=false;
  require(hatch_lines(artifact(rendered(cylinder,{{"views",Json::array({tangent})}}),"cut.dxf")).empty(),"tangent section can explicitly export its outline without invented material");
  auto tilted=box();tilted["features"]=Json::array({{{"id","base"},{"type","cylinder"},{"radius",4},{"height",10}},
    {{"id","tilted"},{"type","transform"},{"input","base"},{"rotation",{{"origin",{0,0,0}},{"axis",{1,0,0}},{"angle_deg",20}}}}});tilted["output"]="tilted";
  const auto ellipse=rendered(tilted,{{"scale",5},{"views",Json::array({section})}});
  const auto ellipse_lines=hatch_lines(artifact(ellipse,"cut.dxf"));require(ellipse_lines.size()>10,"noncircular section produces hatch lines");
  for(const auto& p:ellipse_lines) for(double t:{0.0,.5,1.0}) {
    const double x=p[0]+t*(p[2]-p[0]),y=p[1]+t*(p[3]-p[1]);
    const double local_y=y*std::cos(20*std::acos(-1)/180)+3*std::sin(20*std::acos(-1)/180);
    require(x*x+local_y*local_y<=16+1e-5,"polyline-clipped elliptical section stays in material");
  }
  // A malformed/budget-heavy boundary must fail rather than omit hatch lines.
  const auto spec=normalize_drawing({{"scale",5},{"views",Json::array({section})}},box());
  BuiltModel base(box());auto excessive=base.drawing({{"views",spec.at("views")}});
  Json points=Json::array();for(int i=0;i<15000;++i)for(const auto& p:Json::array({Json::array({0,0}),Json::array({20,0}),Json::array({20,10}),Json::array({0,10})}))points.push_back(p);
  points.push_back({0,0});excessive["views"][0]["section_regions"]=Json::array({Json::array({{{"kind","polyline"},{"hidden",false},{"points",points}}})});
  fails("limit_exceeded",[&]{render_drawing(excessive,spec,{{"document_id","budget"},{"revision",1},{"kernel_version",kernel_version()}});});
}
Json wedge() {
  return {{"schema_version",1},{"units","mm"},{"parameters",{{"height",30},{"linear_allowance",.1},{"angle_allowance",.5}}},
    {"features",Json::array({{{"id","profile"},{"type","sketch"},{"workplane",{{"origin",{0,0,0}},{"normal",{0,0,1}},{"x_direction",{1,0,0}}}},
      {"profile",{{"type","polygon"},{"points",Json::array({Json::array({0,0}),Json::array({40,0}),Json::array({0,Json{{"parameter","height"}}})})}}}},
      {{"id","body"},{"type","extrude"},{"input","profile"},{"distance",6}}})},{"output","body"}};
}
Json slope_dimension() {
  return {{"view","top"},{"kind","angular"},{"arc_radius",12},{"lines",Json::array({
    {{"from",{40,0}},{"to",{0,0}}},
    {{"from",{40,0}},{"to",Json::array({0,Json{{"parameter","height"}}})}}})}};
}
void angular_tolerance_tests() {
  auto angle=slope_dimension();
  Json recipe={{"scale",2},{"views",Json::array({{{"id","top"},{"orientation","top"}}})},{"dimensions",Json::array({angle})}};
  const auto acute=rendered(wedge(),recipe);const double expected=std::atan2(30.,40.)*180/std::acos(-1.);
  near(acute.at("dimensions")[0].at("value_deg"),expected,1e-9);
  require(!acute.at("dimensions")[0].contains("value_mm"),"angular result uses degrees, never mislabeled millimeters");
  near(acute.at("dimensions")[0].at("vertex_mm")[0],40);near(acute.at("dimensions")[0].at("vertex_mm")[1],0);
  auto section=recipe;section["views"][0]={{"id","top"},{"orientation","section"},{"section",{{"axis","z"},{"offset",3}}}};
  near(rendered(wedge(),section).at("dimensions")[0].at("value_deg"),expected,1e-9);
  const auto pairs=dxf_pairs(artifact(acute,"top.dxf"));bool measured_arc=false;
  for(std::size_t i=0;i<pairs.size();++i) if(pairs[i]==std::pair<int,std::string>{0,"ARC"}) {
    std::string layer;double radius=0,start=0,end=0;
    for(std::size_t j=i+1;j<pairs.size() && pairs[j].first!=0;++j) {
      const auto& [code,value]=pairs[j];if(code==8) layer=value;if(code==40) radius=std::stod(value);if(code==50) start=std::stod(value);if(code==51) end=std::stod(value);
    }
    if(layer=="DIMENSIONS") {near(radius,12);near(std::fmod(end-start+360,360),expected,2e-6);measured_arc=true;}
  }
  require(measured_arc,"DXF contains a true 1:1 angular annotation ARC");
  recipe["dimensions"][0]["sweep"]="major";
  near(rendered(wedge(),recipe).at("dimensions")[0].at("value_deg"),360-expected,1e-9);
  recipe["dimensions"][0]=angle;std::swap(recipe["dimensions"][0]["lines"][0]["from"],recipe["dimensions"][0]["lines"][0]["to"]);
  near(rendered(wedge(),recipe).at("dimensions")[0].at("value_deg"),180-expected,1e-9);
  recipe["dimensions"][0]=angle;
  recipe["general_tolerances"]={{"linear",{{"parameter","linear_allowance"}}},{"angular",{{"parameter","angle_allowance"}}}};
  recipe["dimensions"].push_back({{"view","top"},{"kind","width"}});
  auto result=rendered(wedge(),recipe);const auto& general_angle=result.at("dimensions")[0];
  require(general_angle.at("tolerance_source")=="general","angular general tolerance is explicitly inherited");
  near(general_angle.at("lower_limit_deg"),36.869898-.5);near(general_angle.at("upper_limit_deg"),36.869898+.5);
  require(general_angle.at("label")=="36.869898 deg","toleranced nominal retains six-decimal display precision");
  require(artifact(result,"drawing.svg").find("General tolerances")!=std::string::npos,"sheet explicitly prints requested general allowances");
  require(artifact(result,"top.dxf").find("General linear tolerance: +/-0.1 mm")!=std::string::npos &&
    artifact(result,"top.dxf").find("General angular tolerance: +/-0.5 deg")!=std::string::npos,"standalone DXF retains general allowances for inherited dimensions");
  near(result.at("dimensions")[1].at("lower_limit_mm"),39.9);near(result.at("dimensions")[1].at("upper_limit_mm"),40.1);
  recipe["dimensions"][0]["manufacturing_tolerance"]={{"type","symmetric"},{"value",.125}};
  recipe["dimensions"][1]["manufacturing_tolerance"]={{"type","deviation"},{"lower",-.02},{"upper",.01}};
  result=rendered(wedge(),recipe);
  require(result.at("dimensions")[0].at("label")=="36.869898 +/-0.125 deg","explicit angular tolerance overrides the general allowance");
  require(result.at("dimensions")[0].at("tolerance_source")=="dimension","effective tolerance source is reported");
  require(result.at("dimensions")[1].at("label")=="40 +0.01/-0.02","signed deviations retain their signs and upper/lower order");
  near(result.at("dimensions")[1].at("lower_limit_mm"),39.98);
  recipe["dimensions"][1]["manufacturing_tolerance"]={{"type","deviation"},{"lower",0},{"upper",.000001}};
  require(rendered(wedge(),recipe).at("dimensions")[1].at("label")=="40 +0.000001/+0","micron allowance is not rounded to zero");
  recipe["dimensions"][1]["manufacturing_tolerance"]={{"type","deviation"},{"lower",.01},{"upper",.03}};
  require(rendered(wedge(),recipe).at("dimensions")[1].at("label")=="40 +0.03/+0.01","same-sign deviations retain a signed shifted acceptance zone");
  recipe["dimensions"][1]["manufacturing_tolerance"]={{"type","limits"},{"lower",39.9},{"upper",40.2}};
  result=rendered(wedge(),recipe);require(result.at("dimensions")[1].at("label")=="39.9..40.2 LIMITS","absolute limits are printed as supplied");
  near(result.at("dimensions")[1].at("lower_limit_mm"),39.9);near(result.at("dimensions")[1].at("upper_limit_mm"),40.2);
  auto bad=recipe;bad["dimensions"][1]["manufacturing_tolerance"]["lower"]=40.1;
  fails("invalid_drawing",[&]{rendered(wedge(),bad);});
  for(const auto& tolerance:Json::array({
      {{"type","symmetric"},{"value",0}},{{"type","symmetric"},{"value",-.1}},{{"type","symmetric"},{"value",.0000001}},
      {{"type","deviation"},{"lower",.2},{"upper",.1}},{{"type","limits"},{"lower",-.1},{"upper",40.1}}})) {
    bad=recipe;bad["dimensions"][1]["manufacturing_tolerance"]=tolerance;
    fails("invalid_drawing",[&]{normalize_drawing(bad,wedge());});
  }
  bad=recipe;bad["dimensions"][0]["manufacturing_tolerance"]={{"type","symmetric"},{"value",{{"expression",{{"op","multiply"},{"args",{.5,1}},{"unit","mm"}}}}}};
  fails("invalid_model",[&]{normalize_drawing(bad,wedge());});
  bad=recipe;bad["dimensions"][0]["manufacturing_tolerance"]={{"type","symmetric"},{"value",100}};
  fails("invalid_drawing",[&]{rendered(wedge(),bad);});
  bad=recipe;bad["dimensions"][0]["lines"][0]["to"]={3,3};
  fails("drawing_reference_not_found",[&]{rendered(wedge(),bad);});
  bad=recipe;bad["dimensions"][0]["lines"][1]=bad["dimensions"][0]["lines"][0];
  fails("invalid_drawing",[&]{rendered(wedge(),bad);});
  bad=recipe;bad["dimensions"][0]["arc_radius"]=.01;
  fails("invalid_drawing",[&]{rendered(wedge(),bad);});
  // Two close edges are still ambiguous even when one is nearer to the rule.
  auto thin=box();thin["features"][0]["size"]={20,.03,6};
  Json ambiguous={{"views",Json::array({{{"id","top"},{"orientation","top"}}})},{"dimensions",Json::array({
    {{"view","top"},{"kind","angular"},{"lines",Json::array({{{"from",{1,.014}},{"to",{19,.014}}},{{"from",{0,0}},{"to",{0,1}}}})}}})}};
  fails("drawing_reference_ambiguous",[&]{rendered(thin,ambiguous);});
  // Circle-matching tolerance must never become a manufacturing allowance.
  Json circular={{"views",Json::array({{{"id","top"},{"orientation","top"}}})},{"dimensions",Json::array({
    {{"view","top"},{"kind","diameter"},{"center",{10,5}},{"radius",2},{"tolerance",.05}}})}};
  result=rendered(bore(),circular);require(!result.at("dimensions")[0].contains("manufacturing_tolerance"),"matching tolerance is not printed or applied as fabrication tolerance");
  circular["dimensions"][0]["manufacturing_tolerance"]={{"type","symmetric"},{"value",.0001}};
  result=rendered(bore(),circular);require(result.at("dimensions")[0].at("label")=="DIA 4 +/-0.0001","radial manufacturing tolerance is independent of circle matching");
  near(result.at("dimensions")[0].at("lower_limit_mm"),3.9999);
}
void angular_service_tests() {
  Temporary temporary;Service service(temporary.path);call(service,"cad_create",{{"document_id","wedge"},{"model",wedge()}});
  const auto original=call(service,"cad_read",{{"document_id","wedge"}});
  Json angle=slope_dimension();angle["manufacturing_tolerance"]={{"type","symmetric"},{"value",{{"parameter","angle_allowance"}}}};
  Json recipe={{"views",Json::array({{{"id","top"},{"orientation","top"}}})},{"dimensions",Json::array({angle})}};
  const auto first=call(service,"cad_drawing",{{"document_id","wedge"},{"revision",1},{"drawing",recipe}});
  const auto saved=parse_json(read_text(path_from_utf8(first.at("recipe_path"))));
  require(saved.at("drawing")==recipe,"saved recipe preserves angular/tolerance parameter references");
  require(call(service,"cad_read",{{"document_id","wedge"}})==original,"angular drawing does not mutate editable source");
  const auto files=exported_files(temporary.path);auto bad=recipe;bad["dimensions"][0]["manufacturing_tolerance"]={{"type","limits"},{"lower",80},{"upper",100}};
  fails("invalid_drawing",[&]{call(service,"cad_drawing",{{"document_id","wedge"},{"revision",1},{"drawing",bad}});});
  require(exported_files(temporary.path)==files && call(service,"cad_read",{{"document_id","wedge"}})==original,"failed tolerance publication preserves all artifacts and source");
  call(service,"cad_apply",{{"document_id","wedge"},{"expected_revision",1},{"operations",Json::array({
    {{"op","set_parameter"},{"name","height"},{"value",40}},{{"op","set_parameter"},{"name","angle_allowance"},{"value",.25}}})}});
  Service reopened(temporary.path);const auto regenerated=call(reopened,"cad_drawing",{{"document_id","wedge"},{"revision",2},{"drawing",saved.at("drawing")}});
  near(regenerated.at("dimensions")[0].at("value_deg"),45);near(regenerated.at("dimensions")[0].at("lower_limit_deg"),44.75);
  require(read_text(path_from_utf8(first.at("recipe_path")))==saved.dump(2)+"\n","regeneration leaves historical recipe intact");
  call(reopened,"cad_job",{{"action","submit"},{"request_id","angular_drawing"},{"tool","cad_drawing"},{"arguments",{{"document_id","wedge"},{"revision",2},{"drawing",recipe}}}});
  Json done;for(int attempt=0;attempt<3000;++attempt) {done=call(reopened,"cad_job",{{"action","get"},{"job_id","angular_drawing"}});if(done.at("state")=="succeeded" || done.at("state")=="failed")break;std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  require(done.at("state")=="succeeded","angular dimensions and tolerances run through isolated jobs");near(done.at("result").at("dimensions")[0].at("value_deg"),45);
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
  invalid=recipe;invalid["layout"]="third_angle";invalid["scale"]=1000;
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
int main(){try{configure_kernel_logging();set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));geometry_tests();paint_order_tests();layout_hatch_tests();angular_tolerance_tests();angular_service_tests();service_tests();std::cout<<"drawing: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<"FAILED: "<<error.what()<<'\n';return 1;}}
