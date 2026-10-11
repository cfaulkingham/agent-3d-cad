#pragma once
#include "agentcad/storage.hpp"
#include "agentcad/image.hpp"
#include <functional>

namespace agentcad {
// Rasterize worker display geometry only. Exact source/model geometry is never
// altered. The destination is staged; publication belongs to the coordinator.
RgbaImage render_rgba(const Json& payload, const Json& settings,
                      const std::function<void()>& check_cancel = {});
Json render_png(const Json& payload, const Json& settings,
                const fs::path& destination,
                const std::function<void()>& check_cancel = {});
}
