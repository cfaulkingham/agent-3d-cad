#pragma once
#include "agentcad/json.hpp"

namespace agentcad {
inline constexpr std::size_t measurement_part_limit=23;
inline constexpr std::size_t measurement_pair_limit=253;
inline constexpr std::size_t measurement_witness_limit=16;
inline constexpr double measurement_distance_tolerance=1e-7;
inline constexpr double measurement_volume_tolerance=1e-9;
Json measurement_definitions();
void validate_measurement_query(const Json& query);
// Bounded numeric comparison for unique geometric recovery, never stable IDs.
bool measurement_descriptor_matches(const Json& expected,const Json& actual);
}
