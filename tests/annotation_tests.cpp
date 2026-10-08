#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>
using namespace agentcad;
namespace {
int checks=0;
void require(bool ok,const std::string& message){++checks;if(!ok)throw std::runtime_error(message);}
void fails(const std::string& code,const std::function<void()>& fn){try{fn();}catch(const Error& e){require(e.code==code,"Expected "+code+", got "+e.code);return;}throw std::runtime_error("Expected "+code);}
struct Temp{fs::path root=fs::temp_directory_path()/path_from_utf8("agentcad-annotations-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));Temp(){directory(root);}~Temp(){std::error_code e;fs::remove_all(root,e);}};
Json call(Service& s,const std::string& tool,const Json& args){for(int i=0;;++i){try{return s.call(tool,args);}catch(const Error& e){if(e.code!="workspace_busy"||i==1000)throw;}std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
Json ready(Service& s){for(int i=0;i<10000;++i){auto r=call(s,"cad_viewer",{{"action","sync"},{"view_id","notes"}});if(r.at("state")=="ready")return r;if(r.at("state")!="loading")throw std::runtime_error(r.dump());std::this_thread::sleep_for(std::chrono::milliseconds(2));}throw std::runtime_error("No ready view");}
Json model(){return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},{"features",Json::array({{{"id","box"},{"type","box"},{"size",{20,10,6}}},{{"id","assembly"},{"type","assembly"},{"parts",Json::array({{{"id","left"},{"input","box"}},{{"id","right"},{"input","box"},{"placement",{{"translation",{30,0,0}}}}}})}}})},{"output","assembly"}};}
void run(){
 Temp t;Service s(t.root);call(s,"cad_create",{{"document_id","review"},{"model",model()}});call(s,"cad_open",{{"document_id","review"},{"view_id","notes"}});auto shown=ready(s);auto eid=shown.at("evaluation_id");
 auto act=[&](const std::string& operation){return Json{{"action","annotation"},{"operation",operation},{"view_id","notes"},{"evaluation_id",eid}};};
 auto add=[&](Json anchor,Json text="Check clearance"){auto a=act("add");a["anchor"]=anchor;a["text"]=text;return call(s,"cad_viewer",a);};
 const auto head=read_text(t.root/"documents/review/HEAD.json");auto result=add({{"kind","model"}});auto first=result.at("annotations")[0];require(first.at("status")=="current"&&first.at("anchor_lifetime")=="evaluation","New notes have explicit evaluation lifetime");require(first.at("anchor").at("part_id").is_null(),"Overview has no invented part owner");
 result=add({{"kind","part"},{"part_id","right"}},"Right leaf\nInspect hole");const auto part=result.at("annotations")[1];for(int i=0;i<3;++i)require(part.at("anchor").at("point_mm")[i]==(shown.at("summary").at("assembly").at("parts")[1].at("bounds_mm").at("min")[i].get<double>()+shown.at("summary").at("assembly").at("parts")[1].at("bounds_mm").at("max")[i].get<double>())/2,"Leaf anchor uses native bounds center");
 std::string bytes;std::size_t offset=0;for(;;){auto c=call(s,"cad_viewer",{{"action","mesh"},{"view_id","notes"},{"evaluation_id",eid},{"offset",offset}});bytes+=c.at("data").get<std::string>();if(c.at("next_offset").is_null())break;offset=c.at("next_offset").get<std::size_t>();}auto full=parse_json(bytes);
 const auto face=full.at("topology").at("faces")[0];Json ref={{"document_id","review"},{"revision",1},{"evaluation_id",eid},{"feature_id","assembly"},{"kind","face"},{"entity_id",face.at("id")}};
 result=add({{"kind","entity"},{"reference",ref}},"Inspect this face");const auto entity=result.at("annotations")[2];require(entity.at("anchor").at("point_mm")==face.at("center_mm")&&entity.at("anchor").at("part_id")==face.at("part_id"),"Entity anchor is native resolved inspection center and owner");require(entity.at("anchor").at("reference")==ref&&entity.at("anchor").at("position_semantics")=="entity_center","Resolved pick retained as evaluation scoped evidence");
 auto badref=ref;badref["revision"]=2;fails("stale_selection",[&]{add({{"kind","entity"},{"reference",badref}});});badref=ref;badref["entity_id"]="face-9999";fails("selection_missing",[&]{add({{"kind","entity"},{"reference",badref}});});
 const auto before=read_text(t.root/"views/notes/state.json");
 for(auto anchor:{Json{{"kind","part"},{"part_id","absent"}},Json{{"kind","model"},{"point_mm",{0,0,0}}},Json{{"kind","script"}}})fails("invalid_argument",[&]{add(anchor);});
 for(auto text:{Json(""),Json("   \n"),Json(std::string(513,'a')),Json(std::string("bad\x01",4)),Json(12)})fails("invalid_argument",[&]{add({{"kind","model"}},text);});
 require(read_text(t.root/"views/notes/state.json")==before,"Failed annotation admission is atomic");
 std::string utf8;for(int i=0;i<257;++i)utf8+="é";fails("invalid_argument",[&]{add({{"kind","model"}},utf8);});utf8.resize(512);add({{"kind","model"}},utf8);
 auto update=act("update");update["annotation_id"]=first.at("id");update["text"]="Edited plain text <script>";result=call(s,"cad_viewer",update);require(result.at("annotations")[0].at("anchor")==first.at("anchor"),"Text update cannot change anchor");
 Service restarted(t.root);require(call(restarted,"cad_context",{{"view_id","notes"}}).at("annotations")==result.at("annotations"),"Annotations survive service restart");
 Json context={{"action","context"},{"view_id","notes"},{"evaluation_id",eid},{"selection",ref},{"appearance",{{"default_color",{1,0,0}},{"parts",Json::array()}}}};call(s,"cad_viewer",context);
 require(ready(s).at("annotations")[2]==entity,"Colors and selection preserve inspection evidence");
 for(int i=4;i<32;++i)add({{"kind","model"}},"Bounded note "+std::to_string(i));fails("limit_exceeded",[&]{add({{"kind","model"}});});require(call(s,"cad_viewer",act("list")).at("annotations").size()==32,"Full note capacity remains readable");
 require(read_text(t.root/"documents/review/HEAD.json")==head,"Review operations leave source HEAD byte-identical");
 call(s,"cad_apply",{{"document_id","review"},{"expected_revision",1},{"operations",Json::array({{{"op","replace_feature"},{"id","box"},{"feature",{{"id","box"},{"type","box"},{"size",{21,10,6}}}}}})}});
 result=call(s,"cad_context",{{"view_id","notes"}});require(result.at("annotations")[2].at("status")=="retired"&&result.at("annotations")[2].at("anchor")==entity.at("anchor"),"HEAD changes retire anchors immediately without rebinding");fails("stale_selection",[&]{call(s,"cad_viewer",act("list"));});shown=ready(s);eid=shown.at("evaluation_id");result=call(s,"cad_viewer",act("list"));require(result.at("revision")==2&&result.at("evaluation_id")==eid&&result.at("selection").is_null(),"Read responses qualify the current source despite old saved selection");
 require(result.at("annotations")[2].at("evaluation_id")==entity.at("evaluation_id")&&result.at("annotations")[2].at("status")=="retired","Revision refresh preserves historical note identity");
 update["evaluation_id"]=eid;update["text"]="Retired note kept for review";require(call(s,"cad_viewer",update).at("annotations")[0].at("status")=="retired","Historical text edits do not reactivate anchors");
 auto del=act("delete");del["annotation_id"]=first.at("id");require(call(s,"cad_viewer",del).at("annotations").size()==31,"Delete frees a bounded review slot");fails("not_found",[&]{call(s,"cad_viewer",del);});
 result=add({{"kind","part"},{"part_id","right"}},"Current r2 note");require(result.at("annotations").back().at("status")=="current","New note uses new evaluation rather than rebinding old notes");
 require(call(s,"cad_viewer",act("clear")).at("annotations").empty(),"Clear deletes current and retired review notes");add({{"kind","model"}});
 // Motion previews explicitly retire committed inspection centers without
 // deleting text. Read/cleanup metadata remains available, add is rejected.
 const auto arm=parse_json(read_text(path_from_utf8(CAD_SOURCE_DIR)/"examples/articulated-arm.create.json"));call(s,"cad_create",arm);
 call(s,"cad_show",{{"document_id","articulated_arm"},{"view_id","notes"}});shown=ready(s);eid=shown.at("evaluation_id");result=add({{"kind","model"}},"Committed arm review");const auto arm_note=result.at("annotations")[0];
 call(s,"cad_viewer",{{"action","motion_preview"},{"view_id","notes"},{"evaluation_id",eid},{"pose_id","extended"}});shown=ready(s);eid=shown.at("evaluation_id");
 require(shown.at("draft")==true&&shown.at("annotations")[0].at("status")=="retired","Draft poses retire committed review pins");
 fails("stale_selection",[&]{add({{"kind","model"}});});require(call(s,"cad_viewer",act("list")).at("annotations")[0].at("anchor")==arm_note.at("anchor"),"Draft read preserves original inspection coordinates");
 update=act("update");update["annotation_id"]=arm_note.at("id");update["text"]="Text edit during preview";require(call(s,"cad_viewer",update).at("annotations")[0].at("status")=="retired","Draft cleanup cannot rebind an anchor");
 call(s,"cad_viewer",{{"action","motion_reset"},{"view_id","notes"},{"evaluation_id",eid}});shown=ready(s);eid=shown.at("evaluation_id");require(shown.at("draft")==false&&shown.at("evaluation_id")!=arm_note.at("evaluation_id")&&shown.at("annotations")[0].at("status")=="retired"&&shown.at("annotations")[0].at("evaluation_id")==arm_note.at("evaluation_id")&&shown.at("annotations")[0].at("anchor")==arm_note.at("anchor"),"Reset rebuilds a fresh evaluation and never rebinds the original committed anchor");
 call(s,"cad_create",{{"document_id","other"},{"model",model()}});call(s,"cad_show",{{"document_id","other"},{"view_id","notes"}});require(ready(s).at("annotations").empty(),"Retarget removes unrelated review notes");
}
}
int main(){try{set_worker_executable(CAD_SERVICE_EXE);run();std::cout<<"annotation: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
