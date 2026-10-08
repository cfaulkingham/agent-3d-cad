#include "agentcad/artifact_review.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/hash.hpp"
#include "artifact_internal.hpp"
#include <cmath>
#include <iostream>
#include <functional>
#include <limits>
#include <locale>
using namespace agentcad;
namespace {
int checks=0;Json evidence={{"arguments",Json::array()},{"results",Json::array()}};
void check(bool value,const std::string&message){++checks;if(!value)throw std::runtime_error(message);}
void near(double a,double b){check(std::abs(a-b)<1e-5,"Numeric fixture mismatch: "+std::to_string(a)+" != "+std::to_string(b));}
void rejects(const std::function<void()>&fn,const std::string&code=""){try{fn();}catch(const Error&e){check(code.empty()||e.code==code,"Expected "+code+", got "+e.code+": "+e.what());return;}throw std::runtime_error("Expected rejected artifact");}
struct Temporary{fs::path root;Temporary(){root=fs::canonical(temporary_directory(fs::temp_directory_path()));}~Temporary(){std::error_code e;fs::remove_all(root,e);}};
void put32(std::string&raw,std::size_t at,std::uint32_t value){for(unsigned n=0;n<4;++n)raw[at+n]=static_cast<char>((value>>(n*8))&255);}
Json input(const fs::path&path,const std::string&format,const std::string&units){return {{"action","review"},{"path",path_to_utf8(path)},{"format",format},{"units",units},{"expected_sha256",sha256_file(path,artifact_input_limit)}};}
Json reviewed(const fs::path&workspace,const Json&args){const auto r=review_external_artifact(workspace,args,"fixture-native-build");evidence["arguments"].push_back(args);evidence["results"].push_back(r);check(r.at("read_only")==true&&r.at("native_selection_references")==false,"Read-only contract");check(r.at("source").at("sha256")==args.at("expected_sha256"),"Exact source hash");check(verify_external_artifact(path_from_utf8(text_field(r,"directory")),text_field(r,"sha256")).at("sha256")==r.at("sha256"),"Portable verification");return r;}
Json data(const Json&r){return parse_json(read_text(path_from_utf8(text_field(r,"path")),64*1024*1024),64*1024*1024);}
void numeric_tokens() {
  struct Comma : std::numpunct<char> {char do_decimal_point()const override{return ',';}};
  struct LocaleGuard {std::locale previous=std::locale();~LocaleGuard(){std::locale::global(previous);}} guard;
  std::locale::global(std::locale(std::locale::classic(),new Comma));
  near(artifact_detail::real("1.25"),1.25);
  near(artifact_detail::real("-.5"),-.5);
  near(artifact_detail::real("1."),1);
  near(artifact_detail::real("2.5e-2"),.025);
  near(artifact_detail::real("-1E+3"),-1000);
  check(std::signbit(artifact_detail::real("-0")),"Signed zero is preserved");
  check(artifact_detail::real("4.9406564584124654e-324")==std::numeric_limits<double>::denorm_min(),"Representable subnormal is retained");
  check(artifact_detail::numbers("1.25 -.5 2e1",3)==std::vector<double>({1.25,-.5,20}),"Coordinate lists stay locale independent");
  for(const auto* token:{"","+1"," 1","1 ","1,25","0x1p0","nan","inf","1e","1e999","1e-999","1000000001"})
    rejects([&]{artifact_detail::real(token);},"artifact_invalid");
}
}
int main(int argc,char**argv){try{
  numeric_tokens();
  if(argc!=4)throw std::runtime_error("Usage: tests native-worker fixtures evidence.json");set_worker_executable(fs::path(argv[1]));Temporary temp;const auto source=temp.root/"source";directory(source);for(const auto&e:fs::directory_iterator(fs::path(argv[2])))fs::copy_file(e.path(),source/e.path().filename());const auto workspace=temp.root/"workspace";directory(workspace);
  Json source_model={{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",Json::array({{{"id","Part"},{"type","box"},{"size",{10,20,30}}}})},{"output","Part"}};{DocumentLock lock(workspace,"Source");Store(workspace).commit("Source",source_model,true);}
  auto a=input(source/"triangle.stl","stl","in");a["native_source"]={{"document_id","Source"},{"revision",1},{"feature_id","Part"}};auto r=reviewed(workspace,a);near(r["summary"]["bounds"]["max"][0],25.4);check(r["source"]["native_source_association"]["qualification"]=="caller_declared","Native source association stays declared");check(r["summary"]["triangles"]==1,"ASCII STL count");
  r=reviewed(workspace,input(source/"triangle-binary.stl","stl","cm"));near(r["summary"]["bounds"]["max"][1],10);check(data(r)["metadata"]["encoding"]=="binary","Binary STL recognized");
  r=reviewed(workspace,input(source/"triangle.glb","glb","m"));near(r["summary"]["bounds"]["min"][0],1000);near(r["summary"]["bounds"]["max"][0],2000);near(r["summary"]["bounds"]["min"][1],3000);near(r["summary"]["bounds"]["min"][2],4000);check(data(r)["geometry"]["triangles"][0]==Json({0,2,1}),"Mirrored GLTF winding");
  {const auto matrix=reviewed(workspace,input(source/"triangle-matrix.glb","glb","m"));check(data(matrix)["geometry"]==data(r)["geometry"],"GLTF column-major matrix matches independently specified TRS");}
  for(const auto*name:{"nested-stored.3mf","nested-deflate.3mf"}){r=reviewed(workspace,input(source/name,"3mf","file"));near(r["summary"]["bounds"]["min"][0],120);near(r["summary"]["bounds"]["max"][0],130);near(r["summary"]["bounds"]["min"][1],30);near(r["summary"]["bounds"]["min"][2],40);check(data(r)["metadata"]["placements"]==2,"Nested 3MF components reviewed");}
  r=reviewed(workspace,input(source/"drawing.dxf","dxf","mm"));check(r["summary"]["curves"]==3,"DXF three real entities");near(r["summary"]["bounds"]["max"][0],22);check(data(r)["geometry"]["polylines"][1]["closed"]==true,"Closed DXF polyline");
  a=input(source/"robot.urdf","urdf","m");a["references"]=Json::array({{{"uri","triangle.stl"},{"path",path_to_utf8(source/"triangle.stl")},{"expected_sha256",sha256_file(source/"triangle.stl",artifact_input_limit)},{"units","cm"}}});r=reviewed(workspace,a);check(data(r)["metadata"]["graph"]["links"].size()==2&&data(r)["metadata"]["graph"]["joints"].size()==1,"URDF graph parsed");near(r["summary"]["bounds"]["max"][0],1010);near(r["summary"]["bounds"]["min"][2],-3);check(r["source"]["references"].size()==1,"Referenced bytes captured");
  const auto original=path_from_utf8(text_field(r,"directory")),moved=temp.root/"moved";fs::copy(original,moved,fs::copy_options::recursive);check(verify_external_artifact(moved,text_field(r,"sha256"))["source"]==r["source"],"Relocated package verifies");atomic_text(moved/"references/1.stl","changed");rejects([&]{verify_external_artifact(moved,text_field(r,"sha256"));});
  r=reviewed(workspace,input(source/"robot.sdf","sdf","m"));near(r["summary"]["bounds"]["min"][0],3001);near(r["summary"]["bounds"]["max"][0],3003);check(data(r)["metadata"]["graph"]["configuration"]=="declared_link_poses","SDF declared pose policy");
  r=reviewed(workspace,input(source/"primitives.urdf","urdf","m"));near(r["summary"]["bounds"]["min"][0],-10);near(r["summary"]["bounds"]["max"][0],35);near(r["summary"]["bounds"]["max"][2],10);check(r["summary"]["groups"]==2,"Sphere/cylinder roles reviewed separately");
  r=reviewed(workspace,input(source/"robot.srdf","srdf","m"));check(r["summary"]["bounds"].is_null()&&r["summary"]["triangles"]==0,"SRDF does not invent geometry");check(data(r)["metadata"]["groups"].size()==2&&data(r)["metadata"]["robot_names_qualified"]==false,"SRDF semantics unqualified");
  Json model={{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",Json::array({{{"id","Part"},{"type","box"},{"size",{10,20,30}}}})},{"output","Part"}};const auto step_path=source/"roundtrip.step";evaluate_model(workspace,model,{{"kind","export"},{"format","step"},{"path",path_to_utf8(step_path)}});r=reviewed(workspace,input(step_path,"step","file"));check(r["summary"]["representation"]=="exact_brep_import","STEP exact import policy");near(data(r)["metadata"]["exact_summary"]["volume_mm3"],6000);check(data(r).at("geometry").dump().find("face-")==std::string::npos,"No native face selectors");
  for(const auto*name:{"deflate-dynamic.zip","deflate-fixed.zip","deflate-stored.zip"}){const auto entries=artifact_detail::zip(read_text(source/name,artifact_input_limit));check(entries.at("probe.txt").size()==(28+256)*150,"Independent DEFLATE data size");}
  for(const auto*name:{"traversal.3mf","duplicate.3mf","symlink.3mf"})rejects([&]{artifact_detail::zip(read_text(source/name,artifact_input_limit));});
  rejects([&]{artifact_detail::zip(read_text(source/"total-expansion.zip",artifact_input_limit));});
  auto zip=read_text(source/"nested-stored.3mf",artifact_input_limit);const auto central=zip.find("PK\x01\x02");check(central!=std::string::npos,"Fixture central header");auto bad=zip;put32(bad,14,1);put32(bad,central+16,1);rejects([&]{artifact_detail::zip(bad);});bad=zip;put32(bad,22,0x7fffffffu);put32(bad,central+24,0x7fffffffu);rejects([&]{artifact_detail::zip(bad);});bad=zip;bad[6]=1;bad[central+8]=1;rejects([&]{artifact_detail::zip(bad);},"unsupported_feature");
  auto deflate=read_text(source/"deflate-dynamic.zip",artifact_input_limit);bad=deflate;bad[39]=static_cast<char>(255);rejects([&]{artifact_detail::zip(bad);});bad=deflate;bad.resize(bad.size()-5);rejects([&]{artifact_detail::zip(bad);});
  {bad=deflate;const auto central_at=bad.find("PK\x01\x02");const auto compressed=artifact_detail::u32(bad,18);bad.insert(central_at,1,'X');put32(bad,18,compressed+1);put32(bad,central_at+1+20,compressed+1);put32(bad,bad.size()-6,static_cast<std::uint32_t>(central_at+1));rejects([&]{artifact_detail::zip(bad);});}
  bad=deflate;const auto dcentral=bad.find("PK\x01\x02");put32(bad,14,1);put32(bad,dcentral+16,1);rejects([&]{artifact_detail::zip(bad);});
  bad=deflate;put32(bad,22,1);put32(bad,dcentral+24,1);rejects([&]{artifact_detail::zip(bad);});
  bad=zip;put32(bad,bad.size()-10,0xffffffffu);rejects([&]{artifact_detail::zip(bad);},"unsupported_feature");
  rejects([&]{auto q=input(source/"robot.urdf","urdf","m");review_external_artifact(workspace,q,"build");});
  rejects([&]{auto q=a;q["references"][0]["uri"]="../triangle.stl";validate_artifact_review_arguments(q);});
  rejects([&]{artifact_detail::stl("solid bad\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 0 0 0\nvertex 1 0 0\nendloop\nendfacet\nendsolid bad",1);});
  rejects([&]{auto bad=read_text(source/"triangle.glb",artifact_input_limit);put32(bad,8,12);artifact_detail::glb(bad);});
  rejects([&]{artifact_detail::dxf(read_text(source/"drawing.dxf",artifact_input_limit),"in");});
  rejects([&]{artifact_detail::dxf("0\nSECTION\n2\nENTITIES\n0\nINSERT\n2\nblock\n0\nENDSEC\n0\nEOF\n","mm");},"unsupported_feature");
  rejects([&]{artifact_detail::robot("<robot name='r'><link name='a'/><link name='b'/></robot>","urdf",{});});
  rejects([&]{artifact_detail::robot("<sdf version='1.12'><model name='r'><link name='a'><pose relative_to='other'>0 0 0 0 0 0</pose></link></model></sdf>","sdf",{});},"unsupported_feature");
  rejects([&]{artifact_detail::robot("<robot name='r'><group name='a'><group name='b'/></group><group name='b'><group name='a'/></group></robot>","srdf",{});});
  rejects([&]{artifact_detail::robot("<sdf version='1.12'><model name='r'><plugin name='run' filename='evil.so'/><link name='a'/></model></sdf>","sdf",{});},"unsupported_feature");
  fs::create_symlink(source/"triangle.stl",source/"link.stl");rejects([&]{auto q=input(source/"link.stl","stl","mm");review_external_artifact(workspace,q,"build");});
  rejects([&]{auto q=input(source/"triangle.glb","glb","mm");validate_artifact_review_arguments(q);},"invalid_argument");rejects([&]{auto q=input(source/"triangle.stl","stl","mm");q["shell"]="touch bad";validate_artifact_review_arguments(q);},"invalid_argument");rejects([&]{auto q=input(source/"triangle.stl","stl","mm");q["expected_sha256"]=std::string(64,'0');review_external_artifact(workspace,q,"build");},"artifact_mismatch");
  rejects([&]{tinyxml2::XMLDocument d;artifact_detail::safe_xml(d,"<!DOCTYPE x SYSTEM 'file:///outside'><robot name='r'/>");},"unsupported_feature");rejects([&]{tinyxml2::XMLDocument d;artifact_detail::safe_xml(d,"<robot name='&#999999999999999;' />");});rejects([&]{tinyxml2::XMLDocument d;artifact_detail::safe_xml(d,"<robot name='&#xD800;' />");});rejects([&]{artifact_detail::relative_uri("../outside");});rejects([&]{artifact_detail::relative_uri("https://example.com/a");});
  {tinyxml2::XMLDocument d;artifact_detail::safe_xml(d,"<robot name='r'><!-- A & B --></robot>");check(d.RootElement()!=nullptr,"XML comments are accepted without entity interpretation");}
  evidence["definitions"]=artifact_review_definitions();atomic_text(fs::path(argv[3]),evidence.dump(2),64*1024*1024);std::cout<<checks<<" artifact review checks passed\n";return 0;
}catch(const Error&e){std::cerr<<e.json().dump(2)<<"\n";return 1;}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
