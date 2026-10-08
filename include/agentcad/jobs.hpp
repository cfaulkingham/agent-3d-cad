#pragma once
#include "agentcad/storage.hpp"

namespace agentcad {
// All OCCT work runs in a fresh native process. Input is bounded structured JSON.
// Geometry results carry summary; non-geometry inspection returns its report.
Json evaluate_model(const fs::path& workspace, const Json& model,
                    const Json& request = Json{{"kind", "summary"}}, Json* cache_diagnostics = nullptr);
// Private fixed-driver supervision; never exposed as an arbitrary execution tool.
Json run_native_process(const fs::path& workspace, const Json& request);
int process_worker_main(const fs::path& input, const fs::path& output);
Json dispatch_job(const fs::path& workspace, const Json& arguments);
void check_job_cancelled();
Json job_request_context();
std::string request_fingerprint(const std::string& tool, const Json& arguments);
// Used by the executable's private process entry points, never as MCP tools.
int geometry_worker_main(const fs::path& input, const fs::path& output);
int job_worker_main(const fs::path& workspace, const std::string& job_id);
void set_worker_executable(const fs::path& executable);
}
