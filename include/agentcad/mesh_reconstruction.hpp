#pragma once
#include "agentcad/json.hpp"
#include <filesystem>
namespace agentcad {
Json mesh_reconstruction_definitions();
void validate_mesh_recognition(const Json& arguments);
Json recognize_mesh_artifact(const std::filesystem::path& workspace,const Json& arguments);
}
