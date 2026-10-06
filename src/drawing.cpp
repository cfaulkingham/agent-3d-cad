#include "agentcad/drawing.hpp"
#include "agentcad/model.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <numbers>
#include <set>
#include <sstream>
#include <vector>

namespace agentcad {
namespace {
constexpr double pi = std::numbers::pi;
constexpr double attachment_tolerance = 0.02;
constexpr double identical_tolerance = 1e-7;
struct Point { double x{}, y{}; };
Point operator+(Point a, Point b) { return {a.x+b.x,a.y+b.y}; }
Point operator-(Point a, Point b) { return {a.x-b.x,a.y-b.y}; }
Point operator*(Point a, double b) { return {a.x*b,a.y*b}; }
double dot(Point a, Point b) { return a.x*b.x+a.y*b.y; }
double distance(Point a, Point b) { return std::hypot(a.x-b.x,a.y-b.y); }
double projected_number(const Json& v) {
  if(!v.is_number()) throw Error("invalid_argument","Projected coordinates must be numeric");
  const auto value=v.get<double>();
  if(!std::isfinite(value) || std::abs(value)>1e9)
    throw Error("invalid_argument","Projected numbers must be finite and within +/-1000000000");
  return value;
}
Point point(const Json& v) {
  if(!v.is_array() || v.size()!=2) throw Error("invalid_argument","Drawing coordinates require two numbers");
  return {projected_number(v[0]),projected_number(v[1])};
}
Json point_json(Point p) { return Json::array({p.x,p.y}); }
std::string numeric(double n) {
  if(std::abs(n)<0.00000005) n=0;
  std::ostringstream out; out.imbue(std::locale::classic()); out<<std::fixed<<std::setprecision(6)<<n;
  auto s=out.str(); while(s.find('.')!=std::string::npos && s.back()=='0') s.pop_back();
  if(s.back()=='.') s.pop_back(); return s;
}
std::string dimension_numeric(double n) {
  if(n>0 && n<0.0005) return "<0.001";
  std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(3)<<n;
  auto s=out.str();while(s.find('.')!=std::string::npos && s.back()=='0') s.pop_back();
  if(s.back()=='.') s.pop_back();return s;
}
[[noreturn]] void invalid(const std::string& message, Json details=Json::object()) {
  throw Error("invalid_drawing",message,std::move(details));
}
std::string printable(const Json& value, const std::string& field, std::size_t limit) {
  if(!value.is_string()) throw Error("invalid_argument",field+" must be a string");
  const auto s=value.get<std::string>();
  if(s.empty() || s.size()>limit) invalid(field+" has an invalid length");
  for(unsigned char c:s) if(c<32 || c>126) invalid(field+" must contain printable ASCII only");
  return s;
}
void view_identifier(const std::string& id) {
  identifier(id);
  std::string upper=id;
  for(auto& c:upper) if(c>='a' && c<='z') c=static_cast<char>(c-'a'+'A');
  if(upper=="CON" || upper=="PRN" || upper=="AUX" || upper=="NUL" ||
      (upper.size()==4 && (upper.starts_with("COM") || upper.starts_with("LPT")) && upper[3]>='0' && upper[3]<='9'))
    invalid("View IDs must be portable filenames",{{"view",id}});
}
Json closed(Json properties, Json required=Json::array()) {
  return {{"type","object"},{"properties",std::move(properties)},{"required",std::move(required)},{"additionalProperties",false}};
}
Json resolved_point(const Json& value, const Json& parameters) {
  if(!value.is_array() || value.size()!=2) throw Error("invalid_argument","Drawing coordinates require two scalars");
  return Json::array({scalar(value[0],parameters),scalar(value[1],parameters)});
}
std::string xml(const std::string& s) {
  std::string out;
  for(char c:s) switch(c) { case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;
    case '"':out+="&quot;";break;case '\'':out+="&apos;";break;default:out+=c; }
  return out;
}
std::string pdf_string(const std::string& s) {
  std::string out;
  for(char c:s) { if(c=='(' || c==')' || c=='\\') out+='\\'; out+=c; }
  return out;
}
// Conservative Helvetica/Arial advance bounds. Used for wrapping and placement,
// never to infer manufacturing precision or modify a measured dimension.
double text_width(const std::string& s, double size) {
  double w=0;
  for(char c:s) {
    double advance=0.62;
    if(c==' ' || c=='.' || c==',' || c==':' || c==';' || c=='!' || c=='\'' || c=='i' || c=='l') advance=0.31;
    else if(c=='W' || c=='@' || c=='%' || c=='M' || c=='m') advance=1.05;
    else if(c>='A' && c<='Z') advance=0.80;
    else if(c=='w' || c=='&') advance=0.85;
    w+=advance*size;
  }
  return w;
}
std::vector<std::string> wrap(const std::string& text,double size,double width) {
  std::vector<std::string> lines; std::string current;
  for(char c:text) {
    if(!current.empty() && text_width(current+c,size)>width) {
      const auto space=current.find_last_of(' ');
      if(space!=std::string::npos && space>0) {
        lines.push_back(current.substr(0,space)); current=current.substr(space+1);
      } else { lines.push_back(current); current.clear(); }
    }
    current+=c;
  }
  if(!current.empty()) lines.push_back(current);
  return lines;
}

struct Entity {
  std::string kind; std::vector<Point> points; Point center;
  double radius{},start{},end{}; bool hidden{};
};
struct View {
  std::string id,orientation; std::array<double,4> bounds{}; std::vector<Entity> entities;
};
Point circle_point(Point c,double r,double degrees) {
  const auto a=degrees*pi/180; return c+Point{r*std::cos(a),r*std::sin(a)};
}
std::vector<View> projections(const Json& projected,const Json& spec) {
  if(!projected.is_object() || !projected.contains("views") || !projected["views"].is_array())
    invalid("Kernel did not supply drawing views");
  if(projected["views"].size()!=spec["views"].size()) invalid("Projected view count differs from recipe");
  std::vector<View> views; std::size_t count=0,points=0;
  for(const auto& requested:spec["views"]) {
    const auto id=requested["id"].get<std::string>();
    const Json* source=nullptr;
    for(const auto& candidate:projected["views"]) if(candidate.value("id",std::string{})==id) {
      if(source) invalid("Kernel supplied duplicate projected view",{{"view",id}}); source=&candidate;
    }
    if(!source) invalid("Kernel omitted requested view",{{"view",id}});
    View v; v.id=id; v.orientation=requested["orientation"].get<std::string>();
    if(source->value("orientation",std::string{})!=v.orientation) invalid("Projected orientation differs from recipe");
    const auto& bounds=source->at("bounds_mm");
    if(!bounds.is_array() || bounds.size()!=4) invalid("Invalid projected bounds");
    for(std::size_t i=0;i<4;++i) v.bounds[i]=projected_number(bounds[i]);
    if(v.bounds[2]<v.bounds[0] || v.bounds[3]<v.bounds[1]) invalid("Invalid projected bounds");
    const auto& entities=source->at("entities");
    if(!entities.is_array() || entities.empty()) invalid("Drawing view has no projected geometry",{{"view",id}});
    for(const auto& entry:entities) {
      if(++count>100000) throw Error("limit_exceeded","Drawing has too many projected entities");
      Entity e; e.kind=text_field(entry,"kind");
      if(!entry.contains("hidden") || !entry["hidden"].is_boolean()) invalid("Projected hidden flag is missing");
      e.hidden=entry["hidden"].get<bool>();
      if(e.kind=="line" || e.kind=="polyline") {
        const auto& p=entry.at("points");
        if(!p.is_array() || p.size()<2 || (e.kind=="line" && p.size()!=2)) invalid("Invalid projected line");
        points+=p.size(); if(points>400000) throw Error("limit_exceeded","Drawing has too many projected points");
        for(const auto& q:p) e.points.push_back(point(q));
      } else if(e.kind=="circle" || e.kind=="arc") {
        e.center=point(entry.at("center")); e.radius=projected_number(entry.at("radius"));
        if(e.radius<=0) invalid("Projected circle radius must be positive");
        if(e.kind=="arc") {
          e.start=number(entry.at("start_deg")); e.end=number(entry.at("end_deg"));
          if(e.end<=e.start || e.end-e.start>360+1e-6) invalid("Projected arc must be counterclockwise and at most one turn");
        }
      } else invalid("Unsupported projected entity",{{"kind",e.kind}});
      v.entities.push_back(std::move(e));
    }
    views.push_back(std::move(v));
  }
  return views;
}
struct Circle { Point center; double radius,angle; };
std::vector<Circle> circles(const View& view,bool full_only=false) {
  std::vector<Circle> result;
  for(const auto& e:view.entities) if(e.kind=="circle" || (!full_only && e.kind=="arc")) {
    if(std::none_of(result.begin(),result.end(),[&](const auto& c){return distance(c.center,e.center)<=identical_tolerance && std::abs(c.radius-e.radius)<=identical_tolerance;}))
      result.push_back({e.center,e.radius,e.kind=="circle"?45:(e.start+e.end)/2});
  }
  return result;
}
Point attach(const View& view,Point requested,const std::string& kind) {
  std::vector<Point> candidates;
  auto add=[&](Point p) {
    if(distance(requested,p)<=attachment_tolerance && std::none_of(candidates.begin(),candidates.end(),[&](Point old){return distance(old,p)<=identical_tolerance;})) candidates.push_back(p);
  };
  for(const auto& e:view.entities) {
    if(e.kind=="line" || e.kind=="polyline") {
      add(e.points.front()); add(e.points.back());
      if(e.kind=="line") {
        const auto delta=e.points[1]-e.points[0]; const auto length2=dot(delta,delta);
        if(length2>0) add(e.points[0]+delta*std::clamp(dot(requested-e.points[0],delta)/length2,0.0,1.0));
      }
    } else {
      if(e.kind=="circle") add(e.center);
      auto delta=requested-e.center; const auto length=distance(requested,e.center);
      if(length>identical_tolerance) {
        double angle=std::atan2(delta.y,delta.x)*180/pi;
        if(e.kind=="arc") {
          while(angle<e.start) angle+=360; while(angle>e.start+360) angle-=360;
        }
        if(e.kind=="circle" || angle<=e.end+1e-9) add(e.center+delta*(e.radius/length));
      }
      if(e.kind=="arc") { add(circle_point(e.center,e.radius,e.start)); add(circle_point(e.center,e.radius,e.end)); }
    }
  }
  if(candidates.empty()) throw Error("drawing_reference_not_found","Linear dimension point is not attached to projected geometry",{{"view",view.id},{"kind",kind},{"point",point_json(requested)},{"tolerance_mm",attachment_tolerance}});
  if(candidates.size()>1)
    throw Error("drawing_reference_ambiguous","Linear dimension point matches distinct projected geometry",{{"view",view.id},{"point",point_json(requested)},{"matches",candidates.size()}});
  return candidates[0];
}
struct Dimension {
  std::string view,kind,label; double value{},radius{},angle{}; Point from,to,center;
};
std::vector<Dimension> dimensions(const std::vector<View>& views,const Json& spec) {
  std::vector<Dimension> result;
  for(const auto& requested:spec["dimensions"]) {
    Dimension d; d.view=requested["view"].get<std::string>(); d.kind=requested["kind"].get<std::string>();
    const auto& v=*std::find_if(views.begin(),views.end(),[&](const auto& view){return view.id==d.view;});
    if(d.kind=="width") { d.from={v.bounds[0],v.bounds[1]}; d.to={v.bounds[2],v.bounds[1]}; d.value=v.bounds[2]-v.bounds[0]; }
    else if(d.kind=="height") { d.from={v.bounds[2],v.bounds[1]};d.to={v.bounds[2],v.bounds[3]};d.value=v.bounds[3]-v.bounds[1]; }
    else if(d.kind=="horizontal" || d.kind=="vertical") {
      d.from=attach(v,point(requested["from"]),d.kind); d.to=attach(v,point(requested["to"]),d.kind);
      d.value=d.kind=="horizontal"?std::abs(d.to.x-d.from.x):std::abs(d.to.y-d.from.y);
    } else {
      const auto center=point(requested["center"]); const auto radius=number(requested["radius"]),tolerance=number(requested["tolerance"]);
      std::vector<Circle> matches;
      for(const auto& c:circles(v)) if(distance(c.center,center)<=tolerance && std::abs(c.radius-radius)<=tolerance) matches.push_back(c);
      if(matches.empty()) throw Error("drawing_reference_not_found","Radial dimension does not match a projected circle or arc",{{"view",d.view},{"kind",d.kind},{"tolerance_mm",tolerance}});
      if(matches.size()!=1) throw Error("drawing_reference_ambiguous","Radial dimension matches more than one distinct projected circle",{{"view",d.view},{"kind",d.kind},{"matches",matches.size()}});
      d.center=matches[0].center; d.radius=matches[0].radius; d.angle=matches[0].angle;
      d.value=d.radius*(d.kind=="diameter"?2:1);
    }
    if(d.value<=identical_tolerance) invalid("Drawing dimensions must have a nonzero measured span",{{"view",d.view},{"kind",d.kind}});
    d.label=(d.kind=="diameter"?"DIA ":d.kind=="radius"?"R ":"")+dimension_numeric(d.value);
    result.push_back(std::move(d));
  }
  return result;
}

struct Primitive {
  std::string kind,layer="VISIBLE",text; std::vector<Point> points; Point center;
  double radius{},start{},end{},font=3,angle{};
};
using Scene=std::vector<Primitive>;
void line(Scene& s,Point a,Point b,const std::string& layer="DIMENSIONS") {
  Primitive p;p.kind="line";p.points={a,b};p.layer=layer;s.push_back(std::move(p));
}
void label(Scene& s,Point p,const std::string& text,double size=3,double angle=0) {
  Primitive item;item.kind="text";item.layer="DIMENSIONS";item.center=p;item.text=text;item.font=size;item.angle=angle;s.push_back(std::move(item));
}
void rectangle(Scene& s,double x,double y,double w,double h) {
  line(s,{x,y},{x+w,y});line(s,{x+w,y},{x+w,y+h});line(s,{x+w,y+h},{x,y+h});line(s,{x,y+h},{x,y});
}
void arrow(Scene& s,Point tip,Point toward) {
  const auto delta=toward-tip; const auto length=std::hypot(delta.x,delta.y); if(length<1e-9) return;
  const auto d=delta*(1/length); const Point perpendicular{-d.y,d.x};
  line(s,tip,tip+d*2+perpendicular*.65);line(s,tip,tip+d*2-perpendicular*.65);
}
struct Placement { double x,y,width,height,gx,gy,gw,gh,scale; Point origin;
  Point map(Point p) const { return {origin.x+p.x*scale,origin.y-p.y*scale}; }
  Point unmap(Point p) const { return {(p.x-origin.x)/scale,(origin.y-p.y)/scale}; }
};
void geometry(Scene& scene,const View& v,const Placement& layout,bool hidden) {
  for(const auto& e:v.entities) {
    if(e.hidden && !hidden) continue;
    Primitive p;p.kind=e.kind;p.layer=e.hidden?"HIDDEN":"VISIBLE";
    for(auto q:e.points) p.points.push_back(layout.map(q));
    if(e.kind=="circle" || e.kind=="arc") { p.center=layout.map(e.center);p.radius=e.radius*layout.scale;p.start=e.start;p.end=e.end; }
    scene.push_back(std::move(p));
  }
  for(const auto& c:circles(v,true)) {
    const auto p=layout.map(c.center); const auto r=std::clamp(c.radius*layout.scale*.18,1.0,2.5);
    line(scene,{p.x-r,p.y},{p.x+r,p.y},"CENTER");line(scene,{p.x,p.y-r},{p.x,p.y+r},"CENTER");
  }
}
void annotate(Scene& s,const View& v,const Placement& p,const std::vector<Dimension>& dimensions) {
  int horizontal=0,vertical=0,radial=0;
  const double font=2.7;
  for(const auto& d:dimensions) if(d.view==v.id) {
    const auto a=p.map(d.from),b=p.map(d.to);
    if(d.kind=="width" || d.kind=="horizontal") {
      const auto y=p.gy+p.gh+5+horizontal++*6.5;
      line(s,{a.x,a.y+1},{a.x,y+1});line(s,{b.x,b.y+1},{b.x,y+1});line(s,{a.x,y},{b.x,y});
      arrow(s,{a.x,y},{b.x,y});arrow(s,{b.x,y},{a.x,y});
      const auto w=text_width(d.label,font);
      const auto x=std::clamp((a.x+b.x-w)/2,p.x+2,p.x+p.width-w-2);
      label(s,{x,y-1},d.label,font);
    } else if(d.kind=="height" || d.kind=="vertical") {
      const auto x=p.gx+p.gw+5+vertical++*6.5;
      line(s,{a.x+1,a.y},{x+1,a.y});line(s,{b.x+1,b.y},{x+1,b.y});line(s,{x,a.y},{x,b.y});
      arrow(s,{x,a.y},{x,b.y});arrow(s,{x,b.y},{x,a.y});
      const auto w=text_width(d.label,font);
      const auto y=std::clamp((a.y+b.y+w)/2,p.y+10+w,p.y+p.height-2);
      label(s,{x-1,y},d.label,font,-90);
    } else {
      const auto tip=p.map(circle_point(d.center,d.radius,d.angle));
      const auto baseline=p.y+12+radial++*5;
      const auto w=text_width(d.label,font);
      const Point end{p.x+3+w,baseline+1};
      line(s,tip,end);line(s,{p.x+3,baseline+1},end);arrow(s,tip,end);
      label(s,{p.x+3,baseline},d.label,font);
    }
  }
}
std::string svg(const Scene& scene,double width,double height) {
  std::ostringstream out;out.imbue(std::locale::classic());
  out<<"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""<<numeric(width)<<"mm\" height=\""<<numeric(height)<<"mm\" viewBox=\"0 0 "<<numeric(width)<<' '<<numeric(height)<<"\">\n<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
  for(const auto& p:scene) {
    if(p.kind=="text") {
      out<<"<text x=\""<<numeric(p.center.x)<<"\" y=\""<<numeric(p.center.y)<<"\" font-family=\"Helvetica,Arial,sans-serif\" font-size=\""<<numeric(p.font)<<"\"";
      if(p.angle) out<<" transform=\"rotate("<<numeric(p.angle)<<' '<<numeric(p.center.x)<<' '<<numeric(p.center.y)<<")\"";
      out<<" fill=\"#111\">"<<xml(p.text)<<"</text>\n";continue;
    }
    std::string attrs=" fill=\"none\" stroke=\""+std::string(p.layer=="VISIBLE"?"#111":"#555")+"\" stroke-width=\""+(p.layer=="VISIBLE"?"0.3":"0.18")+"\"";
    if(p.layer=="HIDDEN") attrs+=" stroke-dasharray=\"2 1\"";
    if(p.layer=="CENTER") attrs+=" stroke-dasharray=\"3 0.8 0.5 0.8\"";
    if(p.kind=="line" || p.kind=="polyline") {
      out<<"<polyline points=\"";for(const auto& q:p.points) out<<numeric(q.x)<<','<<numeric(q.y)<<' ';out<<'"'<<attrs<<"/>\n";
    } else if(p.kind=="circle") out<<"<circle cx=\""<<numeric(p.center.x)<<"\" cy=\""<<numeric(p.center.y)<<"\" r=\""<<numeric(p.radius)<<'"'<<attrs<<"/>\n";
    else {
      auto physical=[&](double a){return Point{p.center.x+p.radius*std::cos(a*pi/180),p.center.y-p.radius*std::sin(a*pi/180)};};
      const int segments=std::max(1,static_cast<int>(std::ceil((p.end-p.start)/180)));
      const auto first=physical(p.start);out<<"<path d=\"M "<<numeric(first.x)<<' '<<numeric(first.y);
      for(int i=1;i<=segments;++i) {const auto end=physical(p.start+(p.end-p.start)*i/segments);out<<" A "<<numeric(p.radius)<<' '<<numeric(p.radius)<<" 0 0 0 "<<numeric(end.x)<<' '<<numeric(end.y);}
      out<<'"'<<attrs<<"/>\n";
    }
  }
  out<<"</svg>\n";return out.str();
}
std::string pdf(const Scene& scene,double width,double height) {
  constexpr double unit=72/25.4;
  std::ostringstream content;content.imbue(std::locale::classic());
  auto xy=[&](Point p){return numeric(p.x*unit)+" "+numeric((height-p.y)*unit);};
  for(const auto& p:scene) {
    if(p.kind=="text") {
      const auto a=-p.angle*pi/180;
      content<<"BT /F1 "<<numeric(p.font*unit)<<" Tf 0 g "<<numeric(std::cos(a))<<' '<<numeric(std::sin(a))<<' '<<numeric(-std::sin(a))<<' '<<numeric(std::cos(a))<<' '<<xy(p.center)<<" Tm ("<<pdf_string(p.text)<<") Tj ET\n";continue;
    }
    content<<(p.layer=="VISIBLE"?"0 G ":"0.3 G ")<<numeric((p.layer=="VISIBLE"?.3:.18)*unit)<<" w ";
    if(p.layer=="HIDDEN") content<<'['<<numeric(2*unit)<<' '<<numeric(unit)<<"] 0 d\n";
    else if(p.layer=="CENTER") content<<'['<<numeric(3*unit)<<' '<<numeric(.8*unit)<<' '<<numeric(.5*unit)<<' '<<numeric(.8*unit)<<"] 0 d\n";
    else content<<"[] 0 d\n";
    if(p.kind=="line" || p.kind=="polyline") {content<<xy(p.points.front())<<" m\n";for(std::size_t i=1;i<p.points.size();++i) content<<xy(p.points[i])<<" l\n";}
    else {
      const double start=p.kind=="circle"?0:p.start,end=p.kind=="circle"?360:p.end;
      const int count=std::max(1,static_cast<int>(std::ceil((end-start)/45)));
      auto at=[&](double a){return Point{p.center.x+p.radius*std::cos(a),p.center.y-p.radius*std::sin(a)};};
      content<<xy(at(start*pi/180))<<" m\n";
      for(int i=0;i<count;++i) {
        const auto a=(start+(end-start)*i/count)*pi/180,b=(start+(end-start)*(i+1)/count)*pi/180,k=4.0/3*std::tan((b-a)/4);
        const auto first=at(a),last=at(b);const Point tangent_a{-p.radius*std::sin(a),-p.radius*std::cos(a)},tangent_b{-p.radius*std::sin(b),-p.radius*std::cos(b)};
        content<<xy(first+tangent_a*k)<<' '<<xy(last-tangent_b*k)<<' '<<xy(last)<<" c\n";
      }
      if(p.kind=="circle") content<<"h\n";
    }
    content<<"S\n";
  }
  const auto stream=content.str();
  const std::vector<std::string> objects={
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "+numeric(width*unit)+" "+numeric(height*unit)+"] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
    "<< /Length "+std::to_string(stream.size())+" >>\nstream\n"+stream+"endstream"};
  std::string output="%PDF-1.4\n% Native agent-3d-cad vector drawing\n";std::vector<std::size_t> offsets={0};
  for(std::size_t i=0;i<objects.size();++i) {offsets.push_back(output.size());output+=std::to_string(i+1)+" 0 obj\n"+objects[i]+"\nendobj\n";}
  const auto start=output.size();output+="xref\n0 "+std::to_string(objects.size()+1)+"\n0000000000 65535 f \n";
  for(std::size_t i=1;i<offsets.size();++i) {std::ostringstream row;row.imbue(std::locale::classic());row<<std::setw(10)<<std::setfill('0')<<offsets[i]<<" 00000 n \n";output+=row.str();}
  output+="trailer\n<< /Size "+std::to_string(objects.size()+1)+" /Root 1 0 R >>\nstartxref\n"+std::to_string(start)+"\n%%EOF\n";return output;
}
std::string dxf(const View& view,const Scene& annotations,const Placement& placement,bool hidden) {
  std::ostringstream out;out.imbue(std::locale::classic());
  auto pair=[&](int code,const auto& value){out<<code<<'\n'<<value<<'\n';};
  auto number_pair=[&](int code,double value){pair(code,numeric(value));};
  std::size_t next_handle=0x20;
  auto hex=[](std::size_t value){std::ostringstream s;s.imbue(std::locale::classic());s<<std::uppercase<<std::hex<<value;return s.str();};
  auto record=[&](const std::string& owner){pair(5,hex(next_handle++));pair(330,owner);};
  auto table=[&](const std::string& name,const std::string& handle,int entries){pair(0,"TABLE");pair(2,name);pair(5,handle);pair(330,"0");pair(100,"AcDbSymbolTable");pair(70,entries);};
  pair(0,"SECTION");pair(2,"HEADER");pair(9,"$ACADVER");pair(1,"AC1015");pair(9,"$HANDSEED");pair(5,hex(view.entities.size()+annotations.size()+1000));pair(9,"$INSUNITS");pair(70,4);pair(9,"$MEASUREMENT");pair(70,1);pair(0,"ENDSEC");
  pair(0,"SECTION");pair(2,"TABLES");table("LTYPE","10",3);
  for(const auto& type:std::vector<std::string>{"CONTINUOUS","HIDDEN","CENTER"}) {
    pair(0,"LTYPE");record("10");pair(100,"AcDbSymbolTableRecord");pair(100,"AcDbLinetypeTableRecord");pair(2,type);pair(70,0);pair(3,type);pair(72,65);
    if(type=="CONTINUOUS") {pair(73,0);pair(40,0);}
    else if(type=="HIDDEN") {pair(73,2);pair(40,3);pair(49,2);pair(74,0);pair(49,-1);pair(74,0);}
    else {pair(73,4);pair(40,5);pair(49,3);pair(74,0);pair(49,-.75);pair(74,0);pair(49,.5);pair(74,0);pair(49,-.75);pair(74,0);}
  }
  pair(0,"ENDTAB");table("LAYER","11",5);
  for(const auto& layer:std::vector<std::string>{"0","VISIBLE","HIDDEN","CENTER","DIMENSIONS"}) {pair(0,"LAYER");record("11");pair(100,"AcDbSymbolTableRecord");pair(100,"AcDbLayerTableRecord");pair(2,layer);pair(70,0);pair(62,layer=="HIDDEN"?8:7);pair(6,layer=="HIDDEN"?"HIDDEN":layer=="CENTER"?"CENTER":"CONTINUOUS");}
  pair(0,"ENDTAB");table("STYLE","12",1);pair(0,"STYLE");record("12");pair(100,"AcDbSymbolTableRecord");pair(100,"AcDbTextStyleTableRecord");pair(2,"STANDARD");pair(70,0);pair(40,0);pair(41,1);pair(50,0);pair(71,0);pair(42,2.5);pair(3,"txt");pair(4,"");pair(0,"ENDTAB");
  table("BLOCK_RECORD","13",2);
  for(const auto& space:std::vector<std::pair<std::string,std::string>>{{"*Model_Space","14"},{"*Paper_Space","15"}}) {
    pair(0,"BLOCK_RECORD");pair(5,space.second);pair(330,"13");pair(100,"AcDbSymbolTableRecord");pair(100,"AcDbBlockTableRecord");pair(2,space.first);pair(70,0);
  }
  pair(0,"ENDTAB");pair(0,"ENDSEC");pair(0,"SECTION");pair(2,"BLOCKS");
  for(const auto& space:std::vector<std::pair<std::string,std::string>>{{"*Model_Space","14"},{"*Paper_Space","15"}}) {
    pair(0,"BLOCK");record(space.second);pair(100,"AcDbEntity");pair(8,"0");pair(100,"AcDbBlockBegin");pair(2,space.first);pair(70,0);pair(10,0);pair(20,0);pair(30,0);pair(3,space.first);pair(1,"");
    pair(0,"ENDBLK");record(space.second);pair(100,"AcDbEntity");pair(8,"0");pair(100,"AcDbBlockEnd");
  }
  pair(0,"ENDSEC");pair(0,"SECTION");pair(2,"ENTITIES");
  auto entity_start=[&](const std::string& kind,const std::string& layer){pair(0,kind);record("14");pair(100,"AcDbEntity");pair(8,layer);};
  auto dxf_line=[&](Point a,Point b,const std::string& layer){entity_start("LINE",layer);pair(100,"AcDbLine");number_pair(10,a.x);number_pair(20,a.y);pair(30,0);number_pair(11,b.x);number_pair(21,b.y);pair(31,0);};
  for(const auto& e:view.entities) {
    if(e.hidden && !hidden) continue;const auto layer=e.hidden?"HIDDEN":"VISIBLE";
    if(e.kind=="line") dxf_line(e.points[0],e.points[1],layer);
    else if(e.kind=="polyline") {entity_start("LWPOLYLINE",layer);pair(100,"AcDbPolyline");pair(90,e.points.size());pair(70,0);for(const auto& p:e.points){number_pair(10,p.x);number_pair(20,p.y);}}
    else {
      const bool complete=e.kind=="circle" || std::abs(e.end-e.start-360)<=1e-8;
      entity_start(complete?"CIRCLE":"ARC",layer);pair(100,"AcDbCircle");number_pair(10,e.center.x);number_pair(20,e.center.y);pair(30,0);number_pair(40,e.radius);
      if(!complete) {auto angle=[](double value){value=std::fmod(value,360);return value<0?value+360:value;};pair(100,"AcDbArc");number_pair(50,angle(e.start));number_pair(51,angle(e.end));}
    }
  }
  for(const auto& p:annotations) {
    if(p.kind=="line") dxf_line(placement.unmap(p.points[0]),placement.unmap(p.points[1]),p.layer);
    else if(p.kind=="text") {entity_start("TEXT","DIMENSIONS");pair(100,"AcDbText");const auto at=placement.unmap(p.center);number_pair(10,at.x);number_pair(20,at.y);pair(30,0);number_pair(40,p.font/placement.scale);pair(1,p.text);number_pair(50,-p.angle);pair(7,"STANDARD");pair(100,"AcDbText");}
  }
  pair(0,"ENDSEC");pair(0,"EOF");return out.str();
}
}

