#pragma once
#include "agentcad/storage.hpp"

namespace agentcad {
inline constexpr std::size_t artifact_input_limit = 64 * 1024 * 1024;
inline constexpr std::size_t artifact_expansion_limit = 128 * 1024 * 1024;
inline constexpr std::size_t artifact_geometry_limit = 200000;
// Read-only, source-qualified review. No editable model or recovered history is
// published. Kernel work for STEP uses the existing isolated BuiltModel worker.
Json artifact_review_definitions();
void validate_artifact_review_arguments(const Json& arguments);
Json review_external_artifact(const fs::path& workspace, const Json& arguments,
                              const std::string& native_build);
// Rechecks a portable package's exact ledger, source bytes, and review bytes.
// Reparse captured bytes to qualify persisted geometry and metadata. STEP uses
// the bounded kernel worker. This establishes neither authenticity nor history.
Json verify_external_artifact(const fs::path& package, const std::string& expected_review_sha256);
}
