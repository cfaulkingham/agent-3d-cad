#pragma once
#include "agentcad/json.hpp"
#include <filesystem>
#include <functional>
namespace agentcad {
// JSON-only portable visual export. Source mesh coordinates are world mm;
// summary leaf transforms are row-major rigid matrices with mm translation.
// Samples contain {time_s,parts:[{id,transform}]} and must cover every leaf.
// Native coordinator validates saved sequence identity before supplying samples.
// No OCCT objects, caller code, filesystem references or external buffers enter.
Json export_glb(const Json& mesh,const Json& topology,const Json& summary,
                const Json& identity,const Json& appearance,const Json& samples,
                const std::filesystem::path& destination,
                const std::function<void()>& check_cancel = {});
}
