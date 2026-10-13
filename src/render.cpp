#include "agentcad/render.hpp"
#include "agentcad/render_settings.hpp"
#include "agentcad/image.hpp"
#include "agentcad/hash.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <vector>

namespace agentcad {
namespace {
using V=std::array<double,3>;
using C=std::array<double,4>;
constexpr std::size_t geometry_limit=200000,work_limit=200000000,texture_limit=16*1024*1024;
constexpr double pi=3.14159265358979323846;
V add(V a,V b){for(int i=0;i<3;++i)a[i]+=b[i];return a;}
V sub(V a,V b){for(int i=0;i<3;++i)a[i]-=b[i];return a;}
V mul(V a,double n){for(auto& v:a)v*=n;return a;}
double dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
V cross(V a,V b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
double length(V a){return std::hypot(a[0],a[1],a[2]);}
V unit(V a){const auto n=length(a);return n>1e-20?mul(a,1/n):V{0,0,1};}
V vec(const Json& j){if(!j.is_array()||j.size()!=3)throw Error("invalid_argument","Render vector requires three finite values");return {number(j[0]),number(j[1]),number(j[2])};}
C color(const Json& j){if(!j.is_array()||(j.size()!=3&&j.size()!=4))throw Error("invalid_argument","Render color requires RGB or RGBA");C c{0,0,0,1};for(std::size_t i=0;i<j.size();++i){c[i]=number(j[i]);if(c[i]<0||c[i]>1)throw Error("invalid_argument","Render color channels lie in [0,1]");}return c;}
std::size_t index(const Json& j,std::size_t n){if(!j.is_number_integer())throw Error("invalid_argument","Render triangle index must be an integer");const auto i=j.get<std::int64_t>();if(i<0||static_cast<std::uint64_t>(i)>=n)throw Error("invalid_argument","Render triangle index is outside its mesh");return static_cast<std::size_t>(i);}
struct Bounds {
 V lo{INFINITY,INFINITY,INFINITY},hi{-INFINITY,-INFINITY,-INFINITY};
 void include(V p){for(int k=0;k<3;++k){lo[k]=std::min(lo[k],p[k]);hi[k]=std::max(hi[k],p[k]);}}
 bool valid()const{return std::isfinite(lo[0]);}
 V center()const{return mul(add(lo,hi),.5);}
};
struct Vertex {V p{},n{0,0,1};C c{1,1,1,1};std::array<double,2> uv{};};
Vertex mix(const Vertex& a,const Vertex& b,double t){Vertex out;out.p=add(mul(a.p,1-t),mul(b.p,t));out.n=add(mul(a.n,1-t),mul(b.n,t));for(int i=0;i<4;++i)out.c[i]=a.c[i]*(1-t)+b.c[i]*t;for(int i=0;i<2;++i)out.uv[i]=a.uv[i]*(1-t)+b.uv[i]*t;return out;}
struct Texture {std::vector<RgbaImage> levels;int wrap_s=10497,wrap_t=10497,mag=9729,min=9987;};
struct Material {C base{.66,.75,.8,1};V emissive{};double rough=.5,metal=0,opacity=1,cutoff=.5;bool doubled=true;std::string alpha="BLEND";int texture=-1;};
struct Triangle {std::array<Vertex,3> v;std::size_t material=0;};
struct Curve {std::vector<V> points;C color{.12,.15,.18,1};};
struct Scene {std::vector<Triangle> triangles;std::vector<Curve> curves;std::vector<Json> overlays;std::vector<Material> materials;std::vector<Texture> textures;V center{};double span=1;Bounds displayed;};
struct Budget {
 std::size_t used=0,ticks=0;const std::function<void()>& cancel;
 void check(){if(cancel)cancel();}
 void charge(std::size_t amount){if(amount>work_limit-used)throw Error("limit_exceeded","Native render exceeds 200 million pixel operations");used+=amount;check();}
 void step(){if((++ticks&4095)==0)check();}
};
std::vector<Vertex> clip_polygon(std::vector<Vertex> v,V normal,double offset,double sign){
 if(v.empty())return v;std::vector<Vertex> out;auto a=v.back();auto da=sign*(dot(normal,a.p)-offset);
 for(const auto& b:v){const auto db=sign*(dot(normal,b.p)-offset);if((da>=0)!=(db>=0))out.push_back(mix(a,b,da/(da-db)));if(db>=0)out.push_back(b);a=b;da=db;}return out;
}
std::vector<V> clip_loop(const std::vector<V>& v,V n,double off,double sign){std::vector<Vertex> in;for(auto p:v)in.push_back({p});const auto out=clip_polygon(std::move(in),n,off,sign);std::vector<V> result;for(const auto& p:out)result.push_back(p.p);return result;}
bool clip_segment(V& a,V& b,V n,double offset,double sign){const auto da=sign*(dot(n,a)-offset),db=sign*(dot(n,b)-offset);if(da<0&&db<0)return false;if((da>=0)!=(db>=0)){const auto p=add(a,mul(sub(b,a),da/(da-db)));if(da<0)a=p;else b=p;}return true;}
void finish(Material& m,const std::string& s){m.rough=s=="matte"?.9:s=="satin"?.5:s=="glossy"?.15:.24;m.metal=s=="metal"?1:0;}
Texture texture(const Json& j,std::size_t& pixels,Budget& budget){
 fields(j,{"image_data_uri","sha256","mime_type","texcoord","sampler"});
 if(j.at("mime_type")!="image/png"||j.at("texcoord")!=0)throw Error("unsupported_format","Native rendering accepts embedded PNG base-color textures in TEXCOORD_0");
 const auto uri=text_field(j,"image_data_uri"),prefix=std::string("data:image/png;base64,");if(!uri.starts_with(prefix))throw Error("unsupported_format","Native render textures must be bounded embedded PNG data");
 const auto bytes=image_unbase64(uri.substr(prefix.size()),32*1024*1024);if(sha256(bytes)!=text_field(j,"sha256"))throw Error("invalid_argument","Embedded render image hash differs from its bytes");budget.check();
 Texture t;t.levels.push_back(decode_png(bytes,texture_limit-pixels));pixels+=static_cast<std::size_t>(t.levels[0].width)*t.levels[0].height;
 const auto& s=j.at("sampler");fields(s,{"mag_filter","min_filter","wrap_s","wrap_t"});t.mag=s.at("mag_filter").get<int>();t.min=s.at("min_filter").get<int>();t.wrap_s=s.at("wrap_s").get<int>();t.wrap_t=s.at("wrap_t").get<int>();
 if((t.mag!=9728&&t.mag!=9729)||!std::set<int>{9728,9729,9984,9985,9986,9987}.contains(t.min)||!std::set<int>{33071,33648,10497}.contains(t.wrap_s)||!std::set<int>{33071,33648,10497}.contains(t.wrap_t))throw Error("invalid_argument","Unsupported embedded texture sampler");
 while(t.levels.back().width>1||t.levels.back().height>1){budget.check();const auto& p=t.levels.back();RgbaImage next;next.width=std::max(1u,p.width/2);next.height=std::max(1u,p.height/2);const auto count=static_cast<std::size_t>(next.width)*next.height;if(count>texture_limit-pixels)throw Error("limit_exceeded","Native texture mipmaps exceed 16 million pixels");pixels+=count;next.pixels.resize(count*4);
  for(std::uint32_t y=0;y<next.height;++y)for(std::uint32_t x=0;x<next.width;++x){budget.step();for(int c=0;c<4;++c){unsigned value=0,counted=0;for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx){const auto sx=2*x+dx,sy=2*y+dy;if(sx<p.width&&sy<p.height){value+=p.pixels[(static_cast<std::size_t>(sy)*p.width+sx)*4+c];++counted;}}next.pixels[(static_cast<std::size_t>(y)*next.width+x)*4+c]=static_cast<unsigned char>((value+counted/2)/counted);}}
  t.levels.push_back(std::move(next));}
 return t;
}
Scene scene(const Json& payload,const Json& settings,const Json& render,Budget& budget){
 Scene out;const bool external=payload.contains("artifact_geometry");const auto& g=payload.at(external?"artifact_geometry":"mesh");const auto& positions=g.at("positions");const auto& triangles=g.at("triangles");
 if(!positions.is_array()||positions.size()>geometry_limit||!triangles.is_array()||triangles.size()>geometry_limit)throw Error("limit_exceeded","Render mesh exceeds 200000 vertices or triangles");
 const Json empty_object=Json::object(),empty_array=Json::array();std::vector<V> points;Bounds source;for(const auto& p:positions){budget.step();const auto v=vec(p);for(auto x:v)if(std::abs(x)>1e9)throw Error("limit_exceeded","Render coordinates exceed one billion millimetres");points.push_back(v);source.include(v);}
 const auto& rawcurves=g.contains(external?"polylines":"edges")?g.at(external?"polylines":"edges"):empty_array;std::size_t point_count=0;
 if(!rawcurves.is_array()||rawcurves.size()>10000)throw Error("limit_exceeded","Native render exceeds 10000 curves");
 for(const auto& curve:rawcurves){for(const auto& p:curve.at("points")){if(++point_count>200000)throw Error("limit_exceeded","Native render exceeds 200000 curve points");source.include(vec(p));}if(curve.contains("hatch_loops"))for(const auto& loop:curve.at("hatch_loops"))for(const auto& p:loop.is_object()?loop.at("points"):loop){if(++point_count>200000)throw Error("limit_exceeded","Native render hatch point budget exceeded");source.include(vec(p));}}
 const auto& summary=payload.at("summary");const auto bkey=external?"bounds":"bounds_mm";if(summary.contains(bkey)&&!summary.at(bkey).is_null()){const auto& b=summary.at(bkey);const auto lo=vec(b.at("min")),hi=vec(b.at("max"));for(int k=0;k<3;++k)if(lo[k]>hi[k]||(source.valid()&&(source.lo[k]<lo[k]-1e-5||source.hi[k]>hi[k]+1e-5)))throw Error("invalid_argument","Render source bounds do not enclose its geometry");source.lo=lo;source.hi=hi;}
 if(!source.valid())throw Error("export_failed","Native rendering requires visible geometry");
 // Artifact poses retain the captured camera frame; the display summary above
 // validates current geometry, while captured rest bounds define normalization.
 if(external&&payload.contains("artifact")&&payload.at("artifact").contains("summary")){const auto& captured=payload.at("artifact").at("summary");if(captured.contains("bounds")&&!captured.at("bounds").is_null()){source.lo=vec(captured.at("bounds").at("min"));source.hi=vec(captured.at("bounds").at("max"));for(int k=0;k<3;++k)if(source.lo[k]>source.hi[k])throw Error("invalid_argument","Invalid captured artifact bounds");}}
 out.center=source.center();out.span=std::max({1e-9,source.hi[0]-source.lo[0],source.hi[1]-source.lo[1],source.hi[2]-source.lo[2]});
 auto normalize=[&](V p){auto value=mul(sub(p,out.center),1/out.span);for(auto x:value)if(std::abs(x)>1e9)throw Error("limit_exceeded","Displayed pose exceeds native normalized precision bounds");return value;};for(auto& p:points)p=normalize(p);
 std::map<std::string,std::string> owners;std::map<std::string,Bounds> part_bounds;
 if(external){if(!g.at("groups").is_array()||g.at("groups").size()>10000)throw Error("limit_exceeded","Render exceeds 10000 artifact groups");for(const auto& group:g.at("groups")){const auto id=text_field(group,"id");if(!owners.emplace(id,id).second)throw Error("invalid_argument","Duplicate artifact group");}}
 else {for(const auto& face:payload.at("topology").at("faces"))if(!owners.emplace(text_field(face,"id"),face.value("part_id",std::string{})).second)throw Error("invalid_argument","Duplicate native face");}
 const auto& triowners=g.at(external?"triangle_groups":"triangle_faces");if(!triowners.is_array()||triowners.size()!=triangles.size())throw Error("invalid_argument","Render triangle ownership is incomplete");
 std::vector<std::array<std::size_t,3>> indices;std::vector<std::string> leaf;std::vector<V> normals(points.size());
 for(std::size_t i=0;i<triangles.size();++i){budget.step();const auto& t=triangles[i];if(!t.is_array()||t.size()!=3)throw Error("invalid_argument","Render triangle needs three vertices");const auto id=triowners.at(i).get<std::string>();if(!owners.contains(id))throw Error("invalid_argument","Render triangle refers to a missing face or group");leaf.push_back(owners.at(id));std::array<std::size_t,3> ix;for(int k=0;k<3;++k){ix[k]=index(t[k],points.size());part_bounds[leaf.back()].include(points[ix[k]]);}indices.push_back(ix);const auto n=cross(sub(points[ix[1]],points[ix[0]]),sub(points[ix[2]],points[ix[0]]));for(auto p:ix)normals[p]=add(normals[p],n);}
 for(const auto& curve:rawcurves){const auto id=external?text_field(curve,"id"):curve.value("part_id",std::string{});for(const auto& p:curve.at("points"))part_bounds[id].include(normalize(vec(p)));}
 std::set<std::string> hidden;if(settings.contains("hidden_part_ids")){const auto& a=settings.at("hidden_part_ids");if(!a.is_array()||a.size()>1024)throw Error("invalid_argument","Invalid render hidden leaves");for(const auto& v:a){const auto id=v.get<std::string>();if(!part_bounds.contains(id)||!hidden.insert(id).second)throw Error("invalid_argument","Hidden render leaf is missing or duplicated");}}
 Json presentation=settings.value("presentation",Json::object());fields(presentation,{}, {"clip","explode"});std::map<std::string,V> shifts;bool clipping=false;V clipnormal{};double clipoffset=0,clipsign=1;
 if(presentation.contains("clip")&&!presentation.at("clip").is_null()){const auto& c=presentation.at("clip");fields(c,{"normal","offset_mm","keep"});clipnormal=vec(c.at("normal"));if(std::abs(length(clipnormal)-1)>1e-6)throw Error("invalid_argument","Render clip normal must be unit length");clipoffset=(number(c.at("offset_mm"))-dot(clipnormal,out.center))/out.span;const auto keep=text_field(c,"keep");if(keep!="positive"&&keep!="negative")throw Error("invalid_argument","Invalid render clipping half-space");clipsign=keep=="positive"?1:-1;clipping=true;}
 if(presentation.contains("explode")){const auto& e=presentation.at("explode");fields(e,{"distance_mm","directions"});const auto d=number(e.at("distance_mm"));if(d<0||d>1e6||!e.at("directions").is_array()||e.at("directions").size()>1024)throw Error("invalid_argument","Invalid render explosion");std::map<std::string,V> directions;
  for(const auto& v:e.at("directions")){fields(v,{"part_id","direction"});const auto id=text_field(v,"part_id");const auto dir=vec(v.at("direction"));if(std::abs(length(dir)-1)>1e-6||!part_bounds.contains(id)||!directions.emplace(id,dir).second)throw Error("invalid_argument","Explosion needs unique known leaves and unit directions");}
  std::size_t i=0;for(const auto& [id,b]:part_bounds){auto dir=directions.contains(id)?directions.at(id):b.center();if(length(dir)<1e-12){dir={0,0,0};dir[i%3]=i%2?-1:1;}shifts[id]=mul(unit(dir),d/out.span);++i;}}
 const auto surfaces=render.at("surfaces");const auto global_opacity=number(surfaces.at("opacity"));const auto appearance=settings.value("appearance",Json{{"default_color",{.66,.75,.8}},{"parts",Json::array()}});fields(appearance,{"default_color","parts"},{"material"});const auto defaultcolor=color(appearance.at("default_color"));if(!appearance.at("parts").is_array()||appearance.at("parts").size()>1024)throw Error("invalid_argument","Invalid native appearance");
 if(appearance.contains("material"))normalize_render_material(appearance.at("material"));std::map<std::string,Json> overrides;for(const auto& p:appearance.at("parts")){fields(p,{"part_id","color"},{"material"});const auto id=text_field(p,"part_id");color(p.at("color"));if(p.contains("material"))normalize_render_material(p.at("material"));if(!part_bounds.contains(id)||!overrides.emplace(id,p).second)throw Error("invalid_argument","Render appearance requires unique known leaves");}
 std::map<std::string,std::size_t> materials;std::map<std::string,const Json*> groups;std::size_t texture_pixels=0;std::map<std::string,int> texture_ids;std::map<std::string,std::string> texture_sources;
 auto material_for=[&](const std::string& id)->std::size_t{
  if(materials.contains(id))return materials.at(id);Material m;
  if(external){const auto& group=*groups.at(id);if(group.contains("material")){const auto& a=group.at("material");m.base=color(a.value("base_color",Json{1,1,1,1}));m.rough=number(a.value("roughness",Json(1)));m.metal=number(a.value("metallic",Json(1)));m.emissive=vec(a.value("emissive",Json{0,0,0}));m.alpha=a.value("alpha_mode",std::string("OPAQUE"));m.cutoff=number(a.value("alpha_cutoff",Json(.5)));m.doubled=a.value("double_sided",false);m.opacity=m.alpha=="OPAQUE"?1:m.base[3];if(!std::set<std::string>{"OPAQUE","MASK","BLEND"}.contains(m.alpha)||m.rough<0||m.rough>1||m.metal<0||m.metal>1||m.cutoff<0||m.cutoff>1)throw Error("invalid_argument","Invalid authored render material");if(a.contains("base_color_texture")){const auto& tj=a.at("base_color_texture");const auto hash=text_field(tj,"sha256");const auto sampler_key=hash+tj.at("sampler").dump();if(texture_sources.contains(sampler_key)&&texture_sources.at(sampler_key)!=text_field(tj,"image_data_uri"))throw Error("invalid_argument","Texture identity has differing embedded bytes");texture_sources[sampler_key]=text_field(tj,"image_data_uri");if(!texture_ids.contains(sampler_key)){texture_ids[sampler_key]=static_cast<int>(out.textures.size());out.textures.push_back(texture(tj,texture_pixels,budget));}m.texture=texture_ids.at(sampler_key);}}else m.base=defaultcolor;if(overrides.contains(id))m.base=color(overrides.at(id).at("color"));}
  else {const auto style=overrides.contains(id)?overrides.at(id):Json::object();m.base=style.contains("color")?color(style.at("color")):defaultcolor;const auto a=style.value("material",appearance.value("material",Json::object()));const auto normalized=normalize_render_material(a);m.opacity=number(normalized.at("opacity"));finish(m,a.value("finish",appearance.value("material",Json::object()).value("finish",surfaces.at("finish").get<std::string>())));}
  m.opacity*=global_opacity;const auto ix=out.materials.size();out.materials.push_back(m);materials[id]=ix;return ix;
 };
 if(external)for(const auto& group:g.at("groups")){const auto id=text_field(group,"id");groups[id]=&group;material_for(id);}
 // Native indexed positions are shared within analytic faces, so the accumulated
 // normals preserve smooth tessellated surfaces without smoothing across faces.
 for(std::size_t i=0;i<indices.size();++i){budget.step();const auto& id=leaf[i];if(hidden.contains(id))continue;Triangle t;t.material=material_for(id);const auto& ix=indices[i];const auto& group=external?*groups.at(id):empty_object;const auto start=external?group.value("vertex_start",std::size_t(0)):0;
  for(int k=0;k<3;++k){const auto vi=ix[k];auto& v=t.v[k];v.p=add(points[vi],shifts.contains(id)?shifts.at(id):V{});v.n=unit(normals[vi]);if(external){if(group.contains("normals_mm")){const auto& values=group.at("normals_mm");if(vi<start||vi-start>=values.size())throw Error("invalid_argument","Artifact normals do not cover their group");v.n=unit(vec(values.at(vi-start)));}if(group.contains("vertex_colors")){const auto& values=group.at("vertex_colors");if(vi<start||vi-start>=values.size())throw Error("invalid_argument","Artifact colors do not cover their group");v.c=color(values.at(vi-start));}if(group.contains("texture_coordinates")){const auto& values=group.at("texture_coordinates");if(vi<start||vi-start>=values.size())throw Error("invalid_argument","Artifact UV coordinates do not cover their group");const auto& uv=values.at(vi-start);if(!uv.is_array()||uv.size()!=2)throw Error("invalid_argument","Texture coordinates require two values");v.uv={number(uv[0]),number(uv[1])};}else if(out.materials[t.material].texture>=0)throw Error("invalid_argument","Authored texture requires TEXCOORD_0");}}
  std::vector<Vertex> polygon(t.v.begin(),t.v.end());if(clipping)polygon=clip_polygon(std::move(polygon),clipnormal,clipoffset,clipsign);
  for(std::size_t j=1;j+1<polygon.size();++j){Triangle cut{{polygon[0],polygon[j],polygon[j+1]},t.material};for(const auto& v:cut.v)out.displayed.include(v.p);out.triangles.push_back(cut);}}
 for(const auto& raw:rawcurves){budget.check();const auto id=external?text_field(raw,"id"):raw.value("part_id",std::string{});if(hidden.contains(id))continue;const auto shift=shifts.contains(id)?shifts.at(id):V{};
  if(external&&(raw.contains("text")||raw.value("fill",false))){Json item=raw;for(auto& p:item["points"]){const auto normalized=add(normalize(vec(p)),shift);p=normalized;out.displayed.include(normalized);}if(item.contains("hatch_loops"))for(auto& loop:item["hatch_loops"]){auto& values=loop.is_object()?loop["points"]:loop;std::vector<V> pts;for(const auto& p:values)pts.push_back(add(normalize(vec(p)),shift));if(clipping)pts=clip_loop(pts,clipnormal,clipoffset,clipsign);values=pts;for(const auto& p:pts)out.displayed.include(p);}if(item.contains("text")){const auto p=vec(item["points"][0]);if(clipping&&clipsign*(dot(clipnormal,p)-clipoffset)<0)continue;if(item.contains("height_mm"))item["height_mm"]=number(item["height_mm"])/out.span;}out.overlays.push_back(std::move(item));if(raw.contains("text"))continue;}
  Curve c;if(raw.contains("color"))c.color=color(raw.at("color"));const auto& ps=raw.at("points");for(std::size_t i=1;i<ps.size();++i){V a=add(normalize(vec(ps[i-1])),shift),b=add(normalize(vec(ps[i])),shift);if(clipping&&!clip_segment(a,b,clipnormal,clipoffset,clipsign))continue;c.points.push_back(a);c.points.push_back(b);out.displayed.include(a);out.displayed.include(b);}out.curves.push_back(std::move(c));}
 // Empty views after a deliberate clip/hide are valid background-only images.
 return out;
}
struct Projected {double x,y,z,inv;};
struct Camera {
 V b[3];double scale,px,py,d,extent;bool perspective;std::uint32_t w,h;
 Projected project(V p)const{const auto z=dot(b[2],p),factor=perspective?d/(d+z):1.;return {w*.5+dot(b[0],p)*scale*factor+px,h*.5-dot(b[1],p)*scale*factor+py,z,perspective?1/(d+z):1};}
 std::pair<V,V> ray(double x,double y)const{const auto u=(x-w*.5-px)/scale,v=(h*.5+py-y)/scale;if(perspective)return {mul(b[2],-d),add(b[2],add(mul(b[0],u/d),mul(b[1],v/d)))};return {add(add(mul(b[0],u),mul(b[1],v)),mul(b[2],-extent)),b[2]};}
 V view(V p)const{return {dot(b[0],p),dot(b[1],p),dot(b[2],p)};}
};
Camera camera(const Json& settings,const Json& render,const Scene& s,std::uint32_t w,std::uint32_t h){
 const auto c=settings.value("camera",Json{{"yaw",-.65},{"pitch",.6},{"zoom",1},{"pan",{0,0}}});fields(c,{"yaw","pitch","zoom","pan"});auto yaw=number(c.at("yaw"));yaw=std::atan2(std::sin(yaw),std::cos(yaw));const auto pitch=std::clamp(number(c.at("pitch")),-pi/2,pi/2),zoom=std::clamp(number(c.at("zoom")),.05,50.);if(!c.at("pan").is_array()||c.at("pan").size()!=2)throw Error("invalid_argument","Render camera pan needs two values");const auto cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);Camera out{{{cy,-sy,0},{sy*sp,cy*sp,cp},{sy*cp,cy*cp,-sp}},std::min(w,h)*.68*zoom,std::clamp(number(c.at("pan")[0]),-20.,20.)*std::min(w,h),std::clamp(number(c.at("pan")[1]),-20.,20.)*std::min(w,h),0,2,render.at("projection")=="perspective",w,h};
 if(s.displayed.valid()){V mx;for(int k=0;k<3;++k)mx[k]=std::max(std::abs(s.displayed.lo[k]),std::abs(s.displayed.hi[k]));out.extent=std::max(2.,1+length(mx));}if(out.extent>1e9)throw Error("limit_exceeded","Displayed render pose exceeds normalized camera precision bounds");out.d=std::max(out.extent+.25,std::max(3.,out.extent+1)*number(render.at("lens_mm"))/50);return out;
}
int wrap_index(int n,int size,int wrap){if(wrap==33071)return std::clamp(n,0,size-1);if(wrap==10497){n%=size;return n<0?n+size:n;}n%=2*size;if(n<0)n+=2*size;return n<size?n:2*size-1-n;}
C texel(const RgbaImage& im,int x,int y,const Texture& t){x=wrap_index(x,im.width,t.wrap_s);y=wrap_index(y,im.height,t.wrap_t);C c;for(int k=0;k<4;++k)c[k]=im.pixels[(static_cast<std::size_t>(y)*im.width+x)*4+k]/255.;return c;}
C sample_level(const Texture& t,std::size_t level,double u,double v,bool linear){const auto& im=t.levels[level];auto coordinate=[](double value,int wrap){if(wrap==33071)return std::clamp(value,0.,1.);const auto period=wrap==33648?2.:1.;value=std::fmod(value,period);return value<0?value+period:value;};const auto x=coordinate(u,t.wrap_s)*im.width-.5,y=coordinate(v,t.wrap_t)*im.height-.5;if(!linear)return texel(im,static_cast<int>(std::floor(x+.5)),static_cast<int>(std::floor(y+.5)),t);const auto ix=static_cast<int>(std::floor(x)),iy=static_cast<int>(std::floor(y));const auto fx=x-ix,fy=y-iy;const auto a=texel(im,ix,iy,t),b=texel(im,ix+1,iy,t),c=texel(im,ix,iy+1,t),d=texel(im,ix+1,iy+1,t);C out;for(int k=0;k<4;++k)out[k]=(a[k]*(1-fx)+b[k]*fx)*(1-fy)+(c[k]*(1-fx)+d[k]*fx)*fy;return out;}
C sample(const Texture& t,double u,double v,double lod){if(lod<=0)return sample_level(t,0,u,v,t.mag==9729);if(t.min==9728||t.min==9729)return sample_level(t,0,u,v,t.min==9729);const auto l=std::clamp(lod,0.,static_cast<double>(t.levels.size()-1));const bool linear=t.min==9985||t.min==9987;if(t.min==9984||t.min==9985)return sample_level(t,static_cast<std::size_t>(std::floor(l+.5)),u,v,linear);const auto low=static_cast<std::size_t>(std::floor(l)),high=std::min(low+1,t.levels.size()-1);const auto a=sample_level(t,low,u,v,linear),b=sample_level(t,high,u,v,linear);C out;for(int k=0;k<4;++k)out[k]=a[k]*(1-(l-low))+b[k]*(l-low);return out;}
struct Raster {
 RgbaImage image;std::vector<double> depth;Budget& budget;const Scene& scene;const Camera& cam;const Json& settings;V key,worldkey,fill=unit(V{.55,.3,-.4});bool light;double exposure,fill_strength;
 Raster(std::uint32_t w,std::uint32_t h,Budget& b,const Scene& s,const Camera& c,const Json& r):image{w,h,std::vector<unsigned char>(static_cast<std::size_t>(w)*h*4)},depth(static_cast<std::size_t>(w)*h,INFINITY),budget(b),scene(s),cam(c),settings(r){const auto& l=r.at("lighting");const auto angle=number(l.at("rotation_deg"))*pi/180;worldkey={-.45*std::cos(angle)+.55*std::sin(angle),-.45*std::sin(angle)-.55*std::cos(angle),1};key=unit(cam.view(worldkey));light=l.at("enabled");exposure=number(l.at("exposure"));fill_strength=number(l.at("fill"));const auto bg=color(r.at("background").at("color"));for(std::size_t i=0;i<depth.size();++i){budget.step();for(int k=0;k<3;++k)image.pixels[i*4+k]=static_cast<unsigned char>(std::lround(bg[k]*255));image.pixels[i*4+3]=255;}}
 void paint(std::size_t i,C c){const auto alpha=std::clamp(c[3],0.,1.);for(int k=0;k<3;++k)image.pixels[i*4+k]=static_cast<unsigned char>(std::lround(std::clamp(c[k],0.,1.)*255*alpha+image.pixels[i*4+k]*(1-alpha)));}
 C shade(const Vertex& v,const Material& m,bool back,double lod){const auto tx=m.texture<0?C{1,1,1,1}:sample(scene.textures[m.texture],v.uv[0],v.uv[1],lod);C out;for(int k=0;k<3;++k)out[k]=m.base[k]*v.c[k]*tx[k];out[3]=m.opacity*v.c[3]*(m.alpha=="OPAQUE"?1:tx[3]);if(m.alpha=="MASK"){if(out[3]<m.cutoff)return {0,0,0,0};out[3]=1;}
  if(light){auto n=unit(cam.view(v.n));if(back)n=mul(n,-1);const auto diffuse=std::max(dot(n,key),0.),f=std::max(dot(n,fill),0.),rim=std::pow(1-std::abs(n[2]),3);const auto reflect=add(mul(key,-1),mul(n,2*dot(key,n)));const auto spec=std::pow(std::max(-reflect[2],0.),std::max(2.,128*(1-m.rough)))*(.08+.5*m.metal);for(int k=0;k<3;++k)out[k]=(out[k]*(.35+fill_strength*f+.65*diffuse)+.025*rim+spec*(1-m.metal+out[k]*m.metal))*exposure;}
  for(int k=0;k<3;++k)out[k]+=m.emissive[k];return out;
 }
 template<class Fn>void pixels(const std::array<Projected,3>& p,Fn fn){
  const auto area=(p[1].x-p[0].x)*(p[2].y-p[0].y)-(p[1].y-p[0].y)*(p[2].x-p[0].x);if(!std::isfinite(area)||std::abs(area)<1e-12)return;
  const auto minx=std::min({p[0].x,p[1].x,p[2].x}),maxx=std::max({p[0].x,p[1].x,p[2].x}),miny=std::min({p[0].y,p[1].y,p[2].y}),maxy=std::max({p[0].y,p[1].y,p[2].y});if(maxx<0||maxy<0||minx>=image.width||miny>=image.height)return;
  const auto x0=static_cast<int>(std::max(0.,std::floor(minx))),x1=static_cast<int>(std::min(static_cast<double>(image.width-1),std::floor(maxx))),y0=static_cast<int>(std::max(0.,std::floor(miny))),y1=static_cast<int>(std::min(static_cast<double>(image.height-1),std::floor(maxy)));budget.charge(static_cast<std::size_t>(x1-x0+1)*(y1-y0+1));
  for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){budget.step();const auto px=x+.5,py=y+.5;std::array<double,3> w{((p[1].x-px)*(p[2].y-py)-(p[1].y-py)*(p[2].x-px))/area,((p[2].x-px)*(p[0].y-py)-(p[2].y-py)*(p[0].x-px))/area,0};w[2]=1-w[0]-w[1];if(w[0]<-1e-10||w[1]<-1e-10||w[2]<-1e-10)continue;fn(static_cast<std::size_t>(y)*image.width+x,w,area);}
 }
 void triangle(const Triangle& t,bool depthonly,bool transparent){
  std::array<Projected,3> p;for(int k=0;k<3;++k)p[k]=cam.project(t.v[k].p);const auto& material=scene.materials[t.material];const auto face=cross(sub(t.v[1].p,t.v[0].p),sub(t.v[2].p,t.v[0].p));const auto eye=cam.perspective?sub(mul(cam.b[2],-cam.d),t.v[0].p):mul(cam.b[2],-1);const auto back=dot(face,eye)<0;if(back&&!material.doubled)return;
  double lod=0;if(material.texture>=0){const auto& im=scene.textures[material.texture].levels[0];double density=0;for(int k=0;k<3;++k){const auto j=(k+1)%3;const auto screen=std::hypot(p[k].x-p[j].x,p[k].y-p[j].y);if(screen>1e-9)density=std::max(density,std::hypot((t.v[k].uv[0]-t.v[j].uv[0])*im.width,(t.v[k].uv[1]-t.v[j].uv[1])*im.height)/screen);}lod=std::log2(std::max(1e-9,density));}
  pixels(p,[&](std::size_t i,std::array<double,3> w,double){double denominator=0;for(int k=0;k<3;++k)denominator+=w[k]*p[k].inv;const auto z=cam.perspective?1/denominator-cam.d:w[0]*p[0].z+w[1]*p[1].z+w[2]*p[2].z;if(z>depth[i]+1e-9)return;if(depthonly&&material.alpha!="MASK"){depth[i]=z;return;}for(int k=0;k<3;++k)w[k]=w[k]*p[k].inv/denominator;Vertex v;v.n={};v.c={};for(int k=0;k<3;++k){v.n=add(v.n,mul(t.v[k].n,w[k]));for(int c=0;c<4;++c)v.c[c]+=t.v[k].c[c]*w[k];for(int c=0;c<2;++c)v.uv[c]+=t.v[k].uv[c]*w[k];}const auto c=shade(v,material,back,lod);if((!depthonly||material.alpha=="MASK")&&c[3]<=0)return;if(depthonly||!transparent)depth[i]=z;if(!depthonly)paint(i,c);});
 }
 void line(V a,V b,C c,bool testdepth){const auto p=cam.project(a),q=cam.project(b);const auto dx=q.x-p.x,dy=q.y-p.y;double low=0,high=1;auto bound=[&](double denom,double rhs){if(std::abs(denom)<1e-20)return rhs>=0;const auto t=rhs/denom;if(denom<0)low=std::max(low,t);else high=std::min(high,t);return low<=high;};if(!bound(-dx,p.x+1)||!bound(dx,image.width-p.x)||!bound(-dy,p.y+1)||!bound(dy,image.height-p.y))return;const auto count=static_cast<std::size_t>(std::ceil(std::max(std::abs(dx),std::abs(dy))*(high-low)))+1;budget.charge(count);for(std::size_t n=0;n<=count;++n){budget.step();const auto t=low+(high-low)*n/count;const auto x=static_cast<int>(std::floor(p.x+dx*t)),y=static_cast<int>(std::floor(p.y+dy*t));if(x<0||y<0||x>=static_cast<int>(image.width)||y>=static_cast<int>(image.height))continue;const auto z=cam.perspective?1/(p.inv*(1-t)+q.inv*t)-cam.d:p.z*(1-t)+q.z*t;const auto i=static_cast<std::size_t>(y)*image.width+x;if(!testdepth||z<=depth[i]+.002)paint(i,c);}}
 void floor(){if(!scene.displayed.valid())return;const auto& f=settings.at("floor");const bool enabled=f.at("enabled"),grid=settings.at("grid"),axes=settings.at("axes");if(!enabled&&!grid&&!axes)return;const auto plane=scene.displayed.lo[2]-.0001;const auto fc=color(f.at("color"));const auto opacity=number(f.at("opacity"));const auto raw=scene.span/8,decade=std::pow(10.,std::floor(std::log10(raw))),mantissa=raw/decade,step=(mantissa<=1?1:mantissa<=2?2:mantissa<=5?5:10)*decade;budget.charge(depth.size());
  for(std::uint32_t y=0;y<image.height;++y)for(std::uint32_t x=0;x<image.width;++x){budget.step();const auto [origin,direction]=cam.ray(x+.5,y+.5);if(std::abs(direction[2])<1e-12)continue;const auto time=(plane-origin[2])/direction[2];if(time<0)continue;const auto p=add(origin,mul(direction,time));if(length(p)>cam.extent*4)continue;const auto z=dot(cam.b[2],p);const auto i=static_cast<std::size_t>(y)*image.width+x;if(enabled){auto c=fc;c[3]=opacity;if(f.at("finish")=="glossy"){const auto highlight=.1*std::pow(std::max(0.,1-std::hypot(p[0]+.3,p[1]+.3)/1.3),5);for(int k=0;k<3;++k)c[k]+=highlight;}paint(i,c);depth[i]=z;}
   const auto wx=p[0]*scene.span+scene.center[0],wy=p[1]*scene.span+scene.center[1];const auto pixel_world=scene.span/cam.scale*(cam.perspective?(cam.d+z)/cam.d:1);const auto distance=[](double value,double period){const auto r=std::abs(std::remainder(value,period));return r;};if(grid&&(distance(wx,step)<pixel_world*.7||distance(wy,step)<pixel_world*.7))paint(i,{.45,.49,.54,.22});if(axes&&std::abs(wy)<pixel_world)paint(i,{.7,.15,.12,.7});if(axes&&std::abs(wx)<pixel_world)paint(i,{.12,.5,.2,.7});}
 }
 void shadows(){const auto& f=settings.at("floor");if(!f.at("enabled").get<bool>()||!light||!scene.displayed.valid())return;const auto plane=scene.displayed.lo[2]-.00005;std::vector<unsigned char> mask(depth.size());for(const auto& t:scene.triangles){if(scene.materials[t.material].opacity<=0)continue;std::array<Projected,3> p;for(int k=0;k<3;++k){auto v=t.v[k].p;const auto height=v[2]-plane;v[0]-=worldkey[0]*height/worldkey[2];v[1]-=worldkey[1]*height/worldkey[2];v[2]=plane;p[k]=cam.project(v);}pixels(p,[&](std::size_t i,const auto&,double){mask[i]=255;});}
  budget.charge(depth.size()*9);const auto alpha=.28*number(f.at("opacity"));for(std::uint32_t y=0;y<image.height;++y)for(std::uint32_t x=0;x<image.width;++x){budget.step();unsigned sum=0;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const auto sx=static_cast<int>(x)+dx*2,sy=static_cast<int>(y)+dy*2;if(sx>=0&&sy>=0&&sx<static_cast<int>(image.width)&&sy<static_cast<int>(image.height))sum+=mask[static_cast<std::size_t>(sy)*image.width+sx];}if(sum)paint(static_cast<std::size_t>(y)*image.width+x,{.08,.10,.13,alpha*sum/(9*255.)});}
 }
};
// Original small bitmap font: deterministic, portable printable ASCII. Font
// reproduction is deliberately explicit; unsupported Unicode cannot disappear.
const std::map<char,std::array<unsigned char,7>>& font(){
 static const std::map<char,std::array<unsigned char,7>> glyphs={
 {' ',{0,0,0,0,0,0,0}},{'!',{4,4,4,4,4,0,4}},{'"',{10,10,10,0,0,0,0}},{'#',{10,31,10,10,31,10,0}},{'$',{4,15,20,14,5,30,4}},{'%',{25,25,2,4,8,19,19}},{'&',{12,18,20,8,21,18,13}},{'\'',{4,4,8,0,0,0,0}},{'(',{2,4,8,8,8,4,2}},{')',{8,4,2,2,2,4,8}},{'*',{0,21,14,31,14,21,0}},{'+',{0,4,4,31,4,4,0}},{',',{0,0,0,0,0,4,8}},{'-',{0,0,0,31,0,0,0}},{'.',{0,0,0,0,0,0,4}},{'/',{1,2,2,4,8,8,16}},
 {'0',{14,17,19,21,25,17,14}},{'1',{4,12,4,4,4,4,14}},{'2',{14,17,1,2,4,8,31}},{'3',{30,1,1,14,1,1,30}},{'4',{2,6,10,18,31,2,2}},{'5',{31,16,16,30,1,1,30}},{'6',{14,16,16,30,17,17,14}},{'7',{31,1,2,4,8,8,8}},{'8',{14,17,17,14,17,17,14}},{'9',{14,17,17,15,1,1,14}},{':',{0,4,0,0,4,0,0}},{';',{0,4,0,0,4,4,8}},{'<',{2,4,8,16,8,4,2}},{'=',{0,0,31,0,31,0,0}},{'>',{8,4,2,1,2,4,8}},{'?',{14,17,1,2,4,0,4}},{'@',{14,17,23,21,23,16,14}},
 {'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},{'C',{14,17,16,16,16,17,14}},{'D',{30,17,17,17,17,17,30}},{'E',{31,16,16,30,16,16,31}},{'F',{31,16,16,30,16,16,16}},{'G',{14,17,16,23,17,17,15}},{'H',{17,17,17,31,17,17,17}},{'I',{14,4,4,4,4,4,14}},{'J',{7,2,2,2,2,18,12}},{'K',{17,18,20,24,20,18,17}},{'L',{16,16,16,16,16,16,31}},{'M',{17,27,21,21,17,17,17}},{'N',{17,25,21,19,17,17,17}},{'O',{14,17,17,17,17,17,14}},{'P',{30,17,17,30,16,16,16}},{'Q',{14,17,17,17,21,18,13}},{'R',{30,17,17,30,20,18,17}},{'S',{15,16,16,14,1,1,30}},{'T',{31,4,4,4,4,4,4}},{'U',{17,17,17,17,17,17,14}},{'V',{17,17,17,17,17,10,4}},{'W',{17,17,17,21,21,21,10}},{'X',{17,17,10,4,10,17,17}},{'Y',{17,17,10,4,4,4,4}},{'Z',{31,1,2,4,8,16,31}},
 {'[',{14,8,8,8,8,8,14}},{'\\',{16,8,8,4,2,2,1}},{']',{14,2,2,2,2,2,14}},{'^',{4,10,17,0,0,0,0}},{'_', {0,0,0,0,0,0,31}},{'`',{8,4,2,0,0,0,0}},
 {'a',{0,0,14,1,15,17,15}},{'b',{16,16,30,17,17,17,30}},{'c',{0,0,14,16,16,17,14}},{'d',{1,1,15,17,17,17,15}},{'e',{0,0,14,17,31,16,14}},{'f',{6,9,8,28,8,8,8}},{'g',{0,15,17,17,15,1,14}},{'h',{16,16,30,17,17,17,17}},{'i',{4,0,12,4,4,4,14}},{'j',{2,0,6,2,2,18,12}},{'k',{16,16,18,20,24,20,18}},{'l',{12,4,4,4,4,4,14}},{'m',{0,0,26,21,21,21,21}},{'n',{0,0,30,17,17,17,17}},{'o',{0,0,14,17,17,17,14}},{'p',{0,30,17,17,30,16,16}},{'q',{0,15,17,17,15,1,1}},{'r',{0,0,22,25,16,16,16}},{'s',{0,0,15,16,14,1,30}},{'t',{8,8,28,8,8,9,6}},{'u',{0,0,17,17,17,17,15}},{'v',{0,0,17,17,17,10,4}},{'w',{0,0,17,17,21,21,10}},{'x',{0,0,17,10,4,10,17}},{'y',{0,0,17,17,15,1,14}},{'z',{0,0,31,2,4,8,31}},
 {'{',{2,4,4,8,4,4,2}},{'|',{4,4,4,4,4,4,4}},{'}',{8,4,4,2,4,4,8}},{'~',{0,0,8,21,2,0,0}}
 };return glyphs;
}
void overlay(Raster& raster,const Json& item,std::size_t& text_count){
 const auto& cam=raster.cam;
 if(item.value("fill",false)&&item.contains("hatch_loops")){
  std::vector<std::vector<Projected>> loops;double lx=INFINITY,ly=INFINITY,hx=-INFINITY,hy=-INFINITY;std::size_t edges=0;
  for(const auto& loop:item.at("hatch_loops")){const auto& points=loop.is_object()?loop.at("points"):loop;std::vector<Projected> projected;for(const auto& p:points){const auto v=cam.project(vec(p));lx=std::min(lx,v.x);ly=std::min(ly,v.y);hx=std::max(hx,v.x);hy=std::max(hy,v.y);projected.push_back(v);}edges+=points.size();loops.push_back(std::move(projected));}
  if(edges&&hx>=0&&hy>=0&&lx<cam.w&&ly<cam.h){const auto x0=static_cast<int>(std::max(0.,std::floor(lx))),x1=static_cast<int>(std::min(double(cam.w-1),std::floor(hx))),y0=static_cast<int>(std::max(0.,std::floor(ly))),y1=static_cast<int>(std::min(double(cam.h-1),std::floor(hy)));const auto count=static_cast<std::size_t>(x1-x0+1)*(y1-y0+1);if(edges>work_limit/std::max<std::size_t>(1,count))throw Error("limit_exceeded","Native hatch scan exceeds the raster work budget");raster.budget.charge(count*edges);auto c=color(item.value("color",Json{.4,.5,.65}));c[3]=.65;
   for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){raster.budget.step();bool inside=false;const auto px=x+.5,py=y+.5;for(const auto& loop:loops){if(loop.empty())continue;auto a=loop.back();for(const auto& b:loop){if((a.y>py)!=(b.y>py)&&px<(b.x-a.x)*(py-a.y)/(b.y-a.y)+a.x)inside=!inside;a=b;}}if(inside)raster.paint(static_cast<std::size_t>(y)*cam.w+x,c);}}
 }
 if(!item.contains("text"))return;
 const auto text=text_field(item,"text");if(text.size()>16384-text_count)throw Error("limit_exceeded","Native text exceeds 16384 characters");text_count+=text.size();std::vector<std::string> lines(1);for(char ch:text){if(ch=='\n')lines.emplace_back();else{if(!font().contains(ch))throw Error("unsupported_format","Native PNG text supports printable ASCII and newline; unsupported glyphs require a different renderer");lines.back().push_back(ch);}}
 const auto& points=item.at("points");if(points.size()<2)throw Error("invalid_argument","Native text needs source anchor and direction");const auto origin=vec(points[0]);const auto anchor=cam.project(origin),end=cam.project(vec(points[1]));const auto height=number(item.value("height_mm",Json(1)));if(height<=0||height>1e9)throw Error("invalid_argument","Native text height must be positive");V up{0,height,0};if(item.contains("text_transform_mm")){const auto& m=item.at("text_transform_mm");if(!m.is_array()||m.size()!=16)throw Error("invalid_argument","Invalid native text transform");up={number(m[1])*height,number(m[5])*height,number(m[9])*height};}const auto top=cam.project(add(origin,up));const auto font_px=std::max(.2,std::hypot(top.x-anchor.x,top.y-anchor.y)),width=number(item.value("width_factor",Json(1)));if(width<=0||width>100)throw Error("invalid_argument","Invalid native text width factor");const auto angle=std::atan2(end.y-anchor.y,end.x-anchor.x),co=std::cos(angle),si=std::sin(angle);const auto attachment=item.value("attachment",0),horizontal=item.value("horizontal_alignment",0),vertical=item.value("vertical_alignment",0);if(attachment<0||attachment>9||horizontal<0||horizontal>5||vertical<0||vertical>3)throw Error("invalid_argument","Invalid native text alignment");if(horizontal==3||horizontal==5)throw Error("unsupported_format","Aligned and Fit native text require both-point shaping");const auto align=attachment?(attachment-1)%3:horizontal==1||horizontal==4?1:horizontal==2?2:0;
 const auto baseline=attachment?(attachment-1)/3:vertical==3?0:vertical==2?1:2;const auto baseline_shift=baseline==0?0:baseline==1?-font_px*.5:-font_px;const auto multi_shift=baseline==1?-(lines.size()-1)*font_px*.6:baseline==2?-(lines.size()-1)*font_px*1.2:0;
 auto ink=color(item.value("color",Json{.2,.25,.3}));for(std::size_t line=0;line<lines.size();++line){const auto advance=font_px*6/7*width,total=advance*lines[line].size(),left=align==0?0:align==1?-total*.5:-total,upper=baseline_shift+multi_shift+line*font_px*1.2;double lx=INFINITY,ly=INFINITY,hx=-INFINITY,hy=-INFINITY;
  for(const auto& p:std::array<std::array<double,2>,4>{{{left,upper},{left+total,upper},{left,upper+font_px},{left+total,upper+font_px}}}){const auto x=anchor.x+co*p[0]-si*p[1],y=anchor.y+si*p[0]+co*p[1];lx=std::min(lx,x);hx=std::max(hx,x);ly=std::min(ly,y);hy=std::max(hy,y);}if(hx<0||hy<0||lx>=cam.w||ly>=cam.h)continue;
  const auto x0=static_cast<int>(std::max(0.,std::floor(lx))),x1=static_cast<int>(std::min(double(cam.w-1),std::floor(hx))),y0=static_cast<int>(std::max(0.,std::floor(ly))),y1=static_cast<int>(std::min(double(cam.h-1),std::floor(hy)));raster.budget.charge(static_cast<std::size_t>(x1-x0+1)*(y1-y0+1));
  for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){raster.budget.step();const auto dx=x+.5-anchor.x,dy=y+.5-anchor.y,local_x=co*dx+si*dy-left,local_y=-si*dx+co*dy-upper;if(local_x<0||local_y<0||local_x>=total||local_y>=font_px)continue;const auto letter=static_cast<std::size_t>(local_x/advance);const auto col=static_cast<int>(std::floor((local_x-letter*advance)/(font_px/7*width))),row=static_cast<int>(std::floor(local_y/(font_px/7)));if(letter<lines[line].size()&&col>=0&&col<5&&row>=0&&row<7&&(font().at(lines[line][letter])[row]&(1u<<(4-col))))raster.paint(static_cast<std::size_t>(y)*cam.w+x,ink);}}
}
RgbaImage draw_scene(const Json& payload, const Scene& model, const Camera& cam,
                     const Json& settings, Budget& budget) {
 Raster raster(cam.w,cam.h,budget,model,cam,settings);raster.floor();raster.shadows();
 const auto mode=settings.at("mode").get<std::string>();
 if(mode!="wireframe"){
  if(mode=="hidden_line")for(const auto& t:model.triangles)raster.triangle(t,true,false);
  else {std::vector<std::size_t> transparent;for(std::size_t i=0;i<model.triangles.size();++i){budget.step();const auto& t=model.triangles[i];const auto& m=model.materials[t.material];const bool alpha=m.alpha!="MASK"&&(m.opacity<1||(m.alpha=="BLEND"&&m.texture>=0)||t.v[0].c[3]<1||t.v[1].c[3]<1||t.v[2].c[3]<1);if(alpha)transparent.push_back(i);else raster.triangle(t,false,false);}auto depth=[&](std::size_t i){const auto& t=model.triangles[i];return dot(cam.b[2],add(add(t.v[0].p,t.v[1].p),t.v[2].p));};std::sort(transparent.begin(),transparent.end(),[&](auto a,auto b){return depth(a)>depth(b);});for(auto i:transparent){budget.step();raster.triangle(model.triangles[i],false,true);}}
 }
 if(settings.at("edges").get<bool>()){
  const bool depth=mode!="wireframe"&&mode!="xray";for(const auto& curve:model.curves)for(std::size_t i=1;i<curve.points.size();i+=2)raster.line(curve.points[i-1],curve.points[i],curve.color,depth);
  // Mesh artifacts without source curves still have useful wireframe boundaries.
  if(mode=="wireframe"&&payload.contains("artifact_geometry")&&model.curves.empty())for(const auto& t:model.triangles)for(int k=0;k<3;++k)raster.line(t.v[k].p,t.v[(k+1)%3].p,{.12,.15,.18,1},false);
 }
 std::size_t text_count=0;for(const auto& item:model.overlays){budget.check();overlay(raster,item,text_count);}budget.check();return std::move(raster.image);
}
// Sheet labels live outside the scene viewport, using the same portable font.
void label(RgbaImage& image, const std::string& text, unsigned x, unsigned y) {
 for (char ch : text) {
  const auto& glyph = font().at(ch);
  for (unsigned row=0;row<7;++row) for(unsigned col=0;col<5;++col)
   if(glyph[row] & (1u << (4-col))) {
    const auto pixel=(static_cast<std::size_t>(y+row)*image.width+x+col)*4;
    for(unsigned k=0;k<3;++k) image.pixels[pixel+k]=45;
   }
  x+=6;
 }
}
RgbaImage render_image(const Json& payload,const Json& supplied,const std::function<void()>& check_cancel,Json* sheet) {
 fields(supplied,{}, {"width","height","view","camera","render","presentation","appearance","hidden_part_ids"});
 Budget budget{0,0,check_cancel};budget.check();
 auto dimension=[&](const char* key,int fallback){const auto value=supplied.value(key,Json(fallback));if(!value.is_number_integer()||value<16||value>2048)throw Error("invalid_argument","Native render dimensions must be integers from 16 to 2048");return value.get<std::uint32_t>();};
 const auto width=dimension("width",1024),height=dimension("height",768);
 if(static_cast<std::size_t>(width)*height>4*1024*1024)throw Error("limit_exceeded","Native render exceeds four million output pixels");
 const bool is_sheet=render_is_sheet(supplied);
 auto settings=normalize_render_settings(supplied.value("render",Json::object()));
 if(is_sheet) settings["projection"]="orthographic";
 const auto model=scene(payload,supplied,settings,budget);
 if(!is_sheet) return draw_scene(payload,model,camera(supplied,settings,model,width,height),settings,budget);

 // Two rows in reading order. Reserve a header in each tile; fit all displayed
 // bounds in every viewport, then use the smallest scale for all four cameras.
 const std::array<std::string,4> names={"front","top","right","iso"};
 const std::array<std::array<double,2>,4> angles={{{0,0},{0,pi/2},{-pi/2,0},{-pi/4,std::asin(1/std::sqrt(3.))}}};
 std::array<Camera,4> cameras;
 Json views=Json::array();
 double scale=INFINITY;
 const unsigned header=20;
 const auto bounds=model.displayed.valid()?model.displayed:Bounds{{-.5,-.5,-.5},{.5,.5,.5}};
 for(unsigned i=0;i<4;++i) {
  const auto x=i%2?width/2:0, y=i/2?height/2:0;
  const auto w=(i%2?width-width/2:width/2),h=(i/2?height-height/2:height/2)-header;
  Json pose={{"camera",{{"yaw",angles[i][0]},{"pitch",angles[i][1]},{"zoom",1},{"pan",{0,0}}}}};
  auto cam=camera(pose,settings,model,w,h);
  double lo_x=INFINITY,hi_x=-INFINITY,lo_y=INFINITY,hi_y=-INFINITY;
  for(unsigned corner=0;corner<8;++corner) {
   V point;for(unsigned k=0;k<3;++k)point[k]=(corner&(1u<<k))?bounds.hi[k]:bounds.lo[k];
   const auto u=dot(cam.b[0],point),v=dot(cam.b[1],point);
   lo_x=std::min(lo_x,u);hi_x=std::max(hi_x,u);lo_y=std::min(lo_y,v);hi_y=std::max(hi_y,v);
  }
  scale=std::min({scale,(w-16.)/std::max(1e-9,hi_x-lo_x),(h-16.)/std::max(1e-9,hi_y-lo_y)});
  // Hold projected centers until the shared scale has been determined.
  cam.px=-(lo_x+hi_x)*.5;cam.py=(lo_y+hi_y)*.5;cameras[i]=cam;
  views.push_back({{"name",names[i]},{"x",x},{"y",y+header},{"width",w},{"height",h}});
 }
 RgbaImage image{width,height,std::vector<unsigned char>(static_cast<std::size_t>(width)*height*4,255)};
 for(unsigned i=0;i<4;++i) {
  budget.check();auto& cam=cameras[i];cam.scale=scale;cam.px*=scale;cam.py*=scale;
  const auto tile=draw_scene(payload,model,cam,settings,budget);
  const auto x=views[i].at("x").get<unsigned>(),y=views[i].at("y").get<unsigned>();
  budget.charge(static_cast<std::size_t>(tile.width)*tile.height);
  for(unsigned row=0;row<tile.height;++row) {
   budget.check();std::copy_n(tile.pixels.begin()+static_cast<std::size_t>(row)*tile.width*4,tile.width*4,
    image.pixels.begin()+(static_cast<std::size_t>(y+row)*width+x)*4);
  }
  label(image,names[i],x+8,y-header+6);
 }
 for(unsigned y=0;y<height;++y)for(unsigned k=0;k<3;++k)image.pixels[(static_cast<std::size_t>(y)*width+width/2)*4+k]=210;
 for(unsigned x=0;x<width;++x)for(unsigned k=0;k<3;++k)image.pixels[(static_cast<std::size_t>(height/2)*width+x)*4+k]=210;
 if(sheet)*sheet={{"projection","orthographic"},{"pixels_per_mm",scale/model.span},{"views",views}};
 budget.check();return image;
}
}
bool render_is_sheet(const Json& settings) {
 const auto view=settings.value("view",Json("single"));
 if(view!="single"&&view!="sheet")throw Error("invalid_argument","Render view must be single or sheet");
 if(view!="sheet")return false;
 if(settings.contains("camera"))throw Error("invalid_argument","Sheet view uses fixed cameras; omit camera");
 if(settings.value("render",Json::object()).value("projection",Json("orthographic"))!="orthographic")
  throw Error("invalid_argument","Sheet view requires orthographic projection");
 for(const auto* key:{"width","height"})if(settings.contains(key)&&(!settings.at(key).is_number_integer()||settings.at(key)<128||settings.at(key)>2048))
  throw Error("invalid_argument","Sheet dimensions must be integers from 128 to 2048 pixels");
 return true;
}
RgbaImage render_rgba(const Json& payload,const Json& supplied,const std::function<void()>& check_cancel) {
 return render_image(payload,supplied,check_cancel,nullptr);
}
Json render_png(const Json& payload,const Json& supplied,const fs::path& destination,const std::function<void()>& check_cancel){
 Json sheet;const auto image=render_image(payload,supplied,check_cancel,&sheet);const auto bytes=encode_png(image);if(bytes.size()>16*1024*1024)throw Error("limit_exceeded","Native rendered PNG exceeds 16 MiB");if(check_cancel)check_cancel();atomic_text(destination,bytes,16*1024*1024);
 Json result={{"width",image.width},{"height",image.height},{"bytes",bytes.size()},{"sha256",sha256(bytes)},{"method","native_cpu_rasterizer"}};
 if(!sheet.is_null())result["sheet"]=std::move(sheet);return result;
}
}
