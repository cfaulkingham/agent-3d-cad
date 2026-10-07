#pragma once
#include "agentcad/storage.hpp"
#include <exception>

namespace agentcad {
Json tool_definitions();
// Closed field sets and identity fields of a document tool's arguments. Shared
// by Service::call and job submission so malformed input fails before queuing.
void validate_tool_arguments(const std::string& tool, const Json& args);
// The single mapping from library exceptions to domain errors: Error passes
// through; nlohmann JSON access -> invalid_argument; std::filesystem ->
// storage_error; anything else -> internal_error.
Error service_error(std::exception_ptr error);
class Service {
public:
  explicit Service(const fs::path& workspace) : store_(workspace) {}
  // Throws only Error, translated by service_error, so MCP, CLI and job
  // adapters report identical codes for identical input.
  Json call(const std::string& tool, const Json& args);
private:
  Json execute(const std::string& tool, const Json& args);
  Store store_;
};
}
