#pragma once
#include "agentcad/json.hpp"
#include <cstdint>
#include <vector>

namespace agentcad {
struct RgbaImage {
  std::uint32_t width=0,height=0;
  std::vector<std::uint8_t> pixels;
};
inline constexpr std::size_t image_pixel_limit=16*1024*1024;
std::string image_base64(const std::string& bytes);
std::string image_unbase64(const std::string& text,std::size_t limit);
// Portable PNG codec. Bounded, non-interlaced 8-bit grayscale/RGB/indexed/RGBA
// decoding; all chunk CRCs, DEFLATE bounds, filters and zlib checksums are checked.
std::string encode_png(const RgbaImage& image);
RgbaImage decode_png(const std::string& bytes,std::size_t pixel_limit=image_pixel_limit);
}
