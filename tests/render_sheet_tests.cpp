#include "agentcad/render.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/hash.hpp"
#include <array>
#include <cmath>
#include <iostream>
using namespace agentcad;
namespace {
int checks=0;
void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
template<class F>void fails(F fn,const std::string& code="invalid_argument"){try{fn();}catch(const Error& e){require(e.code==code,"Sheet error code");return;}throw std::runtime_error("Expected sheet error");}
struct Pixels{int lo_x=100000,hi_x=-1,lo_y=100000,hi_y=-1;std::size_t count=0;};
Pixels red_bounds(const RgbaImage& image,const Json& view){
 Pixels out;const int x0=view.at("x"),y0=view.at("y"),w=view.at("width"),h=view.at("height");
 for(int y=y0;y<y0+h;++y)for(int x=x0;x<x0+w;++x){const auto p=(static_cast<std::size_t>(y)*image.width+x)*4;
  if(image.pixels[p]==255&&image.pixels[p+1]==0&&image.pixels[p+2]==0){out.lo_x=std::min(out.lo_x,x-x0);out.hi_x=std::max(out.hi_x,x-x0);out.lo_y=std::min(out.lo_y,y-y0);out.hi_y=std::max(out.hi_y,y-y0);++out.count;}}
 return out;
}
}
int main(){const auto root=temporary_directory(fs::temp_directory_path());try{
 const Json model={{"schema_version",1},{"units","mm"},{"parameters",Json::object()},
  {"features",Json::array({{{"id","box"},{"type","box"},{"size",{20,10,5}}}})},{"output","box"}};
 BuiltModel box(model);const Json payload={{"mesh",box.mesh()},{"topology",box.topology()},{"summary",box.summary()}};
 const auto immutable=payload.dump();
 Json opts={{"view","sheet"},{"width",801},{"height",601},
  {"render",{{"lighting",{{"enabled",false}}},{"background",{{"color",{1,1,1}}}},{"grid",false},{"axes",false},{"edges",false},{"floor",{{"enabled",false}}}}},
  {"appearance",{{"default_color",{1,0,0}},{"parts",Json::array()}}}};
 const auto result=render_png(payload,opts,root/"sheet.png");const auto bytes=read_text(root/"sheet.png",16*1024*1024);const auto image=decode_png(bytes);
 require(image.width==801&&image.height==601&&sha256(bytes)==text_field(result,"sha256"),"Sheet preserves requested odd output dimensions and exact PNG hash");
 const auto& sheet=result.at("sheet");const double scale=sheet.at("pixels_per_mm");
 const std::array<std::string,4> names={"front","top","right","iso"};
 const std::array<double,4> widths={20,20,10,30/std::sqrt(2.)},heights={5,10,5,30/std::sqrt(6.)+5*std::sqrt(2./3)};
 std::array<Pixels,4> projected;
 for(unsigned i=0;i<4;++i){const auto& view=sheet.at("views")[i];require(view.at("name")==names[i],"Fixed named view ordering");projected[i]=red_bounds(image,view);const auto& p=projected[i];
  require(std::abs(p.hi_x-p.lo_x+1-widths[i]*scale)<=2&&std::abs(p.hi_y-p.lo_y+1-heights[i]*scale)<=2,"Independent box projection dimensions agree with a single pixels/mm scale including isometric foreshortening");
  require(p.lo_x>=6&&p.lo_y>=6&&p.hi_x<view.at("width").get<int>()-6&&p.hi_y<view.at("height").get<int>()-6,"All fitted views retain margins without clipping");
  unsigned ink=0;const int x=view.at("x"),y=view.at("y");for(int yy=y-14;yy<y-7;++yy)for(int xx=x+8;xx<x+38;++xx){const auto k=(static_cast<std::size_t>(yy)*image.width+xx)*4;ink+=image.pixels[k]==45;}require(ink>10,"Every tile contains a visible label outside the geometry viewport");
 }
 // Odd image widths make the right column one pixel wider. Centering changes
 // raster quantization by at most one pixel even with an identical scale.
 require(std::abs((projected[0].hi_x-projected[0].lo_x)-(projected[1].hi_x-projected[1].lo_x))<=1,"Front and top share X scale within one-pixel raster quantization");
 require(render_rgba(payload,opts).pixels==image.pixels,"PNG and RGBA sheet paths agree deterministically");
 auto changed=opts;changed["presentation"]={{"clip",{{"normal",{1,0,0}},{"offset_mm",10},{"keep","positive"}}}};
 require(render_rgba(payload,changed).pixels!=image.pixels,"Sheet respects source clipping");
 auto external=Json{{"summary",{{"bounds",box.summary().at("bounds_mm")}}},{"artifact_geometry",{{"positions",payload.at("mesh").at("positions")},{"triangles",payload.at("mesh").at("triangles")},{"triangle_groups",Json::array()},{"groups",Json::array({{{"id","body"}}})},{"polylines",Json::array()}}}};
 for(std::size_t i=0;i<payload.at("mesh").at("triangles").size();++i)external["artifact_geometry"]["triangle_groups"].push_back("body");
 require(render_rgba(external,opts).pixels==image.pixels,"External artifact sheets use the same projections and shared scale");
 changed=opts;changed["hidden_part_ids"]={"body"};const auto hidden=render_rgba(external,changed);for(const auto& view:sheet.at("views"))require(red_bounds(hidden,view).count==0,"Hidden geometry stays hidden in all four views");
 changed=opts;changed["presentation"]={{"explode",{{"distance_mm",100},{"directions",Json::array({{{"part_id","body"},{"direction",{1,0,0}}}})}}}};
 require(red_bounds(render_rgba(external,changed),sheet.at("views")[0]).count>0,"Exploded geometry is fitted back inside the sheet");
 for(const auto& bad:Json::array({Json{{"view","unknown"}},Json{{"view","sheet"},{"width",127}},Json{{"view","sheet"},{"camera",Json::object()}},Json{{"view","sheet"},{"render",{{"projection","perspective"}}}}}))fails([&]{render_rgba(payload,bad);});
 int cancellation=0;fails([&]{render_png(payload,opts,root/"sheet.png",[&]{if(++cancellation==100)throw Error("job_cancelled","cancel");});},"job_cancelled");
 require(read_text(root/"sheet.png",16*1024*1024)==bytes,"Cancelled sheet preserves previously published bytes");
 // Each view is below the work limit, but the combined overdraw exceeds it.
 // This catches accidentally resetting the budget for every tile.
 Json crowded={{"summary",{{"bounds",{{"min",{-10,0,-10}},{"max",{10,0,10}}}}}},
  {"artifact_geometry",{{"positions",Json::array({{-10,0,-10},{10,0,-10},{10,0,10},{-10,0,10}})},
    {"triangles",Json::array()},{"triangle_groups",Json::array()},
    {"groups",Json::array({{{"id","plane"}}})},{"polylines",Json::array()}}}};
 for(int n=0;n<2000;++n)for(const auto& triangle:Json::array({{0,1,2},{0,2,3}})){
  crowded["artifact_geometry"]["triangles"].push_back(triangle);crowded["artifact_geometry"]["triangle_groups"].push_back("plane");
 }
 auto workload=opts;workload["width"]=512;workload["height"]=512;
 fails([&]{render_png(crowded,workload,root/"over-budget.png");},"limit_exceeded");
 require(!fs::exists(root/"over-budget.png"),"Aggregate sheet budget fails before publication");
 workload["view"]="single";workload["width"]=256;workload["height"]=236;
 workload["camera"]={{"yaw",0},{"pitch",0},{"zoom",220./(236*.68)},{"pan",{0,0}}};
 require(render_rgba(crowded,workload).width==256,"The largest individual sheet view stays within the work budget");
 require(payload.dump()==immutable,"Rendering all four views never alters geometry payload");
 fs::remove_all(root);std::cout<<"render sheet: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){fs::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}}
