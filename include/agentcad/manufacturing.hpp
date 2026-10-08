#pragma once
#include "agentcad/json.hpp"
#include <filesystem>

namespace agentcad {
class BuiltModel;
inline constexpr std::size_t manufacturing_part_limit=256;
inline constexpr std::size_t manufacturing_file_limit=2560;
inline constexpr std::size_t manufacturing_bytes_limit=256*1024*1024;
Json manufacturing_options_schema();
void validate_manufacturing_options(const Json& options);
// Called only in an isolated native worker. Writes a complete private stage,
// using source-coordinate part geometry; publication belongs to the service.
Json manufacture(const Json& model,const BuiltModel& built,const Json& options,
                 const Json& identity,const std::filesystem::path& stage);
}
