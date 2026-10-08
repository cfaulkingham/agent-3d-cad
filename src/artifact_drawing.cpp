#include "artifact_internal.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace agentcad::artifact_detail {
namespace {
std::string trim(std::string text){const auto a=text.find_first_not_of(" \t\r"),b=text.find_last_not_of(" \t\r");return a==std::string::npos?std::string():text.substr(a,b-a+1);}
using Pair=std::pair<unsigned,std::string>;
std::string value(const std::vector<Pair>&pairs,unsigned code,const std::string&fallback=""){std::string result=fallback;bool found=false;for(const auto&p:pairs)if(p.first==code){require(!found,"Duplicate singleton DXF group");result=p.second;found=true;}return result;}
double numeric(const std::vector<Pair>&pairs,unsigned code,double fallback=0){const auto v=value(pairs,code);return v.empty()?fallback:real(v);}
Vec point(const std::vector<Pair>&p,unsigned base,double scale){return {numeric(p,base)*scale,numeric(p,base+10)*scale,numeric(p,base+20)*scale};}
}
Parsed dxf(const std::string&bytes,const std::string&units){
  require(bytes.find('\0')==std::string::npos&&!invalid_utf8_offset(bytes),"DXF review requires ASCII/UTF-8 text");const auto scale=unit_scale(units);std::istringstream input(bytes);std::string code,data;std::vector<Pair> pairs;
  while(std::getline(input,code)){require(std::getline(input,data).good()||!input.fail(),"Truncated DXF code/value pair");require(pairs.size()<1000000&&data.size()<=4096,"DXF input budget exceeded");pairs.push_back({static_cast<unsigned>(integer(trim(code),1071)),trim(data)});}
  require(!pairs.empty(),"Empty DXF");Parsed parsed;parsed.representation="drawing_curves";std::string section;bool ended=false;std::size_t entity=0;
  for(std::size_t n=0;n<pairs.size();){check_job_cancelled();const auto p=pairs[n++];
    if(p.first==0&&p.second=="SECTION"){require(section.empty()&&n<pairs.size()&&pairs[n].first==2,"Malformed DXF section");section=pairs[n++].second;continue;}
    if(p.first==0&&p.second=="ENDSEC"){require(!section.empty(),"Unexpected DXF ENDSEC");section.clear();continue;}
    if(p.first==0&&p.second=="EOF"){require(section.empty()&&n==pairs.size(),"DXF EOF in section or trailing data");ended=true;break;}
    if(section=="HEADER"){if(p.first==9&&p.second=="$INSUNITS"){require(n<pairs.size()&&pairs[n].first==70,"Malformed DXF INSUNITS");const auto u=integer(pairs[n++].second,24);const std::map<unsigned,double> scales={{0,scale},{1,25.4},{2,304.8},{4,1},{5,10},{6,1000},{13,.001}};if(!scales.contains(u))unsupported("DXF embedded units are unsupported");require(std::abs(scales.at(u)-scale)<1e-9,"Caller units differ from DXF INSUNITS");parsed.metadata["insunits"]=u;}continue;}
    if(section=="BLOCKS"||section=="TABLES"||section=="CLASSES"||section=="OBJECTS"){continue;}
    if(section!="ENTITIES"){if(!section.empty())unsupported("Unsupported DXF section: "+section);invalid("DXF data outside a section");}
    require(p.first==0,"DXF entity lacks type group");std::vector<Pair> fields;while(n<pairs.size()&&pairs[n].first!=0)fields.push_back(pairs[n++]);const auto type=p.second;
    if(type!="LINE"&&type!="LWPOLYLINE"&&type!="ARC"&&type!="CIRCLE")unsupported("Unsupported DXF entity: "+type);
    require(numeric(fields,39)==0&&numeric(fields,210)==0&&numeric(fields,220)==0&&numeric(fields,230,1)==1,"DXF thickness or non-default OCS needs another reader");
    const auto ref="entities/"+std::to_string(++entity);Json meta={{"entity",type},{"handle",value(fields,5)},{"layer",value(fields,8)}};
    if(type=="LINE")parsed.geometry.line({point(fields,10,scale),point(fields,11,scale)},ref,meta);
    else if(type=="LWPOLYLINE"){std::vector<Vec> v;const double z=numeric(fields,38)*scale;const auto flags=integer(value(fields,70,"0"),129);require((flags&~129u)==0,"Unsupported DXF polyline flags");
      for(const auto&field:fields){if(field.first==10)v.push_back({real(field.second)*scale,NAN,z});else if(field.first==20){require(!v.empty()&&std::isnan(v.back()[1]),"Unpaired DXF polyline Y");v.back()[1]=real(field.second)*scale;}else if(field.first==42&&real(field.second)!=0)unsupported("DXF bulged polylines are unsupported");else if((field.first==40||field.first==41||field.first==43)&&real(field.second)!=0)unsupported("DXF polyline widths are unsupported");}
      require(v.size()==integer(value(fields,90),artifact_geometry_limit),"DXF declared polyline vertex count differs");for(const auto&p:v)finite(p[1]);if(flags&1){require(!v.empty(),"Empty DXF closed polyline");v.push_back(v.front());}meta["closed"]=static_cast<bool>(flags&1);parsed.geometry.line(v,ref,meta);}
    else {const auto center=point(fields,10,scale);const double radius=numeric(fields,40)*scale;require(radius>0,"DXF circle radius must be positive");double start=type=="CIRCLE"?0:numeric(fields,50),finish=type=="CIRCLE"?360:numeric(fields,51);require(start>=0&&start<360&&finish>=0&&finish<=360,"DXF arc angles outside 0..360");if(type=="ARC"&&finish<=start)finish+=360;const auto count=std::max<std::size_t>(2,static_cast<std::size_t>(std::ceil((finish-start)/3)));std::vector<Vec> points;
      for(std::size_t k=0;k<=count;++k){const double a=(start+(finish-start)*k/count)*std::acos(-1)/180;points.push_back({center[0]+radius*std::cos(a),center[1]+radius*std::sin(a),center[2]});}meta["analytic"]={{"type",type=="CIRCLE"?"circle":"arc"},{"center_mm",center},{"radius_mm",radius},{"start_degrees",start},{"end_degrees",finish}};parsed.geometry.line(points,ref,meta);}
  }
  require(ended&&!parsed.geometry.polylines.empty(),"DXF lacks EOF or supported drawing entities");parsed.limitations.push_back("ASCII LINE, straight LWPOLYLINE and sampled ARC/CIRCLE are reviewed. Widths, thickness, non-default OCS, blocks/INSERT, splines and text are unsupported; no cut or manufacturing certification is inferred.");return parsed;
}
namespace {
Mat mf_transform(const std::string& text,double scale){if(text.empty())return identity();const auto a=numbers(text,12);Mat m={a[0],a[3],a[6],a[9]*scale,a[1],a[4],a[7],a[10]*scale,a[2],a[5],a[8],a[11]*scale,0,0,0,1};affine(m);return m;}
void mf_namespace(const tinyxml2::XMLElement*e,const std::string&core){require(std::string(e->Name()).find(':')==std::string::npos,"Prefixed 3MF elements need another reader");
  if(e->Attribute("xmlns"))require(attribute(e,"xmlns")==core,"3MF core namespace changed inside model");
  for(auto a=e->FirstAttribute();a;a=a->Next()){const std::string name=a->Name();if(name.find(':')!=std::string::npos&&!name.starts_with("xmlns:")&&name!="xml:lang")unsupported("3MF extension attributes are unsupported");}
  for(auto c=e->FirstChildElement();c;c=c->NextSiblingElement())mf_namespace(c,core);}
}
Parsed three_mf(const std::string&bytes){
  const auto entries=zip(bytes);require(entries.contains("_rels/.rels")&&entries.contains("[Content_Types].xml"),"3MF package lacks OPC relationships/content types");std::string model_path;
  tinyxml2::XMLDocument types;safe_xml(types,entries.at("[Content_Types].xml"));require(tag(types.RootElement())=="Types"&&attribute(types.RootElement(),"xmlns")=="http://schemas.openxmlformats.org/package/2006/content-types","Invalid OPC content types namespace");children(types.RootElement(),{"Default","Override"});
  std::map<std::string,std::string> extensions,overrides;
  for(auto e=types.RootElement()->FirstChildElement();e;e=e->NextSiblingElement()){const auto type=attribute(e,"ContentType");require(!type.empty(),"Missing OPC content type");if(tag(e)=="Default"){const auto ext=attribute(e,"Extension");require(!ext.empty()&&extensions.emplace(ext,type).second,"Duplicate OPC extension content type");}else{auto part=attribute(e,"PartName");require(part.starts_with('/'),"OPC part override must be package rooted");part.erase(0,1);relative_uri(part);require(entries.contains(part)&&overrides.emplace(part,type).second,"Missing/duplicate OPC part override");}}
  const std::string relation_ns="http://schemas.openxmlformats.org/package/2006/relationships";
  for(const auto&[name,raw]:entries)if(name.ends_with(".rels")){tinyxml2::XMLDocument rel;safe_xml(rel,raw);const auto root=rel.RootElement();require(tag(root)=="Relationships"&&attribute(root,"xmlns")==relation_ns,"Invalid OPC relationships namespace");children(root,{"Relationship"});std::set<std::string> ids;
    for(auto e=root->FirstChildElement();e;e=e->NextSiblingElement()){require(ids.insert(attribute(e,"Id")).second&&!attribute(e,"Id").empty(),"Duplicate or empty OPC relationship ID");if(attribute(e,"TargetMode","Internal")!="Internal")unsupported("External OPC references are prohibited");auto target=attribute(e,"Target");
      const bool rooted=target.starts_with('/');if(rooted)target.erase(0,1);relative_uri(target);
      if(!rooted&&name!="_rels/.rels"){const auto marker=name.rfind("/_rels/");require(marker!=std::string::npos,"OPC relationships part has an unsupported location");target=name.substr(0,marker)+"/"+target;}
      require(entries.contains(target),"Missing contained OPC relationship target");
      if(name=="_rels/.rels"&&attribute(e,"Type")=="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"){require(model_path.empty(),"Multiple 3MF model roots");model_path=target;}}}
  require(!model_path.empty()&&entries.contains(model_path),"3MF root model relationship missing");const auto ext=model_path.substr(model_path.find_last_of('.')+1);const auto content_type=overrides.contains(model_path)?overrides.at(model_path):extensions.contains(ext)?extensions.at(ext):std::string();require(content_type=="application/vnd.ms-package.3dmanufacturing-3dmodel+xml","3MF model part content type missing/different");tinyxml2::XMLDocument xml;safe_xml(xml,entries.at(model_path));const auto root=xml.RootElement();const std::string core="http://schemas.microsoft.com/3dmanufacturing/core/2015/02";
  require(tag(root)=="model"&&attribute(root,"xmlns")==core,"Invalid 3MF core model namespace");mf_namespace(root,core);if(!attribute(root,"requiredextensions").empty())unsupported("3MF required extensions are unsupported");children(root,{"resources","build","metadata"});
  const auto scale=unit_scale(attribute(root,"unit","millimeter"));const auto resources=root->FirstChildElement("resources"),build=root->FirstChildElement("build");require(resources&&build&&!resources->NextSiblingElement("resources")&&!build->NextSiblingElement("build"),"3MF requires unique resources and build");children(resources,{"object","basematerials"});children(build,{"item"});
  std::map<std::size_t,const tinyxml2::XMLElement*> objects;for(auto e=resources->FirstChildElement();e;e=e->NextSiblingElement())if(tag(e)=="object"){const auto id=integer(attribute(e,"id"),0x7fffffffu);require(id>0&&objects.size()<10000&&objects.emplace(id,e).second,"Invalid or duplicate 3MF object ID");children(e,{"mesh","components","metadata"});const auto mesh=e->FirstChildElement("mesh"),components=e->FirstChildElement("components");require((mesh!=nullptr)!=(components!=nullptr),"3MF object must have exactly one mesh or components");require((!mesh||!mesh->NextSiblingElement("mesh"))&&(!components||!components->NextSiblingElement("components")),"Duplicate 3MF object geometry");}
  Parsed parsed;parsed.representation="triangle_mesh";std::set<std::size_t> active;std::size_t placements=0;
  std::function<void(std::size_t,const Mat&,unsigned,std::string)> place=[&](std::size_t id,const Mat&m,unsigned depth,std::string path){check_job_cancelled();require(depth<=64&&objects.contains(id)&&active.insert(id).second&&++placements<=10000,"3MF missing/cyclic/excessive object placements");const auto object=objects.at(id);
    if(const auto mesh=object->FirstChildElement("mesh")){children(mesh,{"vertices","triangles"});const auto vv=mesh->FirstChildElement("vertices"),tt=mesh->FirstChildElement("triangles");require(vv&&tt&&!vv->NextSiblingElement("vertices")&&!tt->NextSiblingElement("triangles"),"3MF mesh lacks unique vertices or triangles");children(vv,{"vertex"});children(tt,{"triangle"});std::vector<Vec> v;std::vector<std::array<std::size_t,3>> t;
      for(auto e=vv->FirstChildElement();e;e=e->NextSiblingElement()){require(v.size()<artifact_geometry_limit,"3MF vertex budget exceeded");v.push_back({real(attribute(e,"x"))*scale,real(attribute(e,"y"))*scale,real(attribute(e,"z"))*scale});}
      for(auto e=tt->FirstChildElement();e;e=e->NextSiblingElement()){require(t.size()<artifact_geometry_limit,"3MF triangle budget exceeded");t.push_back({integer(attribute(e,"v1"),v.size()),integer(attribute(e,"v2"),v.size()),integer(attribute(e,"v3"),v.size())});}
      parsed.geometry.mesh(v,t,m,path+"/object/"+std::to_string(id),{{"name",attribute(object,"name")},{"object_type",attribute(object,"type","model")}});
    }else{const auto components=object->FirstChildElement("components");children(components,{"component"});unsigned n=0;for(auto c=components->FirstChildElement();c;c=c->NextSiblingElement()){const auto target=integer(attribute(c,"objectid"),0x7fffffff);place(target,product(m,mf_transform(attribute(c,"transform"),scale)),depth+1,path+"/components/"+std::to_string(n++));}}
    active.erase(id);};
  unsigned item=0;for(auto e=build->FirstChildElement();e;e=e->NextSiblingElement()){const auto printable=attribute(e,"printable","1");require(printable=="0"||printable=="1"||printable=="true"||printable=="false","Invalid 3MF printable value");place(integer(attribute(e,"objectid"),0x7fffffff),mf_transform(attribute(e,"transform"),scale),0,"build/"+std::to_string(item++));}
  require(!parsed.geometry.triangles.empty(),"3MF build contains no mesh geometry");parsed.metadata={{"model_part",model_path},{"source_units",attribute(root,"unit","millimeter")},{"placements",placements},{"zip_entries",entries.size()}};parsed.limitations.push_back("Core build geometry and component placements are reviewed as separate meshes; overlap is not unioned, and material/property extensions, manufacturing suitability, signatures and editable CAD history are not established.");return parsed;
}
}
