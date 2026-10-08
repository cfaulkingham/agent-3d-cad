#include "artifact_internal.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <sstream>

namespace agentcad::artifact_detail {
namespace {
float f32(const std::string& bytes,std::size_t offset){const float value=std::bit_cast<float>(u32(bytes,offset));finite(value);return value;}
std::size_t index(const Json& value,std::size_t size){require(value.is_number_integer()&&value>=0&&value<size,"Invalid artifact array index");return value.get<std::size_t>();}
std::size_t length(const Json& value,std::size_t maximum){require(value.is_number_integer()&&value>=0&&value<=maximum,"Invalid bounded artifact length");return value.get<std::size_t>();}
Mat node_matrix(const Json& node){
  if(node.contains("matrix")){require(!node.contains("translation")&&!node.contains("rotation")&&!node.contains("scale"),"GLTF matrix and TRS are mutually exclusive");const auto& a=node.at("matrix");require(a.is_array()&&a.size()==16,"GLTF matrix needs 16 components");Mat m{};for(int r=0;r<4;++r)for(int c=0;c<4;++c){require(a[c*4+r].is_number(),"GLTF transform must be numeric");m[r*4+c]=finite(a[c*4+r].get<double>());}
    for(int r=0;r<3;++r)m[r*4+3]*=1000;affine(m);return m;}
  const auto vec=[&](const char* key,Json fallback,std::size_t n){const auto a=node.value(key,fallback);require(a.is_array()&&a.size()==n,"GLTF TRS component count differs");std::vector<double> v;for(const auto&c:a){require(c.is_number(),"GLTF TRS must be numeric");v.push_back(finite(c.get<double>()));}return v;};
  const auto t=vec("translation",{0,0,0},3),s=vec("scale",{1,1,1},3),q=vec("rotation",{0,0,0,1},4);double norm=0;for(auto c:q)norm+=c*c;require(std::abs(norm-1)<=1e-5,"GLTF quaternion is not normalized");
  const double x=q[0],y=q[1],z=q[2],w=q[3];Mat m={(1-2*y*y-2*z*z)*s[0],(2*x*y-2*z*w)*s[1],(2*x*z+2*y*w)*s[2],t[0]*1000,(2*x*y+2*z*w)*s[0],(1-2*x*x-2*z*z)*s[1],(2*y*z-2*x*w)*s[2],t[1]*1000,(2*x*z-2*y*w)*s[0],(2*y*z+2*x*w)*s[1],(1-2*x*x-2*y*y)*s[2],t[2]*1000,0,0,0,1};affine(m);return m;
}
void no_extensions(const Json& value){if(value.is_object()){if(value.contains("extensions")&&!value.at("extensions").empty())unsupported("GLTF extensions are unsupported in this reader");for(const auto&entry:value.items())no_extensions(entry.value());}else if(value.is_array())for(const auto&entry:value)no_extensions(entry);}
}
Parsed stl(const std::string& bytes,double scale){
  Parsed parsed;parsed.representation="triangle_mesh";std::vector<Vec> vertices;std::vector<std::array<std::size_t,3>> triangles;
  const bool binary=bytes.size()>=84&&u32(bytes,80)<=artifact_geometry_limit&&84ull+50ull*u32(bytes,80)==bytes.size();
  if(binary){const auto count=u32(bytes,80);require(count>0&&count<=artifact_geometry_limit/3,"STL triangle/vertex budget exceeded");
    for(std::size_t n=0;n<count;++n){if(n%4096==0)check_job_cancelled();const auto start=84+n*50;for(int c=0;c<3;++c)f32(bytes,start+c*4);
      if(u16(bytes,start+48)!=0)unsupported("Binary STL color/attribute payloads are unsupported");const auto first=vertices.size();for(int v=0;v<3;++v)vertices.push_back({f32(bytes,start+12+v*12)*scale,f32(bytes,start+16+v*12)*scale,f32(bytes,start+20+v*12)*scale});triangles.push_back({first,first+1,first+2});}
  }else{require(bytes.find('\0')==std::string::npos&&!invalid_utf8_offset(bytes),"Invalid ASCII STL text");std::istringstream input(bytes);std::string token,line;require(static_cast<bool>(input>>token)&&token=="solid","STL must be valid binary or ASCII solid");std::getline(input,line);
    bool ended=false;while(input>>token){if(token=="endsolid"){std::getline(input,line);ended=true;break;}require(token=="facet","Expected STL facet");require(static_cast<bool>(input>>token)&&token=="normal","Missing STL facet normal");for(int c=0;c<3;++c){require(static_cast<bool>(input>>token),"Missing STL normal component");real(token);}require(static_cast<bool>(input>>token)&&token=="outer"&&static_cast<bool>(input>>token)&&token=="loop","Missing STL outer loop");
      require(vertices.size()+3<=artifact_geometry_limit,"STL vertex budget exceeded");const auto first=vertices.size();for(int v=0;v<3;++v){require(static_cast<bool>(input>>token)&&token=="vertex","Missing STL vertex");Vec p{};for(auto&c:p){require(static_cast<bool>(input>>token),"Missing STL vertex coordinate");c=real(token)*scale;}vertices.push_back(p);}require(static_cast<bool>(input>>token)&&token=="endloop"&&static_cast<bool>(input>>token)&&token=="endfacet","Missing STL facet terminator");triangles.push_back({first,first+1,first+2});check_job_cancelled();}
    require(ended&&!(input>>token),"STL missing endsolid or has trailing tokens");}
  parsed.geometry.mesh(vertices,triangles,identity(),binary?"binary_stl/facets":"ascii_stl/facets");parsed.metadata={{"encoding",binary?"binary":"ascii"},{"solidness","not_established"}};
  parsed.limitations.push_back("STL is a triangle surface; watertightness, solid volume and original CAD faces are not established.");return parsed;
}
Parsed glb(const std::string& bytes){
  require(bytes.size()>=20&&u32(bytes,0)==0x46546c67u&&u32(bytes,4)==2&&u32(bytes,8)==bytes.size(),"Invalid GLB 2 header or length");std::size_t offset=12;std::string text,bin;unsigned chunks=0;
  while(offset<bytes.size()){require(offset+8<=bytes.size(),"Truncated GLB chunk");const auto size=u32(bytes,offset),kind=u32(bytes,offset+4);require(size%4==0&&size<=bytes.size()-offset-8,"Invalid GLB chunk length");
    if(chunks==0){require(kind==0x4e4f534au&&size<=16*1024*1024,"GLB first chunk must be bounded JSON");text=bytes.substr(offset+8,size);}
    else if(chunks==1){require(kind==0x004e4942u,"GLB second chunk must be BIN");bin=bytes.substr(offset+8,size);}else unsupported("Extra GLB chunks are unsupported");offset+=8+size;++chunks;}
  require(chunks==2,"GLB review requires one JSON and one embedded BIN chunk");const auto doc=parse_json(text,16*1024*1024);require(doc.is_object()&&doc.contains("asset")&&doc.at("asset").value("version",Json())=="2.0","GLB asset version must be 2.0");
  no_extensions(doc);if(doc.contains("extensionsRequired")&&!doc.at("extensionsRequired").empty())unsupported("GLTF required extensions are unsupported");
  if((doc.contains("animations")&&!doc.at("animations").empty())||(doc.contains("skins")&&!doc.at("skins").empty()))unsupported("Animated or skinned GLTF needs another review backend");
  const auto&buffers=doc.at("buffers");require(buffers.is_array()&&buffers.size()==1&&!buffers[0].contains("uri"),"GLB requires one self-contained buffer");const auto declared=length(buffers[0].at("byteLength"),bin.size());require(bin.size()-declared<=3,"GLB BIN padding differs from buffer length");
  if(doc.contains("images"))for(const auto&image:doc.at("images"))if(image.contains("uri"))unsupported("External GLTF image references are prohibited");
  const auto&views=doc.at("bufferViews");const auto&accessors=doc.at("accessors");require(views.is_array()&&views.size()<=10000&&accessors.is_array()&&accessors.size()<=10000,"GLTF accessor budget exceeded");
  struct Access {std::size_t start,stride,count;unsigned component;};
  const auto access=[&](const Json&id,bool positions){const auto&a=accessors.at(index(id,accessors.size()));if(a.contains("sparse"))unsupported("Sparse GLTF accessors are unsupported");require(!a.value("normalized",false),"Normalized position/index accessor is unsupported");
    require(a.value("type","")==(positions?"VEC3":"SCALAR"),"GLTF accessor type differs");const unsigned component=a.at("componentType");const std::size_t width=component==5121?1:component==5123?2:component==5125||component==5126?4:0;
    require(width&& (positions?component==5126:component!=5126),"Unsupported GLTF position/index component type");const auto&v=views.at(index(a.at("bufferView"),views.size()));require(v.at("buffer")==0,"GLTF bufferView must address embedded buffer");
    const auto vo=length(v.value("byteOffset",Json(0)),declared),vl=length(v.at("byteLength"),declared),ao=length(a.value("byteOffset",Json(0)),vl),count=length(a.at("count"),artifact_geometry_limit);require(count>0&&vo<=declared&&vl<=declared-vo,"GLTF bufferView exceeds embedded buffer");
    const auto element=width*(positions?3:1),stride=length(v.value("byteStride",Json(element)),252);require(stride>=element&&stride%width==0&&(vo+ao)%width==0,"Invalid GLTF accessor alignment/stride");
    require(ao<=vl&&element<=vl-ao&&(count-1)<=(vl-ao-element)/stride,"GLTF accessor exceeds bufferView");return Access{vo+ao,stride,count,component};};
  const auto&nodes=doc.at("nodes");const auto&meshes=doc.at("meshes");const auto&scenes=doc.at("scenes");require(nodes.is_array()&&nodes.size()<=10000&&meshes.is_array()&&meshes.size()<=10000&&scenes.is_array()&&!scenes.empty(),"GLTF scene budget exceeded");
  const auto scene=index(doc.value("scene",Json(0)),scenes.size());std::vector<unsigned> parents(nodes.size());
  for(const auto&node:nodes){if(node.contains("skin")||node.contains("weights"))unsupported("GLTF skinning or morph weights are unsupported");if(node.contains("children")){require(node.at("children").is_array(),"GLTF children must be an array");for(const auto&child:node.at("children"))require(++parents[index(child,nodes.size())]==1,"GLTF node has multiple parents");}}
  Parsed parsed;parsed.representation="triangle_mesh";std::set<std::size_t> active,visited;
  std::function<void(std::size_t,const Mat&,unsigned)> visit=[&](std::size_t id,const Mat&parent,unsigned depth){check_job_cancelled();require(depth<=64&&active.insert(id).second&&visited.insert(id).second,"GLTF scene cycle or duplicate instance");const auto&node=nodes[id];const auto m=product(parent,node_matrix(node));
    if(node.contains("mesh")){const auto mesh_id=index(node.at("mesh"),meshes.size());const auto&mesh=meshes[mesh_id];if(mesh.contains("weights"))unsupported("GLTF morph weights are unsupported");const auto&primitives=mesh.at("primitives");require(primitives.is_array()&&primitives.size()<=10000,"GLTF primitive budget exceeded");unsigned primitive_id=0;
      for(const auto&p:primitives){if(p.contains("targets"))unsupported("GLTF morph targets are unsupported");require(p.value("mode",4)==4,"Only GLTF triangle primitives are supported");const auto a=access(p.at("attributes").at("POSITION"),true);std::vector<Vec> v;for(std::size_t n=0;n<a.count;++n)v.push_back({f32(bin,a.start+n*a.stride)*1000.,f32(bin,a.start+n*a.stride+4)*1000.,f32(bin,a.start+n*a.stride+8)*1000.});std::vector<std::size_t> indices;
        if(p.contains("indices")){const auto b=access(p.at("indices"),false);for(std::size_t n=0;n<b.count;++n){const auto at=b.start+n*b.stride;indices.push_back(b.component==5121?static_cast<unsigned char>(bin[at]):b.component==5123?u16(bin,at):u32(bin,at));}}else for(std::size_t n=0;n<v.size();++n)indices.push_back(n);
        require(indices.size()%3==0,"GLTF triangle index count is not divisible by three");std::vector<std::array<std::size_t,3>> t;for(std::size_t n=0;n<indices.size();n+=3)t.push_back({indices[n],indices[n+1],indices[n+2]});
        parsed.geometry.mesh(v,t,m,"nodes/"+std::to_string(id)+"/meshes/"+std::to_string(mesh_id)+"/primitives/"+std::to_string(primitive_id++),{{"name",node.value("name",std::string())}});}}
    if(node.contains("children"))for(const auto&child:node.at("children"))visit(index(child,nodes.size()),m,depth+1);active.erase(id);};
  require(scenes[scene].contains("nodes")&&scenes[scene].at("nodes").is_array(),"GLTF selected scene lacks root nodes");for(const auto&root:scenes[scene].at("nodes")){const auto id=index(root,nodes.size());require(parents[id]==0,"GLTF scene root has a parent");visit(id,identity(),0);}
  require(!parsed.geometry.triangles.empty(),"GLTF selected scene has no supported geometry");parsed.metadata={{"scene",scene},{"nodes_reviewed",visited.size()},{"source_units","m"},{"up_axis","Y"}};parsed.limitations.push_back("Selected scene geometry and transforms are reviewed; materials, textures, cameras, lighting and inactive scenes are not rendered.");return parsed;
}
}
