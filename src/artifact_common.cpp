#include "artifact_internal.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>

namespace agentcad::artifact_detail {
[[noreturn]] void invalid(const std::string& message) { throw Error("artifact_invalid", message); }
[[noreturn]] void unsupported(const std::string& message) { throw Error("unsupported_feature", message); }
void require(bool condition, const std::string& message) { if (!condition) invalid(message); }
double finite(double value) { require(std::isfinite(value) && std::abs(value)<=1e9, "Artifact coordinate must be finite and within 1e9 mm"); return value; }
double real(const std::string& text) {
  double value{};
  require(parse_decimal(text,value),"Malformed numeric value"); return finite(value);
}
std::size_t integer(const std::string& text, std::size_t maximum) {
  std::uint64_t value{}; const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
  require(parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size() && value<=maximum,"Malformed or out-of-range integer"); return static_cast<std::size_t>(value);
}
std::vector<double> numbers(const std::string& text, std::size_t count) {
  std::istringstream input(text); input.imbue(std::locale::classic()); std::string token; std::vector<double> values;
  while(input>>token) { require(values.size()<count,"Too many numeric components"); values.push_back(real(token)); }
  require(values.size()==count,"Wrong number of numeric components"); return values;
}
double unit_scale(const std::string& unit) {
  if(unit=="mm" || unit=="millimeter") return 1;
  if(unit=="cm" || unit=="centimeter") return 10;
  if(unit=="m" || unit=="meter") return 1000;
  if(unit=="in" || unit=="inch") return 25.4;
  if(unit=="ft" || unit=="foot") return 304.8;
  if(unit=="um" || unit=="micron") return .001;
  invalid("Unknown explicit artifact units");
}
std::uint32_t u32(const std::string& bytes,std::size_t offset) {
  require(offset<=bytes.size() && bytes.size()-offset>=4,"Truncated binary field");
  return static_cast<unsigned char>(bytes[offset]) | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset+1]))<<8 |
    static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset+2]))<<16 | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset+3]))<<24;
}
std::uint16_t u16(const std::string& bytes,std::size_t offset) {
  require(offset<=bytes.size() && bytes.size()-offset>=2,"Truncated binary field");
  return static_cast<unsigned char>(bytes[offset]) | static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[offset+1]))<<8;
}
std::uint32_t crc32(const std::string& bytes) {
  std::uint32_t crc=0xffffffff;
  for(std::size_t n=0;n<bytes.size();++n) { if(n%65536==0) check_job_cancelled(); crc^=static_cast<unsigned char>(bytes[n]);
    for(int bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u))); }
  return crc^0xffffffff;
}
std::string relative_uri(const std::string& uri,bool directory) {
  require(!uri.empty() && uri.size()<=240 && !invalid_utf8_offset(uri),"Invalid relative artifact URI");
  require(uri.front()!='/' && uri.find_first_of("\\:%?#\0") == std::string::npos,"Artifact references must be plain contained relative paths");
  require(std::none_of(uri.begin(),uri.end(),[](unsigned char c){return c<32 || c==127;}),"Control character in artifact path");
  std::size_t begin=0;
  while(begin<uri.size()) { auto end=uri.find('/',begin); if(end==std::string::npos) end=uri.size();
    const auto part=uri.substr(begin,end-begin); require(!part.empty() && part!="." && part!=".." && part.back()!='.' && part.back()!=' ',"Unsafe artifact path component");
    const auto stem=part.substr(0,part.find('.')); auto lower=stem; for(auto& c:lower)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    require(lower!="con"&&lower!="prn"&&lower!="aux"&&lower!="nul" && !(lower.size()==4 && (lower.starts_with("com")||lower.starts_with("lpt")) && std::isdigit(static_cast<unsigned char>(lower[3]))),"Nonportable artifact path component");
    begin=end+1;
  }
  require(directory || uri.back()!='/',"Unexpected directory reference"); return uri;
}
Mat identity() { return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; }
Mat product(const Mat&a,const Mat&b) { Mat c{}; for(int r=0;r<4;++r)for(int k=0;k<4;++k)for(int j=0;j<4;++j)c[r*4+j]+=a[r*4+k]*b[k*4+j]; affine(c); return c; }
Mat translation(Vec v) { auto m=identity(); for(int i=0;i<3;++i)m[i*4+3]=finite(v[i]);return m; }
Mat pose(const std::vector<double>& v,double scale) {
  require(v.size()==6,"Pose needs xyz and roll/pitch/yaw"); const double cr=std::cos(v[3]),sr=std::sin(v[3]),cp=std::cos(v[4]),sp=std::sin(v[4]),cy=std::cos(v[5]),sy=std::sin(v[5]);
  Mat m={cy*cp,cy*sp*sr-sy*cr,cy*sp*cr+sy*sr,v[0]*scale,sy*cp,sy*sp*sr+cy*cr,sy*sp*cr-cy*sr,v[1]*scale,-sp,cp*sr,cp*cr,v[2]*scale,0,0,0,1}; affine(m);return m;
}
double determinant(const Mat&m) { return m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]); }
void affine(const Mat&m) { for(auto value:m)finite(value); require(m[12]==0&&m[13]==0&&m[14]==0&&m[15]==1 && std::abs(determinant(m))>1e-15,"Singular or non-affine transform"); }
Vec transform(const Mat&m,Vec p) { Vec result{};for(int r=0;r<3;++r)result[r]=finite(m[r*4]*p[0]+m[r*4+1]*p[1]+m[r*4+2]*p[2]+m[r*4+3]);return result; }
void safe_xml(tinyxml2::XMLDocument& document,const std::string& bytes) {
  require(bytes.size()<=16*1024*1024 && bytes.find('\0')==std::string::npos && !invalid_utf8_offset(bytes),"XML must be bounded, NUL-free UTF-8");
  require(std::none_of(bytes.begin(),bytes.end(),[](unsigned char c){return c<32&&c!=9&&c!=10&&c!=13;}),"Invalid XML control character");
  if(bytes.find("<!DOCTYPE")!=std::string::npos || bytes.find("<!ENTITY")!=std::string::npos) unsupported("DOCTYPE and entity declarations are prohibited");
  // Bound lexical input before the third-party parser, and prevent numeric
  // character-reference integer overflow present in upstream release 11.0.0.
  std::size_t tags=0;
  for(std::size_t i=0;i<bytes.size();++i) { if(i%65536==0)check_job_cancelled();
    if(bytes.compare(i,4,"<!--")==0){const auto end=bytes.find("-->",i+4);require(end!=std::string::npos,"Unterminated XML comment");i=end+2;continue;}
    if(bytes.compare(i,9,"<![CDATA[")==0){const auto end=bytes.find("]]>",i+9);require(end!=std::string::npos,"Unterminated XML CDATA");i=end+2;continue;}
    if(bytes[i]=='<') require(++tags<=100000,"XML lexical-node budget exceeded");
    if(bytes[i]!='&')continue;
    const auto end=bytes.find(';',i);require(end!=std::string::npos && end-i<=16,"Malformed or oversized XML entity reference");
    const auto entity=bytes.substr(i+1,end-i-1);
    if(entity.starts_with('#')) { std::uint32_t code=0;std::size_t p=1;int base=10;if(p<entity.size()&&entity[p]=='x'){base=16;++p;}
      require(p<entity.size(),"Empty XML character reference");for(;p<entity.size();++p){const unsigned char c=entity[p];unsigned digit=99;
        if(c>='0'&&c<='9')digit=c-'0';else if(base==16&&c>='a'&&c<='f')digit=c-'a'+10;else if(base==16&&c>='A'&&c<='F')digit=c-'A'+10;
        require(digit<static_cast<unsigned>(base) && code<=(0x10ffffu-digit)/static_cast<unsigned>(base),"Invalid or overflowing XML character reference");code=code*base+digit;}
      require(code==9||code==10||code==13||(code>=0x20&&code<=0xd7ff)||(code>=0xe000&&code<=0xfffd)||(code>=0x10000&&code<=0x10ffff),"Invalid XML Unicode character reference");
    } else require(entity=="amp"||entity=="lt"||entity=="gt"||entity=="apos"||entity=="quot","Unknown XML entity reference");
    i=end;
  }
  require(document.Parse(bytes.data(),bytes.size())==tinyxml2::XML_SUCCESS && document.RootElement(),"Malformed XML artifact");
  std::size_t count=0;
  std::function<void(const tinyxml2::XMLNode*,std::size_t)> walk=[&](auto node,std::size_t depth){require(depth<=64&&++count<=100000,"XML tree budget exceeded");
    if(auto element=node->ToElement())for(auto a=element->FirstAttribute();a;a=a->Next())require(std::string_view(a->Value()).size()<=4096,"XML attribute exceeds 4096 bytes");
    for(auto child=node->FirstChild();child;child=child->NextSibling())walk(child,depth+1);};walk(&document,0);
}
std::string tag(const tinyxml2::XMLElement*e) { require(e,"Missing XML element");const std::string name=e->Name();return name.substr(name.find(':')==std::string::npos?0:name.find(':')+1); }
std::string attribute(const tinyxml2::XMLElement*e,const char*key,const std::string&fallback) {require(e,"Missing XML element");const auto p=e->Attribute(key);return p?p:fallback;}
void children(const tinyxml2::XMLElement*e,const std::set<std::string>&allowed) {for(auto child=e->FirstChildElement();child;child=child->NextSiblingElement())if(!allowed.contains(tag(child)))unsupported("Unsupported XML element: "+tag(child));}
void Geometry::mesh(const std::vector<Vec>&v,const std::vector<std::array<std::size_t,3>>&t,const Mat&m,const std::string&source,Json metadata) {
  check_job_cancelled();affine(m);require(!v.empty()&&!t.empty(),"Empty mesh primitive");
  require(v.size()<=artifact_geometry_limit-positions.size()&&t.size()<=artifact_geometry_limit-triangles.size()&&groups.size()<10000,"Artifact geometry budget exceeded");
  const auto offset=positions.size();const auto group="artifact-"+std::to_string(groups.size()+1);
  metadata["id"]=group;metadata["source_ref"]=source;metadata["editable"]=false;groups.push_back(metadata);
  for(std::size_t i=0;i<v.size();++i){if(i%4096==0)check_job_cancelled();positions.push_back(transform(m,v[i]));}
  for(auto tri:t){require(tri[0]<v.size()&&tri[1]<v.size()&&tri[2]<v.size(),"Triangle index exceeds vertex count");
    require(tri[0]!=tri[1]&&tri[1]!=tri[2]&&tri[0]!=tri[2],"Triangle repeats a vertex index");
    const auto a=transform(m,v[tri[0]]),b=transform(m,v[tri[1]]),c=transform(m,v[tri[2]]);Vec u{},w{};for(int i=0;i<3;++i)u[i]=b[i]-a[i],w[i]=c[i]-a[i];
    const Vec cross={u[1]*w[2]-u[2]*w[1],u[2]*w[0]-u[0]*w[2],u[0]*w[1]-u[1]*w[0]};
    require(cross[0]*cross[0]+cross[1]*cross[1]+cross[2]*cross[2]>1e-24,"Degenerate artifact triangle");if(determinant(m)<0)std::swap(tri[1],tri[2]);
    triangles.push_back({tri[0]+offset,tri[1]+offset,tri[2]+offset});triangle_groups.push_back(group);}
}
void Geometry::line(const std::vector<Vec>&v,const std::string&source,Json metadata) {
  check_job_cancelled();require(v.size()>=2&&polylines.size()<10000,"Invalid or excessive curve count");
  require(v.size()<=artifact_geometry_limit-curve_points,"Curve point budget exceeded");curve_points+=v.size();Json points=Json::array();for(auto p:v){for(auto c:p)finite(c);points.push_back(p);}
  metadata["id"]="curve-"+std::to_string(polylines.size()+1);metadata["source_ref"]=source;metadata["editable"]=false;metadata["points"]=points;polylines.push_back(metadata);
}
Json Geometry::result() const {return {{"positions",positions},{"triangles",triangles},{"triangle_groups",triangle_groups},{"groups",groups},{"polylines",polylines}};}
Json Geometry::summary(const std::string&representation)const {
  Vec low{INFINITY,INFINITY,INFINITY},high{-INFINITY,-INFINITY,-INFINITY};bool have=false;const auto add=[&](const Json&p){have=true;for(int i=0;i<3;++i){const double c=p.at(i);low[i]=std::min(low[i],c);high[i]=std::max(high[i],c);}};
  for(const auto&p:positions)add(p);for(const auto&line:polylines)for(const auto&p:line.at("points"))add(p);
  return {{"representation",representation},{"units","mm"},{"vertices",positions.size()},{"triangles",triangles.size()},{"curves",polylines.size()},{"groups",groups.size()},{"bounds",have?Json{{"min",low},{"max",high}}:Json(nullptr)},{"editable",false}};
}
}
