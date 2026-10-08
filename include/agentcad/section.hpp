#pragma once
#include "agentcad/json.hpp"

namespace agentcad {
inline constexpr std::size_t section_part_limit=1024;
inline constexpr std::size_t section_entity_limit=10000;
inline constexpr std::size_t section_vertex_limit=200000;
inline constexpr std::size_t section_triangle_limit=200000;
inline constexpr std::size_t section_point_limit=200000;
inline constexpr std::size_t section_bytes_limit=8*1024*1024;
inline constexpr double section_deflection_mm=.1;
inline constexpr double section_point_tolerance_mm=1e-6;
Json section_definitions();
void validate_section_query(const Json& query);
}
