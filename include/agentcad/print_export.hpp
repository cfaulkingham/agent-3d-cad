#pragma once
#include "agentcad/kernel.hpp"
#include "agentcad/storage.hpp"
namespace agentcad {
Json print_layout_schema();
void validate_print_layout(const Json& layout);
// Writes only into a coordinator-owned staging directory. Report paths are relative.
Json export_3mf(const BuiltModel& model,const std::string& feature,const Json& layout,
                const Json& identity,const fs::path& stage);
}
