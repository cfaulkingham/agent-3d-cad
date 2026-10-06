#pragma once
#include "agentcad/storage.hpp"

namespace agentcad {
Json tool_definitions();
class Service {
public:
  explicit Service(const fs::path& workspace) : store_(workspace) {}
  Json call(const std::string& tool, const Json& args);
private:
  Store store_;
};
}
