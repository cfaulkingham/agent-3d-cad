#include "agentcad/kernel.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <numbers>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const std::string& message) { ++checks; if (!value) throw std::runtime_error(message); }
void near(double actual,double expected,double tolerance=1e-7) {
  require(std::abs(actual-expected)<tolerance,"Expected "+std::to_string(expected)+", got "+std::to_string(actual));
}
void error(const std::string& code,const std::function<void()>& action,const std::string& part="body") {
  try { action(); } catch (const Error& e) {
    require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what());
    require(e.details.at("view_id")=="view","Anchor error identifies view");
    require(e.details.at("feature_id")=="assembly","Anchor error identifies assembly");
    if (!part.empty()) require(e.details.at("part_id")==part,"Anchor error identifies part");
    return;
  }
  throw std::runtime_error("Expected "+code);
}
Json part(const std::string& id) { return {{"id",id},{"input","block"}}; }
Json model(Json parts=Json::array({part("body")})) {
  return {{"schema_version",1},{"units","mm"},{"parameters",Json::object()},
    {"features",Json::array({{{"id","block"},{"type","box"},{"size",{2,3,4}}},
      {{"id","assembly"},{"type","assembly"},{"parts",parts}}})},{"output","assembly"}};
}
Json view(const std::string& orientation,Json point,const std::string& part="body",Json explosion=Json()) {
  Json item={{"id","view"},{"orientation",orientation},{"balloon_anchors",Json::array({{{"part_id",part},{"point",point}}})}};
  if (!explosion.is_null()) item["explode"]=explosion;
  return {{"views",Json::array({item})},{"hidden_lines",false}};
}
void projected(const BuiltModel& built,const Json& request,double x,double y,const std::string& part="body") {
  const auto projection=built.drawing(request);
  const auto& anchor=projection.at("views")[0].at("balloon_anchors")[0];
  require(anchor.at("part_id")==part,"Projected anchor retains owning part");
  near(anchor.at("point")[0],x); near(anchor.at("point")[1],y);
}
}
int main() {
  try {
    configure_kernel_logging();
    const auto original=model(); BuiltModel box(original);
    const auto summary=box.summary(),topology=box.topology();
    projected(box,view("front",{1,0,2}),1,2);
    projected(box,view("top",{1,1,4}),1,1);
    projected(box,view("right",{2,1,2}),1,2);
    projected(box,view("isometric",{2,0,4}),2/std::sqrt(2.0),6/std::sqrt(6.0));
    // Surface tolerance snaps to the exact face, without accepting solid
    // interiors as zero-distance attachment points.
    projected(box,view("front",{1,-0.000001,2}),1,2);
    error("selection_missing",[&]{box.drawing(view("front",{1,1,2}));});
    error("selection_missing",[&]{box.drawing(view("front",{1,-0.001,2}));});
    error("selection_missing",[&]{box.drawing(view("front",{10,10,10}));});
    error("selection_missing",[&]{box.drawing(view("front",{1,0,2},"unknown"));},"unknown");
    error("invalid_drawing",[&]{box.drawing(view("front",{1,3,2}));});
    error("invalid_drawing",[&]{box.drawing(view("top",{1,1,0}));});
    error("invalid_drawing",[&]{box.drawing(view("isometric",{0,3,0}));});
    auto section=view("section",{1,0,2}); section["views"][0]["section"]={{"axis","z"},{"offset",2}};
    error("invalid_argument",[&]{box.drawing(section);},"");
    auto duplicate=view("front",{1,0,2}); duplicate["views"][0]["balloon_anchors"].push_back(duplicate["views"][0]["balloon_anchors"][0]);
    error("invalid_argument",[&]{box.drawing(duplicate);});
    require(box.summary()==summary && box.topology()==topology,"Balloon validation never mutates assembly geometry");
    BuiltModel restored(original,box.snapshot());
    projected(restored,view("front",{1,0,2}),1,2);

    auto placed=part("body"); placed["placement"]={{"translation",{10,20,30}},
      {"rotation",{{"origin",{0,0,0}},{"axis",{0,0,1}},{"angle_deg",90}}}};
    BuiltModel rotated(model(Json::array({placed})));
    projected(rotated,view("front",{0,1,2}),9,32);
    const auto explosion=Json::array({{{"part_id","body"},{"translation",{7,-5,11}}}});
    projected(rotated,view("front",{0,1,2},"body",explosion),16,43);
    near(rotated.summary().at("bounds_mm").at("min")[2],30);

    auto blocker=part("blocker"); blocker["placement"]={{"translation",{0,-10,0}}};
    BuiltModel occluded(model(Json::array({part("body"),blocker})));
    error("invalid_drawing",[&]{occluded.drawing(view("front",{1,0,2}));});
    const auto move_blocker=Json::array({{{"part_id","blocker"},{"translation",{10,0,0}}}});
    projected(occluded,view("front",{1,0,2},"body",move_blocker),1,2);
    BuiltModel coincident(model(Json::array({part("body"),part("duplicate")})));
    error("selection_ambiguous",[&]{coincident.drawing(view("front",{1,0,2}));});
    auto adjacent=part("adjacent"); adjacent["placement"]={{"translation",{2,0,0}}};
    BuiltModel touching(model(Json::array({part("body"),adjacent})));
    error("selection_ambiguous",[&]{touching.drawing(view("front",{2,0,2}));});

    auto round=model(); round["features"][0]={{"id","block"},{"type","cylinder"},{"radius",2},{"height",4}};
    BuiltModel cylinder(round);
    projected(cylinder,view("front",{0,-2,2}),0,2);
    error("selection_missing",[&]{cylinder.drawing(view("front",{0,0,2}));});
    error("invalid_drawing",[&]{cylinder.drawing(view("front",{0,2,2}));});
    auto no_anchors=view("front",{1,0,2}); no_anchors["views"][0].erase("balloon_anchors");
    require(!box.drawing(no_anchors).at("views")[0].contains("balloon_anchors"),"Ordinary projections keep existing result contract");
    std::cout<<checks<<" balloon kernel checks passed\n";
  } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
