#pragma once
#include "agentcad/storage.hpp"

namespace agentcad {
inline constexpr std::size_t cache_entry_bytes = 64 * 1024 * 1024;
inline constexpr std::size_t cache_total_bytes = 256 * 1024 * 1024;
inline constexpr std::size_t cache_max_entries = 128;
// Keys include the native source/toolchain identity and the pinned kernel.
std::string geometry_cache_key(const Json& model);
// Ordered graph fingerprints contain only each feature's own intent, used
// parameter values and upstream fingerprints. Source provenance is not geometry.
Json feature_cache_keys(const Json& model);
std::string projection_cache_key(const std::string& geometry_key, const Json& projection);
// Cache failure is a miss. Workers only read shared entries and stage new ones;
// successful, uncancelled coordinators publish them with bounded eviction.
std::optional<Json> read_cache(const fs::path& root, const std::string& key);
void stage_cache(const fs::path& path, const std::string& key, const Json& payload);
void publish_cache(const fs::path& root, const fs::path& staged, const std::string& key);
}
