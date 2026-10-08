#pragma once
#include "agentcad/storage.hpp"

namespace agentcad {
Json slicer_definitions();
void validate_slice_arguments(const Json& arguments);
Json plan_slice(const fs::path& workspace, const Json& record, const Json& options,
                const std::string& feature, const std::string& native_build);
Json run_slice(const fs::path& workspace, const Json& record, const Json& arguments,
               const std::string& native_build);
}
