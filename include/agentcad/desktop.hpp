#pragma once
#include "agentcad/service.hpp"

namespace agentcad {
Json launch_desktop(const fs::path& workspace, const std::string& view_id);
void print_client_config(const std::string& client, const fs::path& workspace);
}
