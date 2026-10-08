#pragma once
#include "agentcad/json.hpp"

namespace agentcad {
inline constexpr std::size_t fabrication_part_limit=256;
inline constexpr std::size_t fabrication_pair_limit=256;
inline constexpr std::size_t fabrication_sample_limit=128;
inline constexpr std::size_t fabrication_total_samples=4096;
Json fabrication_definitions();
void validate_fabrication_options(const Json& options);
}
