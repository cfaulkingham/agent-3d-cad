#pragma once
#include "agentcad/storage.hpp"

namespace agentcad {
// All OCCT work runs in a fresh native process. Input is bounded structured JSON.
// Result always has summary; topology/view add topology, view adds mesh.
Json evaluate_model(const fs::path& workspace, const Json& model,
                    const Json& request = Json{{"kind", "summary"}});
Json dispatch_job(const fs::path& workspace, const Json& arguments);
void check_job_cancelled();
Json job_request_context();
std::string request_fingerprint(const std::string& tool, const Json& arguments);
// Used by the executable's private process entry points, never as MCP tools.
int geometry_worker_main(const fs::path& input, const fs::path& output);
int job_worker_main(const fs::path& workspace, const std::string& job_id);
void set_worker_executable(const fs::path& executable);
}
