#pragma once
#include "agentcad/storage.hpp"

namespace agentcad {
class Service;
Json live_tool_definitions();
Json live_call(Service& service, Store& store, const std::string& tool, const Json& arguments);
}
