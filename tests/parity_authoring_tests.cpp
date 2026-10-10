#include "agentcad/authoring.hpp"
#include "agentcad/kernel.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/hash.hpp"
#include <STEPControl_Reader.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <chrono>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <numbers>
#include <thread>
using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
void near(double a,double b,double tolerance=2e-5){require(std::abs(a-b)<tolerance,"Expected "+std::to_string(b)+", got "+std::to_string(a));}
Error fails(const std::string& code,const std::function<void()>& action){try{action();}catch(const Error& e){require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what());return e;}throw std::runtime_error("Expected "+code);}
struct Temp{fs::path path=temporary_file(fs::temp_directory_path());Temp(){fs::remove(path);directory(path);}~Temp(){std::error_code error;fs::remove_all(path,error);}};
void write(const fs::path& path,const std::string& bytes){std::ofstream stream(path,std::ios::binary);stream<<bytes;require(static_cast<bool>(stream),"Fixture writes");}
Json plane(Json origin={0,0,0},Json normal={0,0,1},Json x={1,0,0}){return {{"origin",origin},{"normal",normal},{"x_direction",x}};}
Json model(Json features,const std::string& output){return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",features},{"output",output}};}
Json imported(const std::string& format,const std::string& content){return {{"type",format},{"content",content},{"sha256",sha256(content)}};}
Json profile_model(Json profile,Json workplane=plane(),double depth=3){return model(Json::array({{{"id","profile"},{"type","sketch"},{"workplane",workplane},{"profile",profile}},{{"id","part"},{"type","extrude"},{"input","profile"},{"distance",depth}}}),"part");}
bool curved(const BuiltModel& built,const std::string& kind){const auto topology=built.topology("profile");for(const auto& edge:topology.at("edges"))if(edge.at("curve_kind")==kind)return true;return false;}
void readback(const BuiltModel& built,const fs::path& path){built.export_file(path,"step");STEPControl_Reader reader;require(reader.ReadFile(path_to_utf8(path).c_str())==IFSelect_RetDone,"Independent STEP reads");require(reader.TransferRoots()>0,"STEP roots transfer");const auto shape=reader.OneShape();require(BRepCheck_Analyzer(shape).IsValid(),"Exact STEP readback is valid");GProp_GProps p;BRepGProp::VolumeProperties(shape,p);near(p.Mass(),built.summary().at("volume_mm3"),1e-4);}
std::string dxf(const std::string& entities,int units=4){return "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n"+std::to_string(units)+"\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"+entities+"0\nENDSEC\n0\nEOF\n";}
std::string circle(double radius){return "0\nCIRCLE\n10\n0\n20\n0\n40\n"+std::to_string(radius)+"\n";}
Json wait(Service& service,const std::string& id){for(int attempt=0;attempt<500;++attempt){Json result;try{result=service.call("cad_job",{{"action","get"},{"job_id",id}});}catch(const Error& e){if(e.code!="workspace_busy")throw;std::this_thread::sleep_for(std::chrono::milliseconds(20));continue;}const auto state=result.at("state");if(state=="succeeded")return result.at("result");if(state=="failed"||state=="cancelled"||state=="interrupted")throw std::runtime_error(result.dump());std::this_thread::sleep_for(std::chrono::milliseconds(20));}throw std::runtime_error("Job did not complete");}
std::string section(const std::string& name,const std::string& content){return "0\nSECTION\n2\n"+name+"\n"+content+"0\nENDSEC\n";}
std::string drawing(const std::string& entities,const std::string& blocks="",const std::string& tables="",int units=4){return section("HEADER","9\n$INSUNITS\n70\n"+std::to_string(units)+"\n")+(tables.empty()?"":section("TABLES",tables))+(blocks.empty()?"":section("BLOCKS",blocks))+section("ENTITIES",entities)+"0\nEOF\n";}
std::string block(const std::string& name,const std::string& entities,double x=0,double y=0){return "0\nBLOCK\n2\n"+name+"\n70\n0\n10\n"+std::to_string(x)+"\n20\n"+std::to_string(y)+"\n"+entities+"0\nENDBLK\n";}
std::string insert(const std::string& name,const std::string& extra=""){return "0\nINSERT\n2\n"+name+"\n"+extra;}
Json font(){const auto bytes=read_text(fs::path(CAD_SOURCE_DIR)/"tests/fixtures/authoring-test.ttf",authoring_font_bytes);return {{"content_base64",encode_font_bytes(bytes)},{"sha256",sha256(bytes)}};}
Json dxf_profile(const std::string& bytes,bool with_font=false){auto profile=imported("dxf",bytes);if(with_font)profile["fonts"]={{"STANDARD",font()}};return profile;}
void blocks(const fs::path& root){
  const auto centered="0\nCIRCLE\n10\n1\n20\n2\n40\n2\n";const auto definition=block("round",centered,1,2);const std::string placement="10\n10\n20\n20\n41\n2\n42\n3\n50\n90\n";auto doc=profile_model(dxf_profile(drawing(insert("ROUND",placement),definition)));BuiltModel b(doc);near(b.summary().at("volume_mm3"),72*std::numbers::pi);const auto bounds=b.summary().at("bounds_mm");near(bounds.at("min")[0],4);near(bounds.at("max")[0],16);near(bounds.at("min")[1],16);near(bounds.at("max")[1],24);readback(b,root/"block-ellipse.step");near(BuiltModel(doc,b.snapshot()).summary().at("volume_mm3"),72*std::numbers::pi);
  const auto nested=definition+block("nested",insert("round",placement),10,20);doc=profile_model(dxf_profile(drawing(insert("nested","10\n100\n20\n200\n41\n-2\n42\n0.5\n50\n90\n"),nested)));auto n=BuiltModel(doc).summary();near(n.at("volume_mm3"),72*std::numbers::pi);near(n.at("bounds_mm")["min"][0],98);near(n.at("bounds_mm")["max"][0],102);near(n.at("bounds_mm")["min"][1],188);near(n.at("bounds_mm")["max"][1],212);
  doc=profile_model(dxf_profile(drawing(insert("round",placement+"70\n2\n71\n2\n44\n30\n45\n40\n"),definition)));auto array=BuiltModel(doc).summary();require(array.at("solid_count")==4,"MINSERT retains four instances");near(array.at("volume_mm3"),288*std::numbers::pi);near(array.at("bounds_mm")["min"][0],-36);near(array.at("bounds_mm")["max"][1],54);
  near(BuiltModel(profile_model(dxf_profile(drawing(insert("round"),definition,"",1)))).summary().at("volume_mm3"),12*std::numbers::pi*25.4*25.4,1e-3);
  for(const auto& source:{drawing(insert("a"),block("a",insert("a"))),drawing(insert("a"),block("a",circle(2))+block("A",circle(3)))})fails("invalid_argument",[&]{BuiltModel rejected(profile_model(dxf_profile(source)));});
  fails("invalid_argument",[&]{BuiltModel rejected(profile_model(dxf_profile(drawing(insert("round","41\n0\n"),definition))));});
  fails("unsupported_format",[&]{BuiltModel rejected(profile_model(dxf_profile(drawing(insert("round","66\n1\n"),definition))));});
  fails("limit_exceeded",[&]{BuiltModel rejected(profile_model(dxf_profile(drawing(insert("round","70\n65\n"),definition))));});
  std::string deep;for(int i=0;i<18;++i)deep+=block("b"+std::to_string(i),i==17?circle(1):insert("b"+std::to_string(i+1)));fails("limit_exceeded",[&]{BuiltModel rejected(profile_model(dxf_profile(drawing(insert("b0"),deep))));});
}
std::string hatch(const std::string& boundaries,int count){return "0\nHATCH\n10\n0\n20\n0\n30\n0\n2\nSOLID\n70\n1\n71\n0\n91\n"+std::to_string(count)+"\n"+boundaries+"75\n0\n76\n1\n98\n0\n";}
std::string hatch_square(double a,double b){return "92\n2\n72\n0\n73\n1\n93\n4\n10\n"+std::to_string(a)+"\n20\n"+std::to_string(a)+"\n10\n"+std::to_string(b)+"\n20\n"+std::to_string(a)+"\n10\n"+std::to_string(b)+"\n20\n"+std::to_string(b)+"\n10\n"+std::to_string(a)+"\n20\n"+std::to_string(b)+"\n97\n0\n";}
std::string hatch_circle(double r,bool ccw=true){return "92\n0\n93\n1\n72\n2\n10\n0\n20\n0\n40\n"+std::to_string(r)+"\n50\n0\n51\n360\n73\n"+(ccw?"1":"0")+"\n97\n0\n";}
void hatches(const fs::path& root){auto source=drawing(hatch(hatch_square(0,10)+hatch_square(2,4),2));BuiltModel box(profile_model(dxf_profile(source)));near(box.summary().at("volume_mm3"),288);near(box.summary("profile").at("face_count"),1);readback(box,root/"hatch-hole.step");source=drawing(hatch(hatch_circle(10)+hatch_circle(4,false),2));BuiltModel ring(profile_model(dxf_profile(source)));near(ring.summary().at("volume_mm3"),252*std::numbers::pi);readback(ring,root/"hatch-ring.step");
  const auto ellipse="92\n0\n93\n1\n72\n3\n10\n0\n20\n0\n11\n10\n21\n0\n40\n0.5\n50\n0\n51\n360\n73\n1\n97\n0\n";near(BuiltModel(profile_model(dxf_profile(drawing(hatch(ellipse,1))))).summary().at("volume_mm3"),150*std::numbers::pi);
  const auto triangle="92\n0\n93\n3\n72\n1\n10\n0\n20\n0\n11\n6\n21\n0\n72\n1\n10\n6\n20\n0\n11\n0\n21\n4\n72\n1\n10\n0\n20\n4\n11\n0\n21\n0\n97\n0\n";near(BuiltModel(profile_model(dxf_profile(drawing(hatch(triangle,1))))).summary().at("volume_mm3"),36);
  auto bad=hatch(hatch_square(0,10),1);bad.replace(bad.find("70\n1"),4,"70\n0");fails("unsupported_format",[&]{BuiltModel rejected(profile_model(dxf_profile(drawing(bad))));});bad=hatch(hatch_square(0,10),1);bad.replace(bad.find("73\n1"),4,"73\n0");fails("invalid_argument",[&]{BuiltModel rejected(profile_model(dxf_profile(drawing(bad))));});
}
std::string label(const std::string& value="B",const std::string& options=""){return "0\nTEXT\n1\n"+value+"\n40\n7\n"+options;}
void labels(const fs::path& root){auto source=drawing(label());auto profile=dxf_profile(source,true);BuiltModel b(profile_model(profile));near(b.summary().at("volume_mm3"),78);near(b.summary().at("bounds_mm")["max"][1],7);readback(b,root/"dxf-text.step");
  auto rotated=BuiltModel(profile_model(dxf_profile(drawing(label("B","10\n10\n20\n20\n50\n90\n")),true))).summary();near(rotated.at("bounds_mm")["min"][0],3);near(rotated.at("bounds_mm")["max"][1],26);
  auto aligned=BuiltModel(profile_model(dxf_profile(drawing(label("B","72\n1\n73\n3\n11\n20\n21\n30\n")),true))).summary();near(aligned.at("bounds_mm")["min"][0],16);near(aligned.at("bounds_mm")["max"][1],30);
  near(BuiltModel(profile_model(dxf_profile(drawing(label(" B ")),true))).summary().at("bounds_mm")["min"][0],8);
  near(BuiltModel(profile_model(dxf_profile(drawing(label("\\U+00E9")),true))).summary().at("volume_mm3"),78);
  near(BuiltModel(profile_model(dxf_profile(drawing(label("B","41\n2\n71\n2\n")),true))).summary().at("volume_mm3"),156);
  const auto mtext="0\nMTEXT\n1\nB\\PB\n40\n7\n71\n1\n";auto multi=BuiltModel(profile_model(dxf_profile(drawing(mtext),true))).summary();near(multi.at("volume_mm3"),156);near(multi.at("bounds_mm")["max"][1],0);near(multi.at("bounds_mm")["min"][1],-7-35.0/3);
  near(BuiltModel(profile_model(dxf_profile(drawing("0\nMTEXT\n3\nB\\P\n1\nB\n40\n7\n71\n7\n"),true))).summary().at("bounds_mm")["min"][1],0);
  near(BuiltModel(profile_model(dxf_profile(drawing("0\nMTEXT\n1\nB B\n40\n7\n41\n16\n"),true))).summary().at("volume_mm3"),156);
  fails("invalid_argument",[&]{BuiltModel rejected(profile_model(dxf_profile(source)));});profile["fonts"]["STANDARD"]["sha256"]=std::string(64,'0');fails("artifact_mismatch",[&]{BuiltModel rejected(profile_model(profile));});profile=dxf_profile(source,true);profile["fonts"]["standard"]=font();fails("invalid_argument",[&]{BuiltModel rejected(profile_model(profile));});
  for(const auto& value:{std::string("{B}"),std::string("\\C1;B"),std::string("%%d")})fails("unsupported_format",[&]{BuiltModel rejected(profile_model(dxf_profile(drawing(label(value)),true)));});
}
void source_edges(const fs::path& root){
  const std::string controls="70\n8\n71\n3\n72\n8\n73\n4\n74\n0\n40\n0\n40\n0\n40\n0\n40\n0\n40\n1\n40\n1\n40\n1\n40\n1\n10\n0\n20\n0\n10\n0\n20\n10\n10\n20\n20\n10\n10\n20\n20\n0\n";
  const auto closed="0\nSPLINE\n"+controls+"0\nLINE\n10\n20\n20\n0\n11\n0\n21\n0\n";
  BuiltModel spline(profile_model(dxf_profile(drawing(insert("curved","41\n2\n42\n3\n50\n90\n10\n50\n20\n20\n"),block("curved",closed)))));near(spline.summary().at("volume_mm3"),2160);near(spline.summary().at("bounds_mm")["min"][0],27.5);near(spline.summary().at("bounds_mm")["max"][1],60);readback(spline,root/"spline-insert.step");
  const auto boundary="92\n0\n93\n2\n72\n4\n94\n3\n73\n0\n74\n0\n95\n8\n96\n4\n40\n0\n40\n0\n40\n0\n40\n0\n40\n1\n40\n1\n40\n1\n40\n1\n10\n0\n20\n0\n10\n0\n20\n10\n10\n20\n20\n10\n10\n20\n20\n0\n97\n0\n72\n1\n10\n20\n20\n0\n11\n0\n21\n0\n97\n0\n";
  BuiltModel h(profile_model(dxf_profile(drawing(hatch(boundary,1)))));near(h.summary().at("volume_mm3"),360);readback(h,root/"hatch-spline.step");
  const auto semicircle="92\n0\n93\n2\n72\n2\n10\n0\n20\n0\n40\n5\n50\n180\n51\n0\n73\n0\n72\n1\n10\n5\n20\n0\n11\n-5\n21\n0\n97\n0\n";auto half=BuiltModel(profile_model(dxf_profile(drawing(hatch(semicircle,1))))).summary();near(half.at("volume_mm3"),37.5*std::numbers::pi);near(half.at("center_of_mass_mm")[1],20/(3*std::numbers::pi));
  const auto bulged="92\n2\n72\n1\n73\n1\n93\n2\n10\n-5\n20\n0\n42\n1\n10\n5\n20\n0\n42\n1\n97\n0\n";near(BuiltModel(profile_model(dxf_profile(drawing(hatch(bulged,1))))).summary().at("volume_mm3"),75*std::numbers::pi);
  const auto style="0\nTABLE\n2\nSTYLE\n0\nSTYLE\n2\nSTANDARD\n3\nnever-read-system-font.ttf\n70\n0\n41\n2\n0\nENDTAB\n";near(BuiltModel(profile_model(dxf_profile(drawing(label(),"",style),true))).summary().at("volume_mm3"),156);
  auto profile=dxf_profile(drawing(label()),true);BuiltModel text(profile_model(profile));require(text.topology("profile").at("provenance").at("font_sha256_by_style").at("STANDARD")==font().at("sha256"),"Topology qualifies each style by captured font hash");
}
void portability(const fs::path& root){const auto source=drawing(label("B"));const auto dxf_path=root/"captured.dxf",font_path=root/"captured.ttf";const auto bytes=decode_font_bytes(font().at("content_base64"));write(dxf_path,source);write(font_path,bytes);Service service(root/"workspace");Json args={{"format","dxf"},{"path",path_to_utf8(dxf_path)},{"feature_id","profile"},{"workplane",plane()},{"expected_sha256",sha256(source)},{"fonts",{{"STANDARD",{{"path",path_to_utf8(font_path)},{"expected_sha256",sha256(bytes)}}}}}};auto captured=service.call("cad_capture_sketch",args);require(captured.at("feature")["profile"]["content"]==source,"Raw DXF bytes preserved");require(decode_font_bytes(captured.at("feature")["profile"]["fonts"]["STANDARD"]["content_base64"])==bytes,"Exact font bytes preserved");auto doc=profile_model(captured.at("feature")["profile"]);doc["parameters"]["scale"]=1;doc["features"][0]["profile"]["scale"]={{"parameter","scale"}};service.call("cad_create",{{"document_id","source"},{"model",doc}});service.call("cad_job",{{"action","submit"},{"request_id","capture_dxf"},{"tool","cad_capture_sketch"},{"arguments",args}});require(wait(service,"capture_dxf")==captured,"Durable font/DXF capture is identical");fs::remove(dxf_path);fs::remove(font_path);fs::remove_all(root/"workspace/.cache");near(Service(root/"workspace").call("cad_query",{{"document_id","source"},{"revision",1}}).at("summary")["volume_mm3"],78);require(wait(service,"capture_dxf")==captured,"Capture replay survives all source deletion");auto edited=service.call("cad_apply",{{"document_id","source"},{"expected_revision",1},{"operations",Json::array({{{"op","set_parameter"},{"name","scale"},{"value",2}}})}});near(edited.at("summary")["volume_mm3"],312);const auto original=service.call("cad_read",{{"document_id","source"},{"revision",1}});require(original.at("model")["features"][0]["profile"]["content"]==source,"Edit does not rewrite captured source");auto bad=edited.at("model")["features"][0];bad["profile"]["fonts"]["STANDARD"]["sha256"]=std::string(64,'0');fails("artifact_mismatch",[&]{service.call("cad_apply",{{"document_id","source"},{"expected_revision",2},{"operations",Json::array({{{"op","replace_feature"},{"id","profile"},{"feature",bad}}})}});});require(service.call("cad_read",{{"document_id","source"}}).at("revision")==2,"Corrupt font rollback preserves HEAD");
  service.call("cad_create",{{"document_id","consumer"},{"model",model(Json::array({{{"id","base"},{"type","box"},{"size",{1,1,1}}}}),"base")}});auto component=service.call("cad_apply",{{"document_id","consumer"},{"expected_revision",1},{"operations",Json::array({{{"op","set_component"},{"id","captured"},{"source_document_id","source"},{"source_revision",2},{"source_feature_id","part"}},{{"op","set_output"},{"feature_id","captured"}}})}});near(component.at("summary")["volume_mm3"],312);fs::remove_all(root/"workspace/documents/source");fs::remove_all(root/"workspace/.cache");near(Service(root/"workspace").call("cad_query",{{"document_id","consumer"},{"revision",2}}).at("summary")["volume_mm3"],312);
}
}
int main(){try{set_worker_executable(CAD_SERVICE_EXE);Temp temp;blocks(temp.path);hatches(temp.path);labels(temp.path);source_edges(temp.path);portability(temp.path);std::cout<<"Parity authoring: "<<checks<<" checks passed\n";return 0;}catch(const Error& e){std::cerr<<e.json().dump()<<'\n';return 1;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
