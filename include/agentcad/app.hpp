#pragma once
#include <string>

namespace agentcad {
inline constexpr char viewer_app_uri[] = "ui://agent-3d-cad/viewer.html";
inline constexpr char viewer_app_mime[] = "text/html;profile=mcp-app";
// Compiled from the reviewed web assets by CMake. No runtime files or network.
const std::string& viewer_app_html();
}
