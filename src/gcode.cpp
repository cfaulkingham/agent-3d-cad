#include "agentcad/gcode.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <string_view>

namespace agentcad {
namespace {
using Point=std::array<std::optional<double>,3>;
constexpr double pi=std::numbers::pi, tau=2*pi;
constexpr std::size_t witness_limit=64;
Json object(Json properties,Json required) {
  return {{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};
}
double number(const Json& value,double lo,double hi) {
  if(!value.is_number()||!std::isfinite(value.get<double>())||value<lo||value>hi)
    throw Error("invalid_argument","G-code profile number exceeds its finite range");
  return value.get<double>();
}
void printable(const Json& value) {
  if(!value.is_string())throw Error("invalid_argument","G-code profile name must be text");
  const auto& s=value.get_ref<const std::string&>();
  if(s.empty()||s.size()>64||invalid_utf8_offset(s)||std::any_of(s.begin(),s.end(),[](unsigned char c){return c<32||c==127;}))
    throw Error("invalid_argument","G-code profile name must be 1..64 printable UTF-8 bytes");
}
void ranges(const Json& value,std::size_t count,double minimum,double maximum) {
  if(!value.is_array()||value.size()!=count)throw Error("invalid_argument","G-code profile range array has the wrong size");
  for(const auto& range:value) {
    if(!range.is_array()||range.size()!=2)throw Error("invalid_argument","G-code profile bounds require [min,max]");
    if(number(range[0],minimum,maximum)>=number(range[1],minimum,maximum))
      throw Error("invalid_argument","G-code profile minimum must be less than maximum");
  }
}
struct Check {
  std::string id,method;std::size_t failures=0,unknowns=0;Json witnesses=Json::array();
  void add(const std::string& status,std::size_t line,const std::string& reason,Json evidence=Json::object()) {
    if(status=="fail")++failures;else if(status=="unknown")++unknowns;
    if(witnesses.size()<witness_limit)witnesses.push_back({{"status",status},{"line",line},{"reason",reason},{"evidence",std::move(evidence)}});
  }
  Json result()const{return {{"id",id},{"method",method},{"status",failures?"fail":unknowns?"unknown":"pass"},
    {"failure_count",failures},{"unknown_count",unknowns},{"witnesses",witnesses},
    {"witnesses_truncated",failures+unknowns>witnesses.size()}};}
};
struct Parsed {
  std::string command;std::map<char,double> values;bool valid=true,checksum=false;std::string reason;
};
// Numeric command words are locale independent. Strings and macro syntax are
// never evaluated. Unsupported commands retain a bounded witness instead.
Parsed parse(std::string_view original) {
  Parsed p;std::string s;int depth=0;std::optional<std::size_t> checksum_offset;
  for(std::size_t index=0;index<original.size();++index) {
    const auto c=static_cast<unsigned char>(original[index]);
    if(c==';'&&!depth)break;
    if(c=='('){if(++depth>16){p.valid=false;p.reason="Comment nesting exceeds 16";return p;}continue;}
    if(c==')'){if(!depth){p.valid=false;p.reason="Unmatched comment terminator";return p;}--depth;continue;}
    if(!depth){if(c=='*'&&!checksum_offset)checksum_offset=index;s+=static_cast<char>(c);}
  }
  if(depth){p.valid=false;p.reason="Unclosed comment";return p;}
  if(const auto star=s.find('*');star!=std::string::npos) {
    // XOR checksums cover the original bytes before *, including line number
    // and spaces; comments cannot silently change the protected command.
    const auto raw_star=*checksum_offset;unsigned int expected=0;std::size_t i=star+1;
    while(i<s.size()&&s[i]==' ')++i;
    const auto start=i;while(i<s.size()&&s[i]>='0'&&s[i]<='9')++i;
    const auto digits_end=i;const auto conversion=std::from_chars(s.data()+start,s.data()+i,expected);
    while(i<s.size()&&(s[i]==' '||s[i]=='\r'||s[i]=='\t'))++i;
    unsigned int actual=0;for(std::size_t j=0;j<raw_star;++j)actual^=static_cast<unsigned char>(original[j]);
    if(start==digits_end||conversion.ec!=std::errc{}||conversion.ptr!=s.data()+digits_end||i!=s.size()||expected>255||actual!=expected) {
      p.valid=false;p.reason="Malformed or mismatched line checksum";return p;
    }
    p.checksum=true;s.resize(star);
  }
  std::size_t i=0;
  const auto space=[&](){while(i<s.size()&&(s[i]==' '||s[i]=='\t'||s[i]=='\r'))++i;};space();
  if(i==s.size())return p;
  if(s[i]=='N'||s[i]=='n') {
    ++i;const auto start=i;while(i<s.size()&&s[i]>='0'&&s[i]<='9')++i;
    if(i==start){p.valid=false;p.reason="Malformed line number";return p;}space();
  }
  if(i==s.size())return p;
  auto command_letter=s[i++];if(command_letter>='a'&&command_letter<='z')command_letter-=32;
  if(command_letter!='G'&&command_letter!='M'&&command_letter!='T') {
    p.valid=false;p.reason="Unsupported nonnumeric command or macro";return p;
  }
  const auto start=i;while(i<s.size()&&s[i]>='0'&&s[i]<='9')++i;
  if(i==start){p.valid=false;p.reason="Missing command number";return p;}
  unsigned int code=0;const auto converted=std::from_chars(s.data()+start,s.data()+i,code);
  if(converted.ec!=std::errc{}||code>999999){p.valid=false;p.reason="Command number exceeds range";return p;}
  p.command=std::string(1,command_letter)+std::to_string(code);
  if(i<s.size()&&s[i]=='.') {
    const auto dot=i++;const auto fraction=i;while(i<s.size()&&s[i]>='0'&&s[i]<='9')++i;
    if(i==fraction){p.valid=false;p.reason="Missing command subcode";return p;}
    p.command+=s.substr(dot,i-dot);
  }
  // These supported words use numeric parameters; arbitrary messages on other
  // commands remain unknown and cannot corrupt a numeric motion state.
  static const std::set<std::string> numeric={"G0","G1","G2","G3","G4","G17","G18","G19","G20","G21","G28","G90","G91","G90.1","G91.1","G92",
    "M82","M83","M104","M109","M140","M190","M106","M107","M201","M203","M204","M205","M220","M221","M73","M84","M900"};
  if(!numeric.contains(p.command))return p;
  while(true) {
    space();if(i==s.size())return p;
    char key=s[i++];if(key>='a'&&key<='z')key-=32;
    if(key<'A'||key>'Z'){p.valid=false;p.reason="Unexpected parameter syntax";return p;}
    space();const auto begin=i;if(i<s.size()&&(s[i]=='-'||s[i]=='+'))++i;
    bool digit=false;while(i<s.size()&&s[i]>='0'&&s[i]<='9'){digit=true;++i;}
    if(i<s.size()&&s[i]=='.'){++i;while(i<s.size()&&s[i]>='0'&&s[i]<='9'){digit=true;++i;}}
    if(!digit) {
      // Homing axis selectors and the Marlin W flag have no numeric argument.
      if((p.command=="G28"&&(key=='X'||key=='Y'||key=='Z'||key=='W'))||
          (p.command=="M84"&&(key=='X'||key=='Y'||key=='Z'||key=='E'))){p.values[key]=0;continue;}
      p.valid=false;p.reason="Parameter requires a finite decimal number";return p;
    }
    double value=0;const auto* from=s.data()+begin;if(*from=='+')++from;
    const auto conversion=std::from_chars(from,s.data()+i,value);
    if(conversion.ec!=std::errc{}||conversion.ptr!=s.data()+i||!std::isfinite(value)||std::abs(value)>1e9||!p.values.emplace(key,value).second) {
      p.valid=false;p.reason="Nonfinite, excessive or repeated parameter";return p;
    }
  }
}
double normalized(double angle){angle=std::fmod(angle,tau);return angle<0?angle+tau:angle;}
double sweep(double start,double end,bool clockwise) {
  auto angle=normalized(clockwise?start-end:end-start);return angle<1e-12?tau:angle;
}
Json point_json(const Point& point) {
  Json result=Json::array();for(const auto& v:point)result.push_back(v?Json(*v):Json());return result;
}
}

Json gcode_definitions() {
  const Json finite={{"type","number"},{"minimum",-1000000},{"maximum",1000000}};
  const Json point={{"type","array"},{"items",finite},{"minItems",3},{"maxItems",3}};
  const Json pair={{"type","array"},{"items",finite},{"minItems",2},{"maxItems",2}};
  const Json name={{"type","string"},{"minLength",1},{"maxLength",64},{"pattern","^[^\\u0000-\\u001f\\u007f]+$"}};
  const Json temperature={{"type","array"},{"items",{{"type","number"},{"minimum",0},{"maximum",500}}},{"minItems",2},{"maxItems",2}};
  Json definitions;
  definitions["gcode_options"]=object({{"firmware",{{"const","marlin"}}},
    {"machine",object({{"name",name},{"motion_bounds_mm",{{"type","array"},{"items",pair},{"minItems",3},{"maxItems",3}}},{"home_position_mm",point}}, {"name","motion_bounds_mm"})},
    {"material",object({{"name",name},{"nozzle_temperature_c",temperature},{"bed_temperature_c",temperature}}, {"name","nozzle_temperature_c","bed_temperature_c"})},
    {"initial",object({{"units",{{"enum",{"mm","inch"}}}},{"xyz_mode",{{"enum",{"absolute","relative"}}}},
      {"extrusion_mode",{{"enum",{"absolute","relative"}}}},{"position_mm",point},{"extruder_mm",finite}},
      {"units","xyz_mode","extrusion_mode"})}}, {"firmware","machine","material","initial"});
  const Json status={{"enum",{"pass","fail","unknown"}}},count={{"type","integer"},{"minimum",0},{"maximum",100000000}};
  const Json witness=object({{"status",status},{"line",count},{"reason",{{"type","string"}}},{"evidence",{{"type","object"}}}}, {"status","line","reason","evidence"});
  const Json check=object({{"id",{{"enum",{"syntax","commanded_bounds","temperature_targets","extrusion","firmware_commands","position_tracking"}}}},
    {"method",{{"type","string"}}},{"status",status},{"failure_count",count},{"unknown_count",count},
    {"witnesses",{{"type","array"},{"items",witness},{"maxItems",witness_limit}}},{"witnesses_truncated",{{"type","boolean"}}}},
    {"id","method","status","failure_count","unknown_count","witnesses","witnesses_truncated"});
  definitions["gcode_report"]=object({{"status",status},{"checks",{{"type","array"},{"items",check},{"minItems",6},{"maxItems",6}}},
    {"statistics",{{"type","object"}}},{"coverage",{{"type","object"}}}}, {"status","checks","statistics","coverage"});
  return definitions;
}
void validate_gcode_options(const Json& options) {
  fields(options,{"firmware","machine","material","initial"});
  if(options.at("firmware")!="marlin")throw Error("invalid_argument","Only explicit Marlin command semantics are supported");
  const auto& machine=options.at("machine");fields(machine,{"name","motion_bounds_mm"},{"home_position_mm"});printable(machine.at("name"));
  ranges(machine.at("motion_bounds_mm"),3,-1000000,1000000);
  if(machine.contains("home_position_mm")) {
    const auto& home=machine.at("home_position_mm");if(!home.is_array()||home.size()!=3)throw Error("invalid_argument","Home requires three mm coordinates");
    for(int i=0;i<3;++i)number(home[i],machine.at("motion_bounds_mm")[i][0],machine.at("motion_bounds_mm")[i][1]);
  }
  const auto& material=options.at("material");fields(material,{"name","nozzle_temperature_c","bed_temperature_c"});printable(material.at("name"));
  for(const auto* key:{"nozzle_temperature_c","bed_temperature_c"})ranges(Json::array({material.at(key)}),1,0,500);
  const auto& initial=options.at("initial");fields(initial,{"units","xyz_mode","extrusion_mode"},{"position_mm","extruder_mm"});
  if(initial.at("units")!="mm"&&initial.at("units")!="inch")throw Error("invalid_argument","Initial units must be explicit");
  for(const auto* key:{"xyz_mode","extrusion_mode"})if(initial.at(key)!="absolute"&&initial.at(key)!="relative")
    throw Error("invalid_argument","Initial coordinate modes must be explicit");
  if(initial.contains("position_mm")) {
    const auto& p=initial.at("position_mm");if(!p.is_array()||p.size()!=3)throw Error("invalid_argument","Initial position requires three mm coordinates");
    for(const auto& n:p)number(n,-1000000,1000000);
  }
  if(initial.contains("extruder_mm"))number(initial.at("extruder_mm"),-1000000,1000000);
}

Json inspect_gcode(const std::string& content,const Json& options,const std::function<void()>& checkpoint) {
  validate_gcode_options(options);
  if(content.size()>gcode_bytes_limit)throw Error("limit_exceeded","G-code exceeds 64 MiB");
  if(invalid_utf8_offset(content)||content.find('\0')!=std::string::npos)throw Error("invalid_argument","G-code must be UTF-8 text without NUL bytes");
  Check syntax{"syntax","finite_numeric_words_and_optional_XOR_checksums"},bounds{"commanded_bounds","stateful_linear_and_planar_arc_swept_extrema"},
    temperatures{"temperature_targets","explicit_positive_targets_against_caller_ranges"},extrusion{"extrusion","stateful_E_deltas_with_resets_and_modes"},
    firmware{"firmware_commands","explicit_Marlin_subset_unknown_commands_retained"},tracking{"position_tracking","explicit_initial_state_G92_offsets_and_caller_home"};
  const auto& initial=options.at("initial");const auto& machine=options.at("machine");
  Point position{},offset={0.,0.,0.};
  if(initial.contains("position_mm"))for(int i=0;i<3;++i)position[i]=initial.at("position_mm")[i].get<double>();
  std::optional<double> e=initial.contains("extruder_mm")?std::optional<double>(initial.at("extruder_mm").get<double>()):std::nullopt;
  double unit=initial.at("units")=="inch"?25.4:1.;bool absolute=initial.at("xyz_mode")=="absolute",absolute_e=initial.at("extrusion_mode")=="absolute";
  // M82/M83 overrides are reset by G90/G91 in Marlin, unlike other firmware.
  std::array<int,3> plane={0,1,2};
  std::array<std::optional<double>,3> minimum{},maximum{};
  std::size_t lines=0,moves=0,arcs=0,positive_e=0,deposition=0,skipped_moves=0,checksum_lines=0,temperature_commands=0;
  double extruded=0,retracted=0;Json target_values=Json::array();std::set<std::string> unknown;
  bool nozzle=false,bed=false,state_supported=true,extrusion_supported=true,temperature_supported=true;
  const auto coordinate=[&](int i)->std::optional<double>{if(position[i]&&offset[i])return *position[i]+*offset[i];return {};};
  const auto sample=[&](int axis,double value,std::size_t line,const char* method) {
    if(!std::isfinite(value)||std::abs(value)>1e9){bounds.add("fail",line,"Commanded coordinate exceeds finite range");return;}
    minimum[axis]=minimum[axis]?std::min(*minimum[axis],value):value;maximum[axis]=maximum[axis]?std::max(*maximum[axis],value):value;
    const auto& range=machine.at("motion_bounds_mm")[axis];
    if(value<range[0].get<double>()-1e-7||value>range[1].get<double>()+1e-7)
      bounds.add("fail",line,"Commanded path exceeds explicit motion bounds",{{"axis",std::string(1,"XYZ"[axis])},{"value_mm",value},{"range_mm",range},{"sample",method}});
  };
  const auto started=std::chrono::steady_clock::now();std::size_t begin=0;
  while(begin<content.size()) {
    if(checkpoint)checkpoint();
    if(std::chrono::steady_clock::now()-started>std::chrono::seconds(30))throw Error("job_timeout","Static G-code inspection exceeded 30 seconds");
    auto end=content.find('\n',begin);if(end==std::string::npos)end=content.size();
    if(end-begin>4096||++lines>1000000)throw Error("limit_exceeded","G-code exceeds 4096 bytes per line or one million lines");
    const auto p=parse(std::string_view(content).substr(begin,end-begin));begin=end+1;
    if(!p.valid){syntax.add("fail",lines,p.reason);state_supported=false;position={};offset={};e.reset();continue;}
    if(p.command.empty())continue;
    if(p.checksum)++checksum_lines;
    const auto& c=p.command;const auto& v=p.values;
    const auto has=[&](char key){return v.contains(key);};const auto value=[&](char key){return v.at(key);};
    static const std::map<std::string,std::string> allowed={
      {"G0","XYZEF"},{"G1","XYZEF"},{"G2","XYZEFIJKRP"},{"G3","XYZEFIJKRP"},{"G4","PS"},
      {"G17",""},{"G18",""},{"G19",""},{"G20",""},{"G21",""},{"G28","XYZWR"},{"G90",""},{"G91",""},{"G92","XYZE"},
      {"M82",""},{"M83",""},{"M104","STR"},{"M109","STR"},{"M140","S"},{"M190","SR"},
      {"M106","SP"},{"M107","P"},{"M201","XYZEF"},{"M203","XYZE"},{"M204","PRST"},
      {"M205","XYZEBJST"},{"M220","S"},{"M221","ST"},{"M73","PRQS"},{"M84","XYZES"},{"M900","KT"}};
    if(const auto it=allowed.find(c);it!=allowed.end())for(const auto& [key,n]:v) {
      (void)n;if(it->second.find(key)==std::string::npos)firmware.add("unknown",lines,"Unsupported command parameter",{{"command",c},{"parameter",std::string(1,key)}});
    }
    if(c=="G90"||c=="G91"){absolute=c=="G90";absolute_e=absolute;continue;}
    if(c=="M82"||c=="M83"){absolute_e=c=="M82";continue;}
    if(c=="G20"||c=="G21"){unit=c=="G20"?25.4:1.;continue;}
    if(c=="G17"){plane={0,1,2};continue;}if(c=="G18"){plane={2,0,1};continue;}if(c=="G19"){plane={1,2,0};continue;}
    if(c=="G92") {
      for(int i=0;i<3;++i)if(has("XYZ"[i])){const auto physical=coordinate(i);position[i]=value("XYZ"[i])*unit;offset[i]=physical?std::optional<double>(*physical-*position[i]):std::nullopt;}
      if(has('E'))e=value('E')*unit;
      continue;
    }
    if(c=="G28") {
      const bool selected=has('X')||has('Y')||has('Z');
      for(int i=0;i<3;++i)if(!selected||has("XYZ"[i])) {
        position[i]=machine.contains("home_position_mm")?std::optional<double>(machine.at("home_position_mm")[i].get<double>()):std::nullopt;offset[i]=0.;
      }
      firmware.add("unknown",lines,"Homing sweep, firmware offsets and machine behavior are not simulated");continue;
    }
    if(c=="M104"||c=="M109"||c=="M140"||c=="M190") {
      ++temperature_commands;const bool is_nozzle=c=="M104"||c=="M109";
      if(has('T')&&value('T')!=0)firmware.add("unknown",lines,"Additional heater/extruder selection is unsupported");
      if(!has('S')&&!has('R')){temperatures.add("unknown",lines,"Temperature target is absent");continue;}
      for(const char key:{'S','R'})if(has(key)) {
        const auto target=value(key);const auto& range=options.at("material").at(is_nozzle?"nozzle_temperature_c":"bed_temperature_c");
        if(target>0){if(is_nozzle)nozzle=true;else bed=true;}
        if(target_values.size()<64) {
          Json recorded={{"line",lines},{"heater",is_nozzle?"nozzle":"bed"}};
          if(temperature_supported)recorded["target_c"]=target;
          else {recorded["programmed_value"]=target;recorded["units"]="unknown";}
          target_values.push_back(std::move(recorded));
        }
        if(!temperature_supported)temperatures.add("unknown",lines,"Temperature units were changed by an unsupported command");
        else if(target<0||(target>0&&(target<range[0].get<double>()||target>range[1].get<double>())))
          temperatures.add("fail",lines,"Positive temperature target is outside explicit material range",{{"target_c",target},{"range_c",range},{"heater",is_nozzle?"nozzle":"bed"}});
      }
      continue;
    }
    if(c=="G0"||c=="G1"||c=="G2"||c=="G3") {
      ++moves;const bool arc=c=="G2"||c=="G3";if(arc)++arcs;
      const Point before=position;Point after=position;
      bool arc_span=false;
      bool spatial=false;for(int i=0;i<3;++i)if(has("XYZ"[i])) {
        spatial=true;const auto amount=value("XYZ"[i])*unit;
        after[i]=absolute?std::optional<double>(amount):before[i]?std::optional<double>(*before[i]+amount):std::nullopt;
      }
      if(has('F')&&value('F')<=0)syntax.add("fail",lines,"Movement feedrate must be positive");
      bool known=state_supported;
      for(int i=0;i<3;++i) {
        if(before[i]&&offset[i])sample(i,*before[i]+*offset[i],lines,"start");
        if(after[i]&&offset[i])sample(i,*after[i]+*offset[i],lines,"end");
        if(!before[i]||!after[i]||!offset[i])known=false;
      }
      if(arc) {
        const auto [a,b,z]=plane;const char ca="IJK"[a],cb="IJK"[b];
        if(!before[a]||!before[b]||!after[a]||!after[b]||!offset[a]||!offset[b])known=false;
        else if(has('R')&&(has('I')||has('J')||has('K')))syntax.add("fail",lines,"Arc cannot combine radius and center forms");
        else {
          const double sx=*before[a],sy=*before[b],ex=*after[a],ey=*after[b];double cx=0,cy=0,r=0;bool valid=true;
          if(has('R')) {
            r=std::abs(value('R')*unit);const double dx=ex-sx,dy=ey-sy,chord=std::hypot(dx,dy);
            if(chord<=1e-12||r<chord/2-1e-7){syntax.add("fail",lines,"Radius arc has impossible or ambiguous endpoints");valid=false;}
            else {
              const double h=std::sqrt(std::max(0.,r*r-chord*chord/4));
              const double sign=(c=="G2"?-1.:1.)*(value('R')<0?-1.:1.);
              cx=(sx+ex)/2-sign*dy/chord*h;cy=(sy+ey)/2+sign*dx/chord*h;
            }
          }else if(has(ca)||has(cb)) {
            cx=sx+(has(ca)?value(ca)*unit:0.);
            cy=sy+(has(cb)?value(cb)*unit:0.);
            r=std::hypot(sx-cx,sy-cy);
            if(r<1e-9||std::abs(std::hypot(ex-cx,ey-cy)-r)>std::max(1e-5,r*1e-6)) {
              syntax.add("fail",lines,"Arc endpoints have inconsistent center radii");valid=false;
            }
          }else{syntax.add("fail",lines,"Arc requires a radius or in-plane center");valid=false;}
          if(has('P')||has("IJK"[z])){firmware.add("unknown",lines,"Arc turns or an out-of-plane center parameter are unsupported");known=false;}
          if(valid) {
            arc_span=true;
            const auto start=std::atan2(sy-cy,sx-cx),end_angle=std::atan2(ey-cy,ex-cx);const bool clockwise=c=="G2";
            const auto angle=sweep(start,end_angle,clockwise);
            for(int quarter=0;quarter<4;++quarter) {
              const auto candidate=quarter*pi/2;const auto distance=normalized(clockwise?start-candidate:candidate-start);
              if(distance<=angle+1e-12){sample(a,cx+r*std::cos(candidate)+*offset[a],lines,"arc_extremum");sample(b,cy+r*std::sin(candidate)+*offset[b],lines,"arc_extremum");}
            }
          }else known=false;
        }
      }
      if(!known){++skipped_moves;tracking.add("unknown",lines,"Full swept path lacks known start/frame or supported semantics",{{"start_program_mm",point_json(before)},{"end_program_mm",point_json(after)}});}
      if(has('E')) {
        const double target=value('E')*unit;
        const auto delta=!extrusion_supported?std::nullopt:absolute_e?(e?std::optional<double>(target-*e):std::nullopt):std::optional<double>(target);
        if(delta) {
          bool changed=arc_span;for(int i=0;i<3;++i)if(before[i]&&after[i]&&std::abs(*before[i]-*after[i])>1e-12)changed=true;
          bool span_unknown=false;for(int i=0;i<3;++i)if(!before[i]||!after[i])span_unknown=true;
          if(*delta>0){++positive_e;extruded+=*delta;if(changed)++deposition;else if((spatial||arc)&&span_unknown)extrusion.add("unknown",lines,"Positive extrusion has no measured nonzero spatial span");}
          else retracted-=*delta;
        }else extrusion.add("unknown",lines,"Extrusion lacks known initial E/reset or supported length semantics");
        e=absolute_e?std::optional<double>(target):e?std::optional<double>(*e+target):std::nullopt;
      }
      position=after;continue;
    }
    static const std::set<std::string> controls={"G4","M106","M107","M201","M203","M204","M205","M220","M221","M73","M84","M900"};
    if(controls.contains(c))continue;
    if(unknown.size()<128)unknown.insert(c);
    firmware.add("unknown",lines,"Unsupported command remains in the original artifact",{{"command",c}});
    // Unknown G commands can change coordinate frames; heater/tool/extrusion
    // mode commands must not leave a false known state for following motion.
    if(c[0]=='G'||c[0]=='T'||c=="M200"||c=="M206"||c=="M218"||c=="M428") {
      state_supported=false;position={};offset={};e.reset();
    }
    if(c[0]=='T'||c=="M200")extrusion_supported=false;
    if(c=="M149")temperature_supported=false;
  }
  if(!moves)syntax.add("fail",0,"No supported movement command is present");
  if(!deposition)extrusion.add(extrusion.unknowns?"unknown":"fail",0,"No measured positive extrusion accompanying nonzero spatial motion is present");
  if(!nozzle||!bed)temperatures.add("fail",0,"Positive nozzle and bed heating targets are required");
  if(skipped_moves)bounds.add("unknown",0,"Some complete swept paths could not be measured",{{"skipped_moves",skipped_moves}});
  Json checks=Json::array({syntax.result(),bounds.result(),temperatures.result(),extrusion.result(),firmware.result(),tracking.result()});
  std::string status="pass";for(const auto& check:checks)if(check.at("status")=="fail")status="fail";else if(check.at("status")=="unknown"&&status!="fail")status="unknown";
  Json extent=Json::array();for(int i=0;i<3;++i)extent.push_back({minimum[i]?Json(*minimum[i]):Json(),maximum[i]?Json(*maximum[i]):Json()});
  return {{"status",status},{"checks",checks},{"statistics",{{"lines",lines},{"movement_commands",moves},{"arc_commands",arcs},
    {"positive_extrusion_commands",positive_e},{"deposition_commands",deposition},{"extruded_mm",extruded},{"retracted_mm",retracted},
    {"checksum_lines",checksum_lines},{"temperature_commands",temperature_commands},{"temperature_targets",target_values},
    {"commanded_bounds_mm",extent},{"unknown_commands",Json(unknown)},{"skipped_sweeps",skipped_moves}}},
    {"coverage",{{"firmware","marlin"},{"coordinate_scope","commanded_frame_with_G92_offsets_and_declared_initial_home"},
      {"initial_state","caller_declared"},{"arc_planes","XY_XZ_YZ_single_turn_constant_radius_with_linear_orthogonal_travel"},
      {"sweep_samples","line_endpoints_and_exact_in_plane_arc_extrema"},{"witness_limit",witness_limit},
      {"not_evaluated",{"physical_firmware_execution","homing_or_leveling_sweeps","mesh_vs_toolpath_equivalence","thermal_physics","extrusion_physics","acceleration_or_collision_simulation","printer_compatibility"}}}}};
}
}
