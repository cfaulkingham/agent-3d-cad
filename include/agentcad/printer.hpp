#pragma once
#include "agentcad/storage.hpp"

namespace agentcad {
inline constexpr std::size_t printer_package_bytes_limit = 128 * 1024 * 1024;
inline constexpr std::size_t printer_profile_bytes_limit = 1024 * 1024;

Json printer_definitions();
void validate_printer_arguments(const Json& arguments);
// Coordinator-only artifact operations. Native workers perform the existing
// bounded static G-code review. Neither operation contacts or controls hardware.
// Plan publishes a fresh checksummed package; verify reads a reviewed package
// and recomputes static findings. Physical upload/start has no native action.
Json plan_printer_handoff(const fs::path& workspace, const Json& record,
                         const Json& arguments, const std::string& native_build);
Json verify_printer_handoff(const fs::path& workspace, const Json& record,
                           const Json& arguments, const std::string& native_build);
}