Json drawing_schema() {
  const Json scalar_ref={{"$ref","#/$defs/scalar"}};
  const Json point_schema={{"type","array"},{"items",scalar_ref},{"minItems",2},{"maxItems",2}};
  const Json id={{"type","string"},{"pattern","^[A-Za-z][A-Za-z0-9_-]{0,63}$"}};
  auto text=[](int max){return Json{{"type","string"},{"pattern","^[ -~]+$"},{"minLength",1},{"maxLength",max}};};
  const auto section=closed({{"axis",{{"enum",{"x","y","z"}}}},{"offset",scalar_ref}},{"axis","offset"});
  const Json view={{"oneOf",Json::array({
    closed({{"id",id},{"orientation",{{"enum",{"top","front","right","isometric"}}}}},{"id","orientation"}),
    closed({{"id",id},{"orientation",{{"const","section"}}},{"section",section}},{"id","orientation","section"})})}};
  const Json dimension={{"oneOf",Json::array({
    closed({{"view",id},{"kind",{{"enum",{"width","height"}}}}},{"view","kind"}),
    closed({{"view",id},{"kind",{{"enum",{"radius","diameter"}}}},{"center",point_schema},{"radius",scalar_ref},{"tolerance",{{"type","number"},{"exclusiveMinimum",0},{"maximum",0.1}}}},{"view","kind","center","radius"}),
    closed({{"view",id},{"kind",{{"enum",{"horizontal","vertical"}}}},{"from",point_schema},{"to",point_schema}},{"view","kind","from","to"})})}};
  return closed({{"title",text(80)},{"sheet",{{"enum",{"A4","A3"}}}},{"scale",{{"type","number"},{"exclusiveMinimum",0},{"maximum",1e6}}},
    {"hidden_lines",{{"type","boolean"}}},{"formats",{{"type","array"},{"minItems",1},{"maxItems",3},{"uniqueItems",true},{"items",{{"enum",{"svg","pdf","dxf"}}}}}},
    {"views",{{"type","array"},{"minItems",1},{"maxItems",6},{"items",view}}},{"dimensions",{{"type","array"},{"maxItems",32},{"items",dimension}}},
    {"material",text(80)},{"notes",{{"type","array"},{"maxItems",6},{"items",text(120)}}}});
}

