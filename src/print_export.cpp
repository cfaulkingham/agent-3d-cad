#include "agentcad/print_export.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/jobs.hpp"
#include "artifact_internal.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numbers>
#include <sstream>
#include <set>

namespace agentcad {
namespace {
using Vec=std::array<double,3>;
Json object(Json properties,Json required){return {{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};}
std::string xml(const std::string& s){std::string out;for(char c:s){if(c=='&')out+="&amp;";else if(c=='<')out+="&lt;";else if(c=='>')out+="&gt;";else if(c=='\"')out+="&quot;";else out+=c;}return out;}
void little(std::string& out,std::uint32_t value,int bytes){for(int k=0;k<bytes;++k)out.push_back(static_cast<char>((value>>(8*k))&255));}
std::string zip_store(const std::map<std::string,std::string>& entries){
  std::string out,central;
  for(const auto& [name,data]:entries){check_job_cancelled();if(data.size()>32*1024*1024||out.size()+data.size()>128*1024*1024)throw Error("limit_exceeded","3MF archive exceeds its bounded file budget");
    const auto offset=out.size();const auto crc=artifact_detail::crc32(data);
    little(out,0x04034b50,4);little(out,20,2);little(out,0,2);little(out,0,2);little(out,0,2);little(out,33,2);little(out,crc,4);little(out,data.size(),4);little(out,data.size(),4);little(out,name.size(),2);little(out,0,2);out+=name;out+=data;
    little(central,0x02014b50,4);little(central,20,2);little(central,20,2);little(central,0,2);little(central,0,2);little(central,0,2);little(central,33,2);little(central,crc,4);little(central,data.size(),4);little(central,data.size(),4);little(central,name.size(),2);little(central,0,2);little(central,0,2);little(central,0,2);little(central,0,2);little(central,0,4);little(central,offset,4);central+=name;
  }
  const auto offset=out.size();out+=central;little(out,0x06054b50,4);little(out,0,2);little(out,0,2);little(out,entries.size(),2);little(out,entries.size(),2);little(out,central.size(),4);little(out,offset,4);little(out,0,2);return out;
}
Vec rotate(Vec p,Vec deg){for(int k=0;k<3;++k){const auto a=deg[k]*std::numbers::pi/180,c=std::cos(a),s=std::sin(a);const int i=(k+1)%3,j=(k+2)%3;const double u=p[i],v=p[j];p[i]=c*u-s*v;p[j]=s*u+c*v;}return p;}
struct Part{std::string id;Json mesh;Vec rotation{},low{},high{},shift{};int plate=1;};
void orient(Part& p){p.low.fill(std::numeric_limits<double>::infinity());p.high.fill(-std::numeric_limits<double>::infinity());
  const auto lo=p.mesh.at("bounds_mm").at("min").get<Vec>(),hi=p.mesh.at("bounds_mm").at("max").get<Vec>();
  // Rotate the exact bounding box, conservatively enclosing even unsampled extrema.
  for(int mask=0;mask<8;++mask){Vec corner{};for(int k=0;k<3;++k)corner[k]=(mask&(1<<k))?hi[k]:lo[k];corner=rotate(corner,p.rotation);for(int k=0;k<3;++k){p.low[k]=std::min(p.low[k],corner[k]);p.high[k]=std::max(p.high[k],corner[k]);}}
  // Lay the exported triangles on the bed. An analytic curved extremum can
  // lie below every sampled vertex, leaving the actual print mesh floating.
  p.low[2]=std::numeric_limits<double>::infinity();p.high[2]=-std::numeric_limits<double>::infinity();
  for(const auto& vertex:p.mesh.at("positions")){const auto v=rotate(vertex.get<Vec>(),p.rotation);p.low[2]=std::min(p.low[2],v[2]);p.high[2]=std::max(p.high[2],v[2]);}
}
struct Rect{double x,y,w,h;};
void occupy(std::vector<Rect>& free,const Rect& used){std::vector<Rect> next;
  for(const auto& r:free){if(used.x>=r.x+r.w-1e-8||used.x+used.w<=r.x+1e-8||used.y>=r.y+r.h-1e-8||used.y+used.h<=r.y+1e-8){next.push_back(r);continue;}
    if(used.x>r.x+1e-8)next.push_back({r.x,r.y,used.x-r.x,r.h});
    if(used.x+used.w<r.x+r.w-1e-8)next.push_back({used.x+used.w,r.y,r.x+r.w-used.x-used.w,r.h});
    if(used.y>r.y+1e-8)next.push_back({r.x,r.y,r.w,used.y-r.y});
    if(used.y+used.h<r.y+r.h-1e-8)next.push_back({r.x,used.y+used.h,r.w,r.y+r.h-used.y-used.h});
  }
  free.clear();for(std::size_t i=0;i<next.size();++i){bool contained=false;for(std::size_t j=0;j<next.size();++j)if(i!=j&&next[i].x>=next[j].x-1e-8&&next[i].y>=next[j].y-1e-8&&next[i].x+next[i].w<=next[j].x+next[j].w+1e-8&&next[i].y+next[i].h<=next[j].y+next[j].h+1e-8){if(next[i].x!=next[j].x||next[i].y!=next[j].y||next[i].w!=next[j].w||next[i].h!=next[j].h||i>j){contained=true;break;}}if(!contained)free.push_back(next[i]);}
  if(free.size()>16384)throw Error("limit_exceeded","Print packing rectangle budget exceeded");
}
}
Json print_layout_schema(){const Json length={{"type","number"},{"minimum",0},{"maximum",1000000}},angle={{"type","number"},{"minimum",-360},{"maximum",360}};
  return object({{"bed_mm",{{"type","array"},{"items",{{"type","number"},{"exclusiveMinimum",0},{"maximum",1000000}}},{"minItems",2},{"maxItems",2}}},
    {"margin_mm",length},{"spacing_mm",length},{"allow_quarter_turn",{{"type","boolean"}}},
    {"placements",{{"type","array"},{"minItems",1},{"maxItems",4096},{"items",object({{"source_id",{{"type","string"},{"minLength",1},{"maxLength",1024}}},
      {"plate",{{"type","integer"},{"minimum",1},{"maximum",64}}},{"x_mm",length},{"y_mm",length},
      {"rotation_deg",{{"type","array"},{"items",angle},{"minItems",3},{"maxItems",3}}}}, {"source_id","plate","x_mm","y_mm","rotation_deg"})}}}}, {"bed_mm"});}
void validate_print_layout(const Json& layout){
  fields(layout,{"bed_mm"},{"margin_mm","spacing_mm","allow_quarter_turn","placements"});const auto& bed=layout.at("bed_mm");
  if(!bed.is_array()||bed.size()!=2)throw Error("invalid_argument","Print bed_mm needs width and depth");for(const auto& v:bed)if(number(v)<=0||number(v)>1e6)throw Error("invalid_argument","Print bed dimensions must be in (0, 1000000] mm");
  for(const auto* key:{"margin_mm","spacing_mm"})if(layout.contains(key)&&(number(layout.at(key))<0||number(layout.at(key))>1e6))throw Error("invalid_argument","Print margins and spacing must be in [0, 1000000] mm");
  const double margin=layout.value("margin_mm",8.0);if(2*margin>=number(bed[0])||2*margin>=number(bed[1]))throw Error("invalid_argument","Print margin leaves no usable bed");
  if(layout.contains("allow_quarter_turn")&&!layout.at("allow_quarter_turn").is_boolean())throw Error("invalid_argument","allow_quarter_turn must be boolean");
  if(layout.contains("placements")){const auto& placements=layout.at("placements");if(!placements.is_array()||placements.empty()||placements.size()>4096)throw Error("invalid_argument","Print placements needs 1–4096 entries");std::set<std::string> names;
    for(const auto& p:placements){fields(p,{"source_id","plate","x_mm","y_mm","rotation_deg"});const auto id=text_field(p,"source_id");if(id.empty()||id.size()>1024||!names.insert(id).second)throw Error("invalid_argument","Print placement source IDs must be unique");
      if(!p.at("plate").is_number_integer()||p.at("plate")<1||p.at("plate")>64)throw Error("invalid_argument","Print plate must be 1–64");
      for(const auto* key:{"x_mm","y_mm"})if(number(p.at(key))<0||number(p.at(key))>1e6)throw Error("invalid_argument","Print coordinates must be in [0, 1000000] mm");
      const auto& rotation=p.at("rotation_deg");if(!rotation.is_array()||rotation.size()!=3)throw Error("invalid_argument","rotation_deg needs XYZ angles");for(const auto& v:rotation)if(std::abs(number(v))>360)throw Error("invalid_argument","Print rotation must be within ±360 degrees");
    }
  }
}
Json export_3mf(const BuiltModel& model,const std::string& feature,const Json& layout,const Json& identity,const fs::path& stage){
  if(!layout.is_null())validate_print_layout(layout);auto meshes=model.print_meshes(feature);std::vector<Part> parts;
  for(auto& mesh:meshes){Part p;p.id=text_field(mesh,"source_id");p.mesh=std::move(mesh);orient(p);parts.push_back(std::move(p));}
  if(parts.empty())throw Error("export_failed","3MF requires solid meshes");
  if(!layout.is_null()){
    const double w=number(layout.at("bed_mm")[0]),h=number(layout.at("bed_mm")[1]),margin=layout.value("margin_mm",8.0),gap=layout.value("spacing_mm",3.0);
    if(layout.contains("placements")){
      std::map<std::string,Json> supplied;for(const auto& p:layout.at("placements"))supplied.emplace(text_field(p,"source_id"),p);
      if(supplied.size()!=parts.size())throw Error("selection_count_mismatch","Print placements must cover every source solid exactly once");
      for(auto& p:parts){const auto it=supplied.find(p.id);if(it==supplied.end())throw Error("selection_missing","Print placement source is absent",{{"source_id",p.id}});const auto& v=it->second;p.rotation=v.at("rotation_deg").get<Vec>();orient(p);p.plate=v.at("plate");p.shift={number(v.at("x_mm"))-p.low[0],number(v.at("y_mm"))-p.low[1],-p.low[2]};}
    }else{
      std::vector<std::size_t> order;for(std::size_t i=0;i<parts.size();++i)order.push_back(i);
      std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){const auto& x=parts[a];const auto& y=parts[b];return (x.high[0]-x.low[0])*(x.high[1]-x.low[1])>(y.high[0]-y.low[0])*(y.high[1]-y.low[1]);});
      std::vector<std::vector<Rect>> plates;
      for(auto index:order){check_job_cancelled();auto& p=parts[index];const auto dx=p.high[0]-p.low[0],dy=p.high[1]-p.low[1];double score=std::numeric_limits<double>::infinity();int best=-1;Rect used{};bool turn=false;
        for(std::size_t plate=0;plate<=plates.size();++plate){const bool new_plate=plate==plates.size();if(new_plate){if(best>=0)break;if(plates.size()>=64)throw Error("limit_exceeded","Print layout requires more than 64 plates");plates.push_back({{margin,margin,w-2*margin+gap,h-2*margin+gap}});}
          for(const auto& r:plates[plate])for(int rotation=0;rotation<(layout.value("allow_quarter_turn",true)?2:1);++rotation){const double a=(rotation?dy:dx)+gap,b=(rotation?dx:dy)+gap;if(a>r.w+1e-8||b>r.h+1e-8)continue;const double value=std::min(r.w-a,r.h-b);if(value<score){score=value;best=static_cast<int>(plate);used={r.x,r.y,a,b};turn=rotation!=0;}}
          if(best>=0)break;
          if(new_plate)throw Error("invalid_argument","A source solid does not fit the print bed",{{"source_id",p.id},{"size_mm",{dx,dy}}});
        }
        if(turn){p.rotation[2]=90;orient(p);}p.plate=best+1;p.shift={used.x-p.low[0],used.y-p.low[1],-p.low[2]};occupy(plates[best],used);
      }
    }
    // Independent containment and rectangle-clearance check of the final placements.
    for(std::size_t i=0;i<parts.size();++i){const auto& p=parts[i];for(int k=0;k<2;++k)if(p.low[k]+p.shift[k]<margin-1e-7||p.high[k]+p.shift[k]>(k==0?w:h)-margin+1e-7)throw Error("invalid_argument","Print placement exceeds bed margin",{{"source_id",p.id}});
      for(std::size_t j=0;j<i;++j){const auto& q=parts[j];if(p.plate!=q.plate)continue;bool separated=false;for(int k=0;k<2;++k)separated=separated||p.high[k]+p.shift[k]+gap<=q.low[k]+q.shift[k]+1e-7||q.high[k]+q.shift[k]+gap<=p.low[k]+p.shift[k]+1e-7;
        if(!separated)throw Error("invalid_argument","Print placements overlap or violate spacing",{{"source_id",p.id},{"other_source_id",q.id}});}
    }
  }
  Json report={{"schema_version",1},{"source",identity},{"units","mm"},{"mesh_tolerance_mm",.1},{"layout",layout},{"placements",Json::array()},{"plates",Json::array()},
    {"limitations",{"Geometry-only 3MF; no slicer settings, supports or printer approval.","Packing uses conservative bounding rectangles and does not guarantee the fewest plates.","Source IDs with solid-N suffixes are revision-local; verify correspondence before reusing a layout after topology changes."}}};
  std::set<int> plates;for(const auto& p:parts){plates.insert(p.plate);report["placements"].push_back({{"source_id",p.id},{"plate",p.plate},{"x_mm",p.low[0]+p.shift[0]},{"y_mm",p.low[1]+p.shift[1]},{"rotation_deg",p.rotation},
    {"translation_mm",p.shift},{"bounds_mm",{{"min",{p.low[0]+p.shift[0],p.low[1]+p.shift[1],p.low[2]+p.shift[2]}},{"max",{p.high[0]+p.shift[0],p.high[1]+p.shift[1],p.high[2]+p.shift[2]}}}}});}
  std::size_t total_bytes=0;
  for(int plate:plates){check_job_cancelled();std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17)<<"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<model unit=\"millimeter\" xml:lang=\"en-US\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\"><metadata name=\"Title\">"<<xml(text_field(identity,"document_id"))<<"</metadata><metadata name=\"Application\">Agent CAD</metadata><resources>";
    int id=0;Json names=Json::array();for(const auto& p:parts)if(p.plate==plate){names.push_back(p.id);out<<"<object id=\""<<++id<<"\" type=\"model\" name=\""<<xml(p.id)<<"\"><mesh><vertices>";
      for(const auto& v:p.mesh.at("positions")){auto point=rotate(v.get<Vec>(),p.rotation);for(int k=0;k<3;++k)point[k]+=p.shift[k];out<<"<vertex x=\""<<point[0]<<"\" y=\""<<point[1]<<"\" z=\""<<point[2]<<"\"/>";}
      out<<"</vertices><triangles>";for(const auto& t:p.mesh.at("triangles"))out<<"<triangle v1=\""<<t[0]<<"\" v2=\""<<t[1]<<"\" v3=\""<<t[2]<<"\"/>";out<<"</triangles></mesh></object>";
      if(out.tellp()>32*1024*1024)throw Error("limit_exceeded","3MF model XML exceeds 32 MiB");}
    out<<"</resources><build>";for(int i=1;i<=id;++i)out<<"<item objectid=\""<<i<<"\"/>";out<<"</build></model>";
    const auto bytes=zip_store({{"[Content_Types].xml","<?xml version=\"1.0\" encoding=\"UTF-8\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/></Types>"},
      {"_rels/.rels","<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/></Relationships>"},{"3D/3dmodel.model",out.str()}});
    total_bytes+=bytes.size();if(total_bytes>256*1024*1024)throw Error("limit_exceeded","Print package exceeds 256 MiB");const auto name="plate-"+std::to_string(plate)+".3mf";atomic_text(stage/name,bytes,128*1024*1024);
    report["plates"].push_back({{"plate",plate},{"path",name},{"bytes",bytes.size()},{"sha256",sha256(bytes)},{"source_ids",names}});
  }
  atomic_text(stage/"layout.json",report.dump(2));return report;
}
}
