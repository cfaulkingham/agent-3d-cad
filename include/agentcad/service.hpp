#pragma once
#include "agentcad/storage.hpp"

namespace agentcad {
Json tool_definitions();
// Closed field sets and identity fields of a document tool's arguments. Shared
// by Service::call and job submission so malformed input fails before queuing.
void validate_tool_arguments(const std::string& tool, const Json& args);
class Service {
public:
  explicit Service(const fs::path& workspace) : store_(workspace) {}
  Json call(const std::string& tool, const Json& args);
private:
  Store store_;
};
}
