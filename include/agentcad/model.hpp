#pragma once
#include "agentcad/json.hpp"
#include <array>

namespace agentcad {
Json model_definitions();
void validate_model(const Json& model);
double scalar(const Json& value, const Json& parameters, const std::string& unit = "mm");
std::array<double, 3> vector3(const Json& value, const Json& parameters, const std::string& unit = "mm");
Json apply_operations(const Json& model, const Json& operations);
}
