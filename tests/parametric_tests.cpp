#include "agentcad/cache.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include "mutation_test_support.hpp"
#include <STEPControl_Reader.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include <functional>
#include <iostream>
#include <numbers>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
void near(double actual,double expected,double tolerance=1e-6){require(std::abs(actual-expected)<=tolerance,"Expected "+std::to_string(expected)+", got "+std::to_string(actual));}
void fails(const std::string& code,const std::function<void()>& action){try{action();}catch(const Error& e){require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what());return;}throw std::runtime_error("Expected error "+code);}
struct Temporary {fs::path root;Temporary(){root=temporary_file(fs::temp_directory_path());fs::remove(root);directory(root);}~Temporary(){std::error_code ignored;fs::remove_all(root,ignored);}};
Json expression(const std::string& op,Json args,const std::string& unit="dimensionless"){return {{"expression",{{"op",op},{"args",args},{"unit",unit}}}};}
void arithmetic(){
  const Json parameters={{"angle",30},{"side",3}};
  const auto compute=[&](Json value,const std::string& unit="dimensionless"){return scalar(value,parameters,unit);};
  near(compute(expression("sin_deg",{Json{{"parameter","angle"}}})),.5);
  near(compute(expression("cos",{std::numbers::pi})), -1);
  near(compute(expression("tan_deg",{45})),1);
  near(compute(expression("asin",{.5},"deg"),"deg"),30);
  near(compute(expression("acos",{0},"rad"),"rad"),std::numbers::pi/2);
  near(compute(expression("atan",{1},"deg"),"deg"),45);
  auto angle=expression("atan2",{3,4},"deg");angle["expression"]["argument_unit"]="mm";near(compute(angle,"deg"),36.869897645844);
  auto hypotenuse=expression("sqrt",{expression("add",{expression("square",{3},"mm2"),expression("square",{4},"mm2")},"mm2")},"mm");
  near(compute(hypotenuse,"mm"),5);
  near(compute(expression("pow",{2,8})),256);
  near(compute(expression("log",{expression("exp",{2})})),2);
  near(compute(expression("min",{3,5},"mm"),"mm"),3);
  near(compute(expression("max",{3,5})),5);
  near(compute(expression("clamp",{15,3,10})),10);
  near(compute(expression("round",{2.6})),3);
  near(compute(expression("floor",{2.6})),2);
  near(compute(expression("ceil",{2.2})),3);
  near(compute(expression("negate",{2},"mm"),"mm"),-2);
  near(compute(expression("abs",{-2},"mm"),"mm"),2);
  auto compare=expression("less",{3,4});compare["expression"]["argument_unit"]="mm";
  near(compute(expression("if",{compare,5,expression("divide",{1,0},"mm")},"mm"),"mm"),5);
  for(const auto& [op,expected]:std::initializer_list<std::pair<const char*,double>>{{"less",1},{"less_equal",1},{"greater",0},{"greater_equal",0},{"equal",0},{"not_equal",1}})near(compute(expression(op,{3,4})),expected);
  near(compute(expression("and",{1,0})),0);near(compute(expression("or",{1,0})),1);near(compute(expression("not",{0})),1);
  for(const auto& invalid:{expression("sqrt",{-1}),expression("tan_deg",{90}),expression("asin",{2},"deg"),expression("log",{0}),expression("pow",{-1,.5}),expression("pow",{0,-1}),expression("clamp",{3,10,1}),expression("atan2",{0,0},"deg")})
    fails("invalid_model",[&]{compute(invalid,invalid.at("expression").at("unit"));});
  fails("invalid_argument",[&]{compute(expression("exp",{100}));});
  fails("invalid_model",[&]{compute(expression("sin_deg",{1},"mm"),"mm");});
  fails("invalid_model",[&]{compute(expression("sqrt",{expression("add",{1,2},"mm")},"mm"),"mm");});
  fails("invalid_model",[&]{compute(expression("sqrt",{1,2}));});
  fails("invalid_model",[&]{compute(expression("if",{1,2,Json{{"parameter","absent"}}}));});
  fails("invalid_model",[&]{compute(expression("if",{1,2,expression("shell",{1})}));});
  auto bad=expression("abs",{1});bad["expression"]["argument_unit"]="mm";fails("invalid_model",[&]{compute(bad);});
  Json deep=1;for(int i=0;i<18;++i)deep=expression("abs",{deep});fails("limit_exceeded",[&]{compute(deep);});
  Json wide=1;for(int i=0;i<7;++i)wide=expression("add",{wide,wide});fails("limit_exceeded",[&]{compute(wide);});
  for(const double count:{1.,65.,3.5})fails("invalid_model",[&]{pattern_count(count,parameters);});
}
Json model(){
  return {{"schema_version",1},{"units","mm"},{"parameters",{{"count",3},{"angle",30}}},
    {"features",Json::array({
      {{"id","body"},{"type","box"},{"size",{expression("multiply",{10,expression("sin_deg",{Json{{"parameter","angle"}}})},"mm"),2,2}}},
      {{"id","copies"},{"type","pattern"},{"input","body"},{"count",{{"parameter","count"}}},{"step",{20,0,0}}}
    })},{"output","copies"}};
}
void geometry_and_persistence(){
  Temporary temp;Service service(temp.root);const auto initial=model();
  const auto created=service.call("cad_create",{{"document_id","library"},{"model",initial}});
  near(created.at("summary").at("volume_mm3"),60);require(created.at("summary").at("solid_count")==3,"Parameter count creates three solids");
  auto changed=initial;changed["parameters"]["count"]=5;auto keys=feature_cache_keys(initial),changed_keys=feature_cache_keys(changed);
  require(keys.at("body")==changed_keys.at("body")&&keys.at("copies")!=changed_keys.at("copies"),"Count dependency changes only replicated output");
  const auto updated=service.call("cad_apply",{{"document_id","library"},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","count"},{"value",5}}})}});
  near(updated.at("summary").at("volume_mm3"),100);require(updated.at("summary").at("solid_count")==5,"Count edit rebuilds topology");
  const auto saved=read_text(temp.root/"documents"/"library"/"revisions"/"2.json");
  fails("invalid_model",[&]{service.call("cad_apply",{{"document_id","library"},{"expected_revision",2},{"operations",Json::array({{{"op","set_parameter"},{"name","count"},{"value",2.5}}})}});});
  require(service.call("cad_read",{{"document_id","library"}}).at("revision")==2,"Invalid count leaves HEAD unchanged");
  require(read_text(temp.root/"documents"/"library"/"revisions"/"2.json")==saved,"Rejected count preserves committed bytes");
  Service reopened(temp.root);auto reopened_summary=reopened.call("cad_query",{{"document_id","library"},{"revision",2},{"kind","summary"}}).at("summary");near(reopened_summary.at("volume_mm3"),100);
  const auto exported=service.call("cad_export",{{"document_id","library"},{"revision",2},{"format","step"}});
  STEPControl_Reader reader;require(reader.ReadFile(exported.at("path").get<std::string>().c_str())==IFSelect_RetDone&&reader.TransferRoots()>0,"Independent STEP transfer");
  require(BRepCheck_Analyzer(reader.OneShape()).IsValid(),"Independent STEP validation");GProp_GProps volume;BRepGProp::VolumeProperties(reader.OneShape(),volume);near(volume.Mass(),100);
  auto circular=initial;circular["features"][1]={{"id","copies"},{"type","circular_pattern"},{"input","body"},{"count",expression("add",{Json{{"parameter","count"}},1})},
    {"axis",{{"origin",{20,0,0}},{"direction",{0,0,1}}}},{"angle_deg",expression("divide",{360,expression("add",{Json{{"parameter","count"}},1})},"deg")}};
  near(BuiltModel(circular).summary().at("volume_mm3"),80);
  auto consumer=initial;consumer["parameters"]={{"local_count",4}};consumer["features"]=Json::array({{{"id","seed"},{"type","box"},{"size",{1,1,1}}}});consumer["output"]="seed";
  service.call("cad_create",{{"document_id","consumer"},{"model",consumer}});
  const auto captured=service.call("cad_apply",{{"document_id","consumer"},{"expected_revision",1},{"operations",Json::array({
    {{"op","set_component"},{"id","module"},{"source_document_id","library"},{"source_revision",2},{"bindings",{{"count",{{"parameter","local_count"}}}}}},
    {{"op","set_output"},{"feature_id","module"}}
  })}});
  near(captured.at("summary").at("volume_mm3"),80);
  Temporary independent;Service portable(independent.root);auto portable_record=portable.call("cad_create",{{"document_id","portable"},{"model",test::receipt_source(service,captured)}});near(portable_record.at("summary").at("volume_mm3"),80);
}
}
int main(){try{configure_kernel_logging();set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));arithmetic();geometry_and_persistence();std::cout<<checks<<" parametric checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
