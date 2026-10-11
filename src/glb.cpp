#include "agentcad/glb.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <vector>
namespace agentcad {
namespace {
using Matrix=std::array<double,16>;
using Vector=std::array<double,3>;
using Quaternion=std::array<double,4>;
constexpr Matrix unit={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
constexpr std::size_t max_bytes=64*1024*1024;
Matrix matrix(const Json& value) {
  if(!value.is_array()||value.size()!=16)throw Error("invalid_argument","GLB leaf transform needs sixteen row-major values");
  Matrix out;for(int i=0;i<16;++i)out[i]=number(value.at(i));
  for(int i=0;i<3;++i)for(int j=0;j<3;++j){double dot=0;for(int k=0;k<3;++k)dot+=out[i*4+k]*out[j*4+k];if(std::abs(dot-(i==j?1:0))>1e-7)throw Error("invalid_argument","GLB leaf transforms must be rigid");}
  const auto determinant=out[0]*(out[5]*out[10]-out[6]*out[9])-out[1]*(out[4]*out[10]-out[6]*out[8])+out[2]*(out[4]*out[9]-out[5]*out[8]);
  if(std::abs(determinant-1)>1e-7||std::abs(out[12])+std::abs(out[13])+std::abs(out[14])+std::abs(out[15]-1)>1e-9)throw Error("invalid_argument","GLB transforms require a proper rigid matrix");return out;
}
Vector inverse(const Matrix& m,const Json& point) {
  if(!point.is_array()||point.size()!=3)throw Error("invalid_argument","GLB mesh point needs three values");Vector delta{},out{};
  for(int i=0;i<3;++i)delta[i]=number(point.at(i))-m[i*4+3];
  for(int i=0;i<3;++i)for(int j=0;j<3;++j)out[i]+=m[j*4+i]*delta[j]/1000.;return out;
}
Quaternion rotation(const Matrix& m) {
  Quaternion q{};const double trace=m[0]+m[5]+m[10];
  if(trace>0){const auto v=std::sqrt(trace+1)*2;q={(m[9]-m[6])/v,(m[2]-m[8])/v,(m[4]-m[1])/v,v/4};}
  else if(m[0]>m[5]&&m[0]>m[10]){const auto v=std::sqrt(1+m[0]-m[5]-m[10])*2;q={v/4,(m[1]+m[4])/v,(m[2]+m[8])/v,(m[9]-m[6])/v};}
  else if(m[5]>m[10]){const auto v=std::sqrt(1+m[5]-m[0]-m[10])*2;q={(m[1]+m[4])/v,v/4,(m[6]+m[9])/v,(m[2]-m[8])/v};}
  else {const auto v=std::sqrt(1+m[10]-m[0]-m[5])*2;q={(m[2]+m[8])/v,(m[6]+m[9])/v,v/4,(m[4]-m[1])/v};}
  double n=0;for(double v:q)n+=v*v;n=std::sqrt(n);for(auto& v:q)v/=n;return q;
}
Json translation(const Matrix& m){return {m[3]/1000,m[7]/1000,m[11]/1000};}
void word(std::vector<unsigned char>& data,std::uint32_t value){for(int k=0;k<4;++k)data.push_back(static_cast<unsigned char>((value>>(k*8))&255));}
void scalar(std::vector<unsigned char>& data,double value){const auto f=static_cast<float>(value);if(!std::isfinite(f))throw Error("limit_exceeded","GLB coordinates exceed finite float precision");word(data,std::bit_cast<std::uint32_t>(f));}
struct Buffer {
  Json document={{"asset",{{"version","2.0"},{"generator","agent-3d-cad native GLB"}}},{"scene",0},{"scenes",Json::array()},{"nodes",Json::array()},{"meshes",Json::array()},{"materials",Json::array()},{"accessors",Json::array()},{"bufferViews",Json::array()}};
  std::vector<unsigned char> binary;
  std::size_t accessor(const std::vector<double>& data,std::size_t width,bool bounds,int target=0) {
    while(binary.size()%4)binary.push_back(0);const auto offset=binary.size();for(double value:data)scalar(binary,value);
    if(binary.size()>max_bytes)throw Error("limit_exceeded","GLB binary exceeds 64 MiB");
    Json view={{"buffer",0},{"byteOffset",offset},{"byteLength",binary.size()-offset}};if(target)view["target"]=target;
    const auto vi=document["bufferViews"].size();document["bufferViews"].push_back(view);
    Json entry={{"bufferView",vi},{"componentType",5126},{"count",data.size()/width},{"type",width==1?"SCALAR":width==3?"VEC3":"VEC4"}};
    if(bounds){std::vector<double> lo(width,std::numeric_limits<double>::infinity()),hi(width,-std::numeric_limits<double>::infinity());for(std::size_t i=0;i<data.size();++i){lo[i%width]=std::min(lo[i%width],static_cast<double>(static_cast<float>(data[i])));hi[i%width]=std::max(hi[i%width],static_cast<double>(static_cast<float>(data[i])));}entry["min"]=lo;entry["max"]=hi;}
    const auto id=document["accessors"].size();document["accessors"].push_back(entry);return id;
  }
};
std::map<std::string,std::size_t> hierarchy(Buffer& out,const Json& summary,const std::map<std::string,Matrix>& transforms) {
  std::map<std::string,std::size_t> parents;
  if(!summary.contains("assembly")||!summary.at("assembly").contains("tree"))return parents;
  const auto& supplied=summary.at("assembly").at("tree");
  if(!supplied.is_array()||supplied.size()>8192)throw Error("limit_exceeded","GLB assembly hierarchy supports at most 8192 occurrences");
  std::map<std::string,Json> tree;std::map<std::string,std::size_t> groups;
  for(const auto& item:supplied){const auto id=text_field(item,"id"),kind=text_field(item,"kind");text_field(item,"parent_id");
    if(id.empty()||(kind!="part"&&kind!="assembly")||!tree.emplace(id,item).second)throw Error("invalid_argument","GLB hierarchy needs unique assembly and part occurrences");
    if(kind=="assembly"){const auto node=out.document["nodes"].size();groups.emplace(id,node);out.document["nodes"].push_back({{"name",id},{"children",Json::array()},{"extras",{{"occurrence_id",id},{"kind","assembly"},{"assembly_id",item.value("assembly_id",std::string{})},{"input",item.value("input",std::string{})},{"transform_encoding","identity group; descendant rigid transforms are world-relative"}}}});}
    else if(!transforms.contains(id))throw Error("invalid_argument","GLB hierarchy contains an unknown source leaf");
  }
  for(const auto& [id,item]:tree){const auto parent=text_field(item,"parent_id");
    const auto slash=id.rfind('/');const auto expected=slash==std::string::npos?std::string{}:id.substr(0,slash);
    if(parent!=expected||(!parent.empty()&&!groups.contains(parent)))throw Error("invalid_argument","GLB hierarchy parent must be the containing assembly occurrence");
    const auto parent_node=parent.empty()?0:groups.at(parent);
    if(item.at("kind")=="assembly")out.document["nodes"][parent_node]["children"].push_back(groups.at(id));
    else parents.emplace(id,parent_node);
  }
  if(parents.size()!=transforms.size())throw Error("invalid_argument","GLB hierarchy must cover every source leaf");return parents;
}
Json finish_material(const Json& style,const Json& fallback) {
  auto color=style.value("color",fallback.at("default_color"));if(!color.is_array()||color.size()!=3)throw Error("invalid_argument","GLB color requires RGB");
  for(const auto& v:color)if(number(v)<0||number(v)>1)throw Error("invalid_argument","GLB colors lie in [0,1]");
  const auto material=style.value("material",fallback.value("material",Json::object()));fields(material,{}, {"opacity","finish"});const auto opacity=material.contains("opacity")?number(material.at("opacity")):1.;
  const auto finish=material.value("finish",std::string("satin"));if(opacity<0||opacity>1||(finish!="matte"&&finish!="satin"&&finish!="glossy"&&finish!="metal"))throw Error("invalid_argument","Unsupported GLB finish or opacity");
  const auto roughness=finish=="matte"?.9:finish=="satin"?.5:finish=="glossy"?.15:.24;
  return {{"pbrMetallicRoughness",{{"baseColorFactor",{number(color[0]),number(color[1]),number(color[2]),opacity}},{"metallicFactor",finish=="metal"?1:0},{"roughnessFactor",roughness}}},{"alphaMode",opacity<1?"BLEND":"OPAQUE"},{"doubleSided",true}};
}
}
Json export_glb(const Json& mesh,const Json& topology,const Json& summary,const Json& identity,const Json& supplied_appearance,const Json& samples,const std::filesystem::path& destination,const std::function<void()>& check_cancel) {
  const auto check=[&]{if(check_cancel)check_cancel();};check();
  const auto appearance=supplied_appearance.is_null()?Json{{"default_color",{.66,.75,.8}},{"parts",Json::array()}}:supplied_appearance;
  fields(appearance,{"default_color","parts"},{"material"});if(!appearance.at("parts").is_array()||appearance.at("parts").size()>1024)throw Error("invalid_argument","Invalid GLB leaf appearance");
  std::map<std::string,Matrix> transforms;
  if(summary.contains("assembly"))for(const auto& part:summary.at("assembly").at("parts")){const auto id=text_field(part,"id");if(!transforms.emplace(id,matrix(part.at("transform"))).second)throw Error("invalid_argument","Duplicate GLB leaf");}
  else transforms.emplace("",unit);
  if(transforms.size()>1024)throw Error("limit_exceeded","GLB export supports at most 1024 leaves");
  std::map<std::string,Json> styles;for(const auto& item:appearance.at("parts")){fields(item,{"part_id","color"},{"material"});const auto id=text_field(item,"part_id");if(!transforms.contains(id)||!styles.emplace(id,item).second)throw Error("invalid_argument","GLB appearance needs unique current leaves");}
  std::map<std::string,std::string> owners;for(const auto& face:topology.at("faces"))owners.emplace(text_field(face,"id"),face.value("part_id",std::string{}));
  if(!mesh.at("positions").is_array()||mesh.at("positions").size()>200000||!mesh.at("triangles").is_array()||mesh.at("triangles").size()>200000||mesh.at("triangles").size()!=mesh.at("triangle_faces").size())throw Error("limit_exceeded","GLB source mesh exceeds native display limits");
  std::map<std::string,std::vector<std::size_t>> triangles;
  for(std::size_t i=0;i<mesh.at("triangles").size();++i){const auto face=mesh.at("triangle_faces").at(i).get<std::string>();if(!owners.contains(face)||!transforms.contains(owners.at(face)))throw Error("invalid_argument","GLB mesh face ownership is incomplete");triangles[owners.at(face)].push_back(i);}
  if(triangles.empty())throw Error("export_failed","GLB export requires tessellated surfaces");
  Buffer out;out.document["extras"]={{"source",identity},{"source_units","mm"},{"output_units","m"},{"linear_deflection_mm",mesh.value("linear_deflection_mm",.1)}};
  out.document["nodes"].push_back({{"name","CAD Z-up to glTF Y-up"},{"rotation",{-std::sqrt(.5),0,0,std::sqrt(.5)}},{"children",Json::array()}});
  const auto parents=hierarchy(out,summary,transforms);
  std::map<std::string,std::size_t> nodes;
  for(const auto& [owner,indices]:triangles){check();const auto& transform=transforms.at(owner);std::vector<double> positions,normals;
    for(auto index:indices){const auto& tri=mesh.at("triangles").at(index);if(!tri.is_array()||tri.size()!=3)throw Error("invalid_argument","GLB mesh triangle needs three indices");std::array<Vector,3> points;
      for(int k=0;k<3;++k){if(!tri.at(k).is_number_unsigned()&&!tri.at(k).is_number_integer())throw Error("invalid_argument","Invalid GLB mesh index");const auto n=tri.at(k).get<std::int64_t>();if(n<0||static_cast<std::uint64_t>(n)>=mesh.at("positions").size())throw Error("invalid_argument","Invalid GLB mesh index");points[k]=inverse(transform,mesh.at("positions").at(n));}
      const auto a=Vector{points[1][0]-points[0][0],points[1][1]-points[0][1],points[1][2]-points[0][2]},b=Vector{points[2][0]-points[0][0],points[2][1]-points[0][1],points[2][2]-points[0][2]};Vector normal{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};const auto length=std::hypot(normal[0],normal[1],normal[2]);if(length<=1e-20)continue;for(auto& v:normal)v/=length;
      for(const auto& point:points){positions.insert(positions.end(),point.begin(),point.end());normals.insert(normals.end(),normal.begin(),normal.end());}}
    if(positions.empty())throw Error("export_failed","GLB leaf contains only degenerate triangles",{{"part_id",owner}});
    const auto p=out.accessor(positions,3,true,34962),n=out.accessor(normals,3,false,34962),material_id=out.document["materials"].size();out.document["materials"].push_back(finish_material(styles.contains(owner)?styles.at(owner):Json::object(),appearance));
    const auto mi=out.document["meshes"].size();out.document["meshes"].push_back({{"name",owner.empty()?identity.value("feature_id",std::string("model")):owner},{"primitives",Json::array({{{"attributes",{{"POSITION",p},{"NORMAL",n}}},{"material",material_id},{"mode",4}}})}});
    const auto ni=out.document["nodes"].size();nodes.emplace(owner,ni);out.document["nodes"][parents.contains(owner)?parents.at(owner):0]["children"].push_back(ni);out.document["nodes"].push_back({{"name",owner.empty()?identity.value("feature_id",std::string("model")):owner},{"mesh",mi},{"translation",translation(transform)},{"rotation",rotation(transform)},{"extras",{{"part_id",owner},{"source",identity}}}});
  }
  if(!samples.is_null()&&!samples.empty()) {
    if(!samples.is_array()||samples.size()<2||samples.size()>2048)throw Error("limit_exceeded","GLB animation needs 2 to 2048 native pose samples");
    std::vector<double> times;std::vector<std::map<std::string,Matrix>> poses;double previous=-1;
    for(const auto& sample:samples){check();fields(sample,{"time_s","parts"});const auto time=number(sample.at("time_s"));if(time<0||time>3600||time<=previous||(previous<0&&time!=0))throw Error("invalid_argument","GLB sample times must start at zero and increase");previous=time;times.push_back(time);
      std::map<std::string,Matrix> pose;for(const auto& part:sample.at("parts")){fields(part,{"id","transform"});const auto id=part.at("id").get<std::string>();if(!transforms.contains(id)||!pose.emplace(id,matrix(part.at("transform"))).second)throw Error("invalid_argument","GLB pose leaves differ from source");}if(pose.size()!=transforms.size())throw Error("invalid_argument","GLB poses must cover every source leaf");poses.push_back(std::move(pose));}
    const auto input=out.accessor(times,1,true);Json animation={{"name",identity.value("sequence_name",std::string("Native sequence"))},{"samplers",Json::array()},{"channels",Json::array()},{"extras",{{"source",identity},{"sampling","native rigid poses with linear glTF translation/quaternion interpolation"}}}};
    for(const auto& [owner,node]:nodes){check();std::vector<double> translations,rotations;Quaternion last{};bool first=true;
      for(const auto& pose:poses){const auto& m=pose.at(owner);const auto t=translation(m).get<std::vector<double>>();translations.insert(translations.end(),t.begin(),t.end());auto q=rotation(m);double dot=0;for(int k=0;k<4;++k)dot+=last[k]*q[k];if(!first&&dot<0)for(auto& v:q)v=-v;rotations.insert(rotations.end(),q.begin(),q.end());last=q;first=false;}
      for(const auto& [path,data,width]:std::vector<std::tuple<std::string,const std::vector<double>*,int>>{{"translation",&translations,3},{"rotation",&rotations,4}}){const auto output=out.accessor(*data,width,false),sampler=animation["samplers"].size();animation["samplers"].push_back({{"input",input},{"output",output},{"interpolation","LINEAR"}});animation["channels"].push_back({{"sampler",sampler},{"target",{{"node",node},{"path",path}}}});}}
    out.document["animations"]=Json::array({animation});
  }
  out.document["scenes"].push_back({{"nodes",Json::array({0})}});out.document["buffers"]=Json::array({{{"byteLength",out.binary.size()}}});auto json=out.document.dump();while(json.size()%4)json.push_back(' ');while(out.binary.size()%4)out.binary.push_back(0);
  const auto size=12+8+json.size()+8+out.binary.size();if(size>max_bytes)throw Error("limit_exceeded","GLB export exceeds 64 MiB");check();std::vector<unsigned char> header;word(header,0x46546c67);word(header,2);word(header,static_cast<std::uint32_t>(size));word(header,static_cast<std::uint32_t>(json.size()));word(header,0x4e4f534a);
  std::ofstream file(destination,std::ios::binary|std::ios::trunc);if(!file)throw Error("storage_error","Cannot create staged GLB export");file.write(reinterpret_cast<const char*>(header.data()),header.size());file.write(json.data(),json.size());header.clear();word(header,static_cast<std::uint32_t>(out.binary.size()));word(header,0x004e4942);file.write(reinterpret_cast<const char*>(header.data()),header.size());file.write(reinterpret_cast<const char*>(out.binary.data()),out.binary.size());file.close();if(!file)throw Error("storage_error","Cannot write staged GLB export");check();
  return {{"bytes",size},{"mesh_count",nodes.size()},{"sample_count",samples.is_array()?samples.size():0},{"units","m"},{"source",identity}};
}
}