Json normalize_drawing(const Json& spec,const Json& model) {
  fields(spec,{}, {"title","sheet","scale","hidden_lines","formats","views","dimensions","material","notes"});
  const auto& parameters=model.at("parameters");
  Json result={{"sheet",spec.value("sheet",Json("A4"))},{"hidden_lines",spec.value("hidden_lines",Json(true))},
    {"formats",spec.value("formats",Json::array({"svg","pdf","dxf"}))},{"views",Json::array()},{"dimensions",Json::array()},{"notes",Json::array()}};
  if(!result["sheet"].is_string() || (result["sheet"]!="A4" && result["sheet"]!="A3")) invalid("Sheet must be A4 or A3 landscape");
  if(!result["hidden_lines"].is_boolean()) throw Error("invalid_argument","hidden_lines must be boolean");
  if(spec.contains("scale")) {const auto scale=number(spec["scale"]);if(scale<=0) invalid("Drawing scale must be positive");result["scale"]=scale;}
  for(const auto& field:{"title","material"}) if(spec.contains(field)) result[field]=printable(spec[field],field,80);
  const auto& formats=result["formats"];
  if(!formats.is_array() || formats.empty() || formats.size()>3) invalid("Request one to three drawing formats");
  std::set<std::string> format_set;
  for(const auto& format:formats) {if(!format.is_string()) invalid("Drawing format must be a string");const auto name=format.get<std::string>();if((name!="svg" && name!="pdf" && name!="dxf") || !format_set.insert(name).second) invalid("Drawing formats must be unique svg/pdf/dxf values");}
  const auto views=spec.value("views",Json::array({Json{{"id","top"},{"orientation","top"}},Json{{"id","front"},{"orientation","front"}},Json{{"id","right"},{"orientation","right"}},Json{{"id","iso"},{"orientation","isometric"}}}));
  if(!views.is_array() || views.empty() || views.size()>6) invalid("Request one to six drawing views");
  std::set<std::string> ids,portable_ids;
  for(const auto& view:views) {
    fields(view,{"id","orientation"},{"section"});const auto id=text_field(view,"id"),orientation=text_field(view,"orientation");view_identifier(id);
    auto portable=id;for(auto& c:portable) if(c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');
    if(!ids.insert(id).second || !portable_ids.insert(portable).second) invalid("View IDs must be unique, including case-insensitive filesystems");
    Json normalized={{"id",id},{"orientation",orientation}};
    if(orientation=="section") {
      if(!view.contains("section")) invalid("Section view requires a section plane");fields(view["section"],{"axis","offset"});
      const auto axis=text_field(view["section"],"axis");if(axis!="x" && axis!="y" && axis!="z") invalid("Section axis must be x, y or z");
      normalized["section"]={{"axis",axis},{"offset",scalar(view["section"]["offset"],parameters)}};
    } else {
      if(view.contains("section")) invalid("Only section views accept a section plane");
      if(orientation!="top" && orientation!="front" && orientation!="right" && orientation!="isometric") invalid("Unsupported drawing orientation");
    }
    result["views"].push_back(std::move(normalized));
  }
  const auto requested=spec.value("dimensions",Json::array());
  if(!requested.is_array() || requested.size()>32) invalid("Request at most 32 dimensions");
  for(const auto& dimension:requested) {
    const auto kind=text_field(dimension,"kind"),view=text_field(dimension,"view");
    if(!ids.contains(view)) invalid("Dimension references an unknown view",{{"view",view}});
    Json d={{"view",view},{"kind",kind}};
    if(kind=="width" || kind=="height") fields(dimension,{"view","kind"});
    else if(kind=="diameter" || kind=="radius") {
      fields(dimension,{"view","kind","center","radius"},{"tolerance"});d["center"]=resolved_point(dimension["center"],parameters);d["radius"]=scalar(dimension["radius"],parameters);
      if(d["radius"].get<double>()<=0) invalid("Radial dimension radius must be positive");
      const auto tolerance=dimension.contains("tolerance")?number(dimension["tolerance"]):attachment_tolerance;
      if(tolerance<=0 || tolerance>0.1) invalid("Circle matching tolerance must be positive and at most 0.1 mm");d["tolerance"]=tolerance;
    } else if(kind=="horizontal" || kind=="vertical") {
      fields(dimension,{"view","kind","from","to"});d["from"]=resolved_point(dimension["from"],parameters);d["to"]=resolved_point(dimension["to"],parameters);
    } else invalid("Unsupported dimension kind");
    result["dimensions"].push_back(std::move(d));
  }
  if(spec.contains("notes")) {if(!spec["notes"].is_array() || spec["notes"].size()>6) invalid("Request at most six drawing notes");for(const auto& note:spec["notes"]) result["notes"].push_back(printable(note,"note",120));}
  return result;
}

Json render_drawing(const Json& projected,const Json& normalized,const Json& identity) {
  const auto views=projections(projected,normalized);const auto dims=dimensions(views,normalized);
  const double sheet_w=normalized["sheet"]=="A3"?420:297,sheet_h=normalized["sheet"]=="A3"?297:210,margin=8,gap=7;
  const auto document=printable(identity.at("document_id"),"document_id",64);
  const auto title=normalized.value("title",document);const auto kernel=printable(identity.at("kernel_version"),"kernel_version",32);
  const auto revision=revision_number(identity.at("revision"));
  const auto left_width=sheet_w-2*margin-93;
  const auto title_lines=wrap(title,3.7,left_width-6);
  std::vector<std::string> note_lines;
  for(const auto& s:wrap("Model: "+document,2.7,left_width-6)) note_lines.push_back(s);
  if(normalized.contains("material")) for(const auto& s:wrap("Material: "+normalized["material"].get<std::string>(),2.7,left_width-6)) note_lines.push_back(s);
  for(const auto& note:normalized["notes"]) for(const auto& s:wrap("- "+note.get<std::string>(),2.7,left_width-6)) note_lines.push_back(s);
  const double footer_h=std::max(27.0,7+title_lines.size()*4.7+note_lines.size()*3.7);
  const double footer_y=sheet_h-margin-footer_h;
  const std::size_t cols=views.size()<=1?1:views.size()<=4?2:3,rows=(views.size()+cols-1)/cols;
  const double cell_w=(sheet_w-2*margin-(cols-1)*gap)/cols,cell_h=(footer_y-margin-gap-(rows-1)*gap)/rows;
  std::vector<Placement> placements;double fit=std::numeric_limits<double>::infinity();
  for(std::size_t i=0;i<views.size();++i) {
    int horizontal=0,vertical=0,radial=0;
    for(const auto& d:dims) if(d.view==views[i].id) {
      if(d.kind=="width" || d.kind=="horizontal") ++horizontal;
      else if(d.kind=="height" || d.kind=="vertical") ++vertical;
      else ++radial;
      if(text_width(d.label,2.7)>cell_w-6) invalid("Dimension label cannot fit the selected sheet",{{"view",d.view}});
    }
    Placement p{};p.x=margin+(i%cols)*(cell_w+gap);p.y=margin+(i/cols)*(cell_h+gap);p.width=cell_w;p.height=cell_h;
    p.gx=p.x+5;p.gy=p.y+9+radial*5;p.gw=cell_w-10-(vertical?4+vertical*6.5:0);p.gh=cell_h-14-radial*5-(horizontal?4+horizontal*6.5:0);
    if(p.gw<10 || p.gh<10) invalid("Annotations cannot fit the selected sheet; use fewer views/dimensions or A3",{{"view",views[i].id}});
    const auto& b=views[i].bounds;const auto width=b[2]-b[0],height=b[3]-b[1];
    if(width<1e-9 && height<1e-9) invalid("Drawing view has no measurable extents",{{"view",views[i].id}});
    if(width>1e-9) fit=std::min(fit,(p.gw-3)/width);if(height>1e-9) fit=std::min(fit,(p.gh-3)/height);
    placements.push_back(p);
  }
  const double auto_limit=std::min(fit,1e6),decade=std::pow(10.0,std::floor(std::log10(auto_limit)));
  double automatic=decade;
  for(const auto preferred:{1.0,2.0,2.5,5.0,10.0}) if(preferred*decade<=auto_limit) automatic=preferred*decade;
  double scale=normalized.contains("scale")?number(normalized["scale"]):automatic;
  if(!std::isfinite(scale) || scale<=0 || scale>fit*(1+1e-10)) invalid("Requested scale does not fit the sheet",{{"maximum_scale",fit},{"requested_scale",scale}});
  Scene sheet;rectangle(sheet,margin-2,margin-2,sheet_w-2*margin+4,sheet_h-2*margin+4);
  rectangle(sheet,margin,footer_y,sheet_w-2*margin,footer_h);line(sheet,{margin+left_width,footer_y},{margin+left_width,sheet_h-margin});
  double text_y=footer_y+5;
  for(const auto& s:title_lines){label(sheet,{margin+3,text_y},s,3.7);text_y+=4.7;}
  for(const auto& s:note_lines){label(sheet,{margin+3,text_y},s,2.7);text_y+=3.7;}
  const double metadata_x=margin+left_width+3;
  label(sheet,{metadata_x,footer_y+5},"Revision "+std::to_string(revision)+" | Units: mm",2.7);
  const auto ratio=scale<1?"1:"+numeric(1/scale):numeric(scale)+":1";
  label(sheet,{metadata_x,footer_y+10},"Scale "+ratio+" | "+normalized["sheet"].get<std::string>(),2.7);
  label(sheet,{metadata_x,footer_y+15},"Kernel: OpenCascade "+kernel,2.7);
  label(sheet,{metadata_x,footer_y+20},"Labeled views | Sheet 1/1",2.7);
  label(sheet,{metadata_x,footer_y+25},"Dimensions from saved model",2.7);
  Json files=Json::array(),measured=Json::array();
  for(const auto& d:dims) measured.push_back({{"view",d.view},{"kind",d.kind},{"value_mm",d.value}});
  for(std::size_t i=0;i<views.size();++i) {
    auto& p=placements[i];p.scale=scale;const auto& v=views[i];const auto& b=v.bounds;
    p.origin={p.gx+p.gw/2-(b[0]+b[2])*scale/2,p.gy+p.gh/2+(b[1]+b[3])*scale/2};
    std::string view_label=v.id+" - "+v.orientation;
    if(v.orientation=="section") {const auto& section=normalized["views"][i]["section"];view_label=v.id+" - SECTION "+section["axis"].get<std::string>()+"="+numeric(section["offset"].get<double>())+" mm (plane only)";}
    const auto label_size=std::min(3.0,3.0*(p.width-4)/std::max(text_width(view_label,3.0),1.0));
    if(label_size<1.8) invalid("View label is too long for the selected sheet",{{"view",v.id}});
    label(sheet,{p.x+2,p.y+4},view_label,label_size);
    geometry(sheet,v,p,normalized["hidden_lines"].get<bool>());
    Scene annotations;annotate(annotations,v,p,dims);sheet.insert(sheet.end(),annotations.begin(),annotations.end());
    if(std::find(normalized["formats"].begin(),normalized["formats"].end(),Json("dxf"))!=normalized["formats"].end()) {
      // Center marks and dimensions use the same view transform, reversed back
      // into model mm; exact projection circles/arcs are serialized directly.
      for(const auto& c:circles(v,true)) {const auto at=p.map(c.center);const auto r=std::clamp(c.radius*scale*.18,1.0,2.5);line(annotations,{at.x-r,at.y},{at.x+r,at.y},"CENTER");line(annotations,{at.x,at.y-r},{at.x,at.y+r},"CENTER");}
      files.push_back({{"name",v.id+".dxf"},{"format","dxf"},{"content",dxf(v,annotations,p,normalized["hidden_lines"].get<bool>())}});
    }
  }
  for(const auto& format:normalized["formats"]) {
    if(format=="svg") files.push_back({{"name","drawing.svg"},{"format","svg"},{"content",svg(sheet,sheet_w,sheet_h)}});
    if(format=="pdf") files.push_back({{"name","drawing.pdf"},{"format","pdf"},{"content",pdf(sheet,sheet_w,sheet_h)}});
  }
  std::size_t size=0;for(const auto& file:files) size+=file["content"].get_ref<const std::string&>().size();
  if(size>32*1024*1024) throw Error("limit_exceeded","Drawing artifacts exceed 32 MiB");
  return {{"files",files},{"dimensions",measured},{"scale",scale},{"sheet_mm",Json::array({sheet_w,sheet_h})}};
}
}
