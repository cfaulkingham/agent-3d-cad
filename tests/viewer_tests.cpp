#include "agentcad/service.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/viewer.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <functional>

using namespace agentcad;
namespace {
int checks=0;
void require(bool condition,const std::string& message){++checks;if(!condition)throw std::runtime_error(message);}
void fails(const std::string& code,const std::function<void()>& f){try{f();}catch(const Error& e){require(e.code==code,"Expected "+code+" got "+e.code);return;}throw std::runtime_error("Expected "+code);}
struct Temporary {
  fs::path path;
  Temporary():path(fs::temp_directory_path()/path_from_utf8("agentcad-view-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-零")) {fs::create_directory(path);}
  ~Temporary(){std::error_code ec;fs::remove_all(path,ec);}
};
Json box(){return {{"schema_version",1},{"units","mm"},{"parameters",{{"height",6}}},{"features",Json::array({{{"id","base"},{"type","box"},{"size",Json::array({20,10,Json{{"parameter","height"}}})}}})},{"output","base"}};}
Json edits(int revision,int height){return {{"document_id","part"},{"expected_revision",revision},{"operations",Json::array({{{"op","set_parameter"},{"name","height"},{"value",height}}})}};}
}
int main(){try{
  set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));Temporary temp;Service service(temp.path);
  service.call("cad_create",{{"document_id","part"},{"model",box()},{"request_id","create_part"}});
  const auto replay=service.call("cad_create",{{"document_id","part"},{"model",box()},{"request_id","create_part"}});
  require(replay.at("revision")==1,"create retry deduplicated");
  auto changed=box();changed["parameters"]["height"]=7;
  fails("request_conflict",[&]{service.call("cad_create",{{"document_id","part"},{"model",changed},{"request_id","create_part"}});});
  const auto view=service.call("cad_view",{{"document_id","part"},{"revision",1}});
  require(view.at("draft")==false && view.at("feature_id")=="base","committed view identity");
  const auto html=read_text(path_from_utf8(view.at("path").get<std::string>()));
  require(html.find("<canvas")!=std::string::npos && html.find("application/json")!=std::string::npos,"standalone HTML includes canvas and payload");
  require(html.find("connect-src 'none'")!=std::string::npos,"offline viewer forbids network");
  const auto data=parse_json(read_text(path_from_utf8(view.at("data_path").get<std::string>())));
  require(data.at("evaluation_id")==view.at("evaluation_id"),"HTML/data share identity");
  require(data.at("mesh").at("triangle_faces").size()==12,"box exact face mapping has twelve triangles");
  Json pick={{"document_id","part"},{"revision",1},{"evaluation_id",view.at("evaluation_id")},{"feature_id","base"},{"kind","edge"},{"entity_id",data.at("topology").at("edges")[0].at("id")}};
  Service reopened(temp.path);
  const auto resolved=reopened.call("cad_resolve_selection",pick);
  require(resolved.at("geometry").at("curve_kind")=="line","pick resolves after restart");
  require(resolved.contains("selector"),"unique edge yields geometric design reference");
  auto bad=pick;bad["feature_id"]="absent";
  fails("stale_selection",[&]{reopened.call("cad_resolve_selection",bad);});
  bad=pick;bad["entity_id"]="edge-999";
  fails("selection_missing",[&]{reopened.call("cad_resolve_selection",bad);});
  const auto preview=reopened.call("cad_preview",edits(1,8));
  require(preview.at("draft")==true && std::abs(preview.at("summary").at("volume_mm3").get<double>()-1600)<1e-6,"preview builds draft geometry");
  require(reopened.call("cad_read",{{"document_id","part"}}).at("revision")==1,"preview preserves HEAD");
  bad=pick;bad["evaluation_id"]=preview.at("evaluation_id");
  fails("draft_selection",[&]{reopened.call("cad_resolve_selection",bad);});
  const auto top=reopened.call("cad_query",{{"document_id","part"},{"revision",1},{"kind","topology"}});
  require(top.at("evaluation_id")!=view.at("evaluation_id"),"rebuild receives fresh identity");
  const auto mesh=reopened.call("cad_query",{{"document_id","part"},{"revision",1},{"kind","mesh"}});
  require(mesh.contains("topology")&&mesh.contains("mesh"),"mesh query contains corresponding topology");
  auto edit=edits(1,8);edit["request_id"]="edit_part";
  require(reopened.call("cad_apply",edit).at("revision")==2,"edit commits");
  require(reopened.call("cad_apply",edit).at("revision")==2,"edit retry does not duplicate commit");
  fails("stale_selection",[&]{reopened.call("cad_resolve_selection",pick);});
  fails("revision_conflict",[&]{reopened.call("cad_preview",edits(1,10));});
  const auto comparison=reopened.call("cad_compare",{{"document_id","part"},{"from_revision",1},{"to_revision",2}});
  require(std::abs(comparison.at("volume_delta_mm3").get<double>()-400)<1e-6,"revision comparison geometry");
  require(comparison.at("parameters").at("height").at("to")==8,"revision comparison named parameter");
  require(comparison.at("features").empty(),"parameter changes preserve feature intent IDs");
  fails("invalid_argument",[&]{reopened.call("cad_query",{{"document_id","part"},{"revision",2},{"kind","view"}});});
  // Resolve a pick, store its geometric intent in a real selective edit, and
  // produce the viewer for the newly committed geometry.
  auto current_view=reopened.call("cad_view",{{"document_id","part"},{"revision",2}});
  auto current_data=parse_json(read_text(path_from_utf8(current_view.at("data_path").get<std::string>())));
  pick["revision"]=2;pick["evaluation_id"]=current_view.at("evaluation_id");
  pick["entity_id"]=current_data.at("topology").at("edges")[0].at("id");
  const auto design_reference=reopened.call("cad_resolve_selection",pick).at("selector");
  auto fillet=Json{{"id","selected"},{"type","fillet"},{"input","base"},{"radius",0.5},{"edges",design_reference}};
  auto selected_ops=Json::array({Json{{"op","add_feature"},{"feature",fillet}},Json{{"op","set_output"},{"feature_id","selected"}}});
  reopened.call("cad_apply",{{"document_id","part"},{"expected_revision",2},{"operations",selected_ops}});
  auto selected_view=reopened.call("cad_view",{{"document_id","part"},{"revision",3}});
  require(selected_view.at("feature_id")=="selected" && selected_view.at("summary").at("face_count")>6,"resolved selective edit updates viewer geometry");
  const auto output_summary=reopened.call("cad_query",{{"document_id","part"},{"revision",3}});
  const auto base_summary=reopened.call("cad_query",{{"document_id","part"},{"revision",3},{"feature_id","base"}});
  require(output_summary.at("feature_id")=="selected","default summary identifies the output feature");
  require(base_summary.at("feature_id")=="base" && std::abs(base_summary.at("summary").at("volume_mm3").get<double>()-1600)<1e-6,"scoped summary identifies and measures the intermediate feature");
  require(output_summary.at("summary").at("volume_mm3")<base_summary.at("summary").at("volume_mm3"),"scoped summary does not return output geometry");
  // Four length-10 edges become eight when height changes to ten. Ambiguity
  // must preserve both saved HEAD and its old view identity.
  auto ambiguous_model=box();
  ambiguous_model["features"].push_back({{"id","round"},{"type","fillet"},{"input","base"},{"radius",0.5},
    {"edges",{{"type","geometric"},{"feature_id","base"},{"curve_kind","line"},{"expected_count",4},{"length",{{"value",10},{"tolerance",1e-6}}}}}});
  ambiguous_model["output"]="round";
  reopened.call("cad_create",{{"document_id","ambiguous"},{"model",ambiguous_model}});
  auto ambiguous_edit=edits(1,10);ambiguous_edit["document_id"]="ambiguous";
  fails("selection_ambiguous",[&]{reopened.call("cad_apply",ambiguous_edit);});
  require(reopened.call("cad_read",{{"document_id","ambiguous"}}).at("revision")==1,"ambiguous reference preserves committed HEAD");
  const auto step=reopened.call("cad_export",{{"document_id","part"},{"revision",1},{"format","step"}});
  const auto imported=reopened.call("cad_import",{{"document_id","unicode_import"},{"path",step.at("path")}});
  require(std::abs(imported.at("summary").at("volume_mm3").get<double>()-1200)<1e-6,"Unicode workspace crosses JSON and worker file boundaries");
  auto many=box();
  many["features"].push_back({{"id","row"},{"type","pattern"},{"input","base"},{"count",64},{"step",{30,0,0}}});
  many["features"].push_back({{"id","grid"},{"type","pattern"},{"input","row"},{"count",4},{"step",{0,20,0}}});
  many["output"]="grid";
  reopened.call("cad_create",{{"document_id","many"},{"model",many}});
  auto large_view=reopened.call("cad_view",{{"document_id","many"},{"revision",1}});
  const auto large_metadata=temp.path/"evaluations"/(large_view.at("evaluation_id").get<std::string>()+".json");
  require(fs::file_size(large_metadata)>max_json_bytes,"large topology exercises artifact bound above model input bound");
  auto large_pick=Json{{"document_id","many"},{"revision",1},{"evaluation_id",large_view.at("evaluation_id")},{"feature_id","grid"},{"kind","face"},{"entity_id","face-1"}};
  require(reopened.call("cad_resolve_selection",large_pick).at("geometry").at("surface_kind")=="plane","large stored evaluation resolves after read");
  auto injection=data;injection["document_id"]="</script><script>alert(1)</script>";
  require(viewer_html(injection).find("</script><script>alert")==std::string::npos,"embedded content cannot escape JSON script element");
  for(const auto& tool:tool_definitions()) require(tool.contains("inputSchema")&&tool.contains("outputSchema"),"every tool advertises input/output schema");
  std::cout<<"viewer: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;}}
