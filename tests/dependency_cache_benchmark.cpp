#include "agentcad/cache.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/kernel.hpp"
#include "geometry_equivalence.hpp"
#include <chrono>
#include <iostream>

using namespace agentcad;
namespace {
Json fixture() {
  return parse_json(R"({"schema_version":1,"units":"mm","parameters":{"width":24},"features":[
    {"id":"stock","type":"box","size":[135,70,4]},
    {"id":"tool","type":"cylinder","radius":3,"height":6,"origin":[5,5,-1]},
    {"id":"row","type":"pattern","input":"tool","count":16,"step":[8,0,0]},
    {"id":"grid","type":"pattern","input":"row","count":8,"step":[0,8,0]},
    {"id":"perforated","type":"cut","left":"stock","right":"grid"},
    {"id":"adapter","type":"box","size":[{"parameter":"width"},30,5],"origin":[150,0,0]},
    {"id":"station","type":"assembly","parts":[{"id":"panel","input":"perforated"},{"id":"lower","input":"adapter"},{"id":"upper","input":"adapter","placement":{"translation":[0,40,0]}}]}],"output":"station"})");
}
Json measure(const fs::path& workspace,const Json& model) {
  directory(workspace);
  Json diagnostics;const auto start=std::chrono::steady_clock::now();
  const auto result=evaluate_model(workspace,model,{{"kind","summary"}},&diagnostics);
  const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
  return {{"seconds",seconds},{"summary",result.at("summary")},{"cache",diagnostics}};
}
}
int main(int argc,char** argv) {
  try {
    if(argc!=2)throw std::runtime_error("Usage: cad_dependency_cache_benchmark EVIDENCE_DIRECTORY");
    configure_kernel_logging();set_worker_executable(path_from_utf8(CAD_SERVICE_EXE));
    const auto parent=fs::absolute(path_from_utf8(argv[1]));directory(parent);
    const auto root=temporary_file(parent);fs::remove(root);directory(root);
    auto model=fixture();atomic_text(root/"model.json",model.dump(2));
    Json report={{"kernel_version",kernel_version()},{"build",AGENTCAD_CACHE_BUILD},
      {"native_sha256",sha256(read_text(path_from_utf8(CAD_SERVICE_EXE),256*1024*1024))},
      {"workspace",path_to_utf8(root)},{"initial",measure(root/"reused",model)},{"edits",Json::array()}};
    for(const int width:{32,36,40}) {
      model["parameters"]["width"]=width;
      const auto reused=measure(root/"reused",model);
      const auto cold=measure(root/("cold-"+std::to_string(width)),model);
      if(!test::geometry_equivalent(reused.at("summary"),cold.at("summary")))throw std::runtime_error("Incremental and cold geometry differ");
      const auto& hits=reused.at("cache").at("feature_hits");
      for(const auto& item:hits.items())
        if(item.value().get<bool>()!=(item.key()!="adapter"&&item.key()!="station"))throw std::runtime_error("Unexpected dependency rebuild: "+hits.dump());
      const double ratio=cold.at("seconds").get<double>()/reused.at("seconds").get<double>();
      report["edits"].push_back({{"width_mm",width},{"incremental",reused},{"independent_cold",cold},{"cold_over_incremental",ratio}});
      std::cerr<<"width "<<width<<": incremental "<<reused.at("seconds")<<" s; cold "<<cold.at("seconds")<<" s; "<<ratio<<"x\n";
    }
    report["unchanged"]=measure(root/"reused",model);
    atomic_text(root/"report.json",report.dump(2));
    std::cout<<Json{{"report",path_to_utf8(root/"report.json")},{"edits",report.at("edits").size()}}.dump()<<'\n';return 0;
  }catch(const std::exception& error){std::cerr<<"FAILED: "<<error.what()<<'\n';return 1;}
}
