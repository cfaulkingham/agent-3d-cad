#pragma once
#include "agentcad/image.hpp"
#include "agentcad/storage.hpp"
#include <functional>

namespace agentcad {
// N+1 boundaries describe N frames, including the final frame's hold duration.
// The native callback runs once per frame, at its starting boundary. Source
// qualification and bounded geometry evaluation belong to the coordinator.
// A stable 256-color RGB palette quantizes alpha composited onto white.
Json export_gif(const std::vector<double>& boundaries_s,const fs::path& destination,
                const std::function<RgbaImage(std::size_t,double)>& frame,
                bool loop=false,const std::function<void()>& check_cancel={});
}
