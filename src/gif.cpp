#include "agentcad/gif.hpp"
#include <array>
#include <cmath>
#include <fstream>
#include <unordered_map>

namespace agentcad {
namespace {
constexpr std::size_t byte_limit=64*1024*1024,pixel_limit=4*1024*1024,total_pixel_limit=128*1024*1024;
struct Output {
  std::ofstream file;std::size_t bytes=0;
  explicit Output(const fs::path& path):file(path,std::ios::binary|std::ios::trunc){if(!file)throw Error("storage_error","Cannot create staged animated GIF");}
  void write(const char* data,std::size_t count){if(count>byte_limit-bytes)throw Error("limit_exceeded","Animated GIF exceeds 64 MiB");file.write(data,static_cast<std::streamsize>(count));if(!file)throw Error("storage_error","Cannot write staged animated GIF");bytes+=count;}
  void byte(unsigned value){const auto b=static_cast<char>(value);write(&b,1);}
  void word(unsigned value){byte(value&255);byte((value>>8)&255);}
};
struct Codes {
  Output& output;std::array<char,255> block{};std::size_t count=0;std::uint32_t bits=0;unsigned used=0;
  void byte(unsigned value){block[count++]=static_cast<char>(value);if(count==block.size())flush();}
  void flush(){if(count){output.byte(static_cast<unsigned>(count));output.write(block.data(),count);count=0;}}
  void code(unsigned value,unsigned width){bits|=value<<used;used+=width;while(used>=8){byte(bits&255);bits>>=8;used-=8;}}
  void finish(){if(used)byte(bits&255);flush();output.byte(0);}
};
void compressed(Output& output,const std::vector<std::uint8_t>& pixels,const std::function<void()>& check) {
  output.byte(8);Codes codes{output};std::unordered_map<std::uint32_t,unsigned> dictionary;dictionary.reserve(4096);
  unsigned next=258,width=9,prefix=pixels.front();codes.code(256,width);
  for(std::size_t i=1;i<pixels.size();++i){if(i%4096==0)check();const auto suffix=pixels[i];const auto key=(prefix<<8)|suffix;const auto found=dictionary.find(key);
    if(found!=dictionary.end()){prefix=found->second;continue;}
    codes.code(prefix,width);
    if(next<4096){dictionary.emplace(key,next++);if(next>(1u<<width)&&width<12)++width;}
    else {codes.code(256,width);dictionary.clear();next=258;width=9;}
    prefix=suffix;
  }
  codes.code(prefix,width);if(next==(1u<<width)&&width<12)++width;codes.code(257,width);codes.finish();
}
std::vector<std::uint8_t> quantize(const RgbaImage& image,const std::function<void()>& check) {
  std::vector<std::uint8_t> result(static_cast<std::size_t>(image.width)*image.height);
  for(std::size_t i=0;i<result.size();++i){if(i%4096==0)check();const auto* p=image.pixels.data()+i*4;const auto alpha=p[3];unsigned values[3];for(int k=0;k<3;++k)values[k]=(static_cast<unsigned>(p[k])*alpha+255*(255-alpha)+127)/255;
    const auto r=(values[0]*7+127)/255,g=(values[1]*7+127)/255,b=(values[2]*3+127)/255;result[i]=static_cast<std::uint8_t>((r<<5)|(g<<2)|b);}
  return result;
}
}
Json export_gif(const std::vector<double>& boundaries,const fs::path& destination,const std::function<RgbaImage(std::size_t,double)>& frame,bool loop,const std::function<void()>& check_cancel) {
  const auto check=[&]{if(check_cancel)check_cancel();};check();
  if(!frame||boundaries.size()<3||boundaries.size()>257)throw Error("limit_exceeded","Animated GIF needs 2 to 256 native frames");
  std::vector<unsigned> delays;long long previous=0;
  for(std::size_t i=0;i<boundaries.size();++i){const auto t=boundaries[i];if(!std::isfinite(t)||t<0||t>3600||(i==0&&t!=0)||(i&&t<=boundaries[i-1]))throw Error("invalid_argument","GIF boundaries start at zero and increase to at most 3600 seconds");
    const auto tick=std::llround(t*100);if(i){const auto delay=tick-previous;if(delay<1||delay>65535)throw Error("invalid_argument","GIF frame delays need 1 to 65535 centiseconds after timing quantization");delays.push_back(static_cast<unsigned>(delay));}previous=tick;}
  if(fs::exists(destination))throw Error("storage_error","Staged GIF destination already exists");
  const auto temporary=temporary_directory(destination.parent_path());
  try {
    Output out(temporary/"animation.gif");std::uint32_t width=0,height=0;std::size_t total_pixels=0;
    for(std::size_t i=0;i<delays.size();++i){check();const auto image=frame(i,boundaries[i]);check();const auto count=static_cast<std::uint64_t>(image.width)*image.height;
      if(!image.width||!image.height||image.width>4096||image.height>4096||count>pixel_limit||image.pixels.size()!=count*4)throw Error("limit_exceeded","GIF frames need valid RGBA images of at most 4096 per axis and 4 million pixels");
      if(!i&&count*delays.size()>total_pixel_limit)throw Error("limit_exceeded","GIF frame plan exceeds 128 million total pixels");
      if(i&&(image.width!=width||image.height!=height))throw Error("invalid_argument","GIF frame dimensions must remain constant");
      if(count>total_pixel_limit-total_pixels)throw Error("limit_exceeded","GIF frames exceed 128 million total pixels");total_pixels+=static_cast<std::size_t>(count);
      if(!i){width=image.width;height=image.height;out.write("GIF89a",6);out.word(width);out.word(height);out.byte(0xf7);out.byte(255);out.byte(0);
        for(unsigned index=0;index<256;++index){out.byte((((index>>5)&7)*255+3)/7);out.byte((((index>>2)&7)*255+3)/7);out.byte(((index&3)*255+1)/3);}
        if(loop){out.byte(0x21);out.byte(0xff);out.byte(11);out.write("NETSCAPE2.0",11);out.byte(3);out.byte(1);out.word(0);out.byte(0);}
      }
      // Every frame replaces the whole canvas; disposal 1 retains it until the
      // next opaque frame. No source alpha can leave stale previous-frame pixels.
      out.byte(0x21);out.byte(0xf9);out.byte(4);out.byte(4);out.word(delays[i]);out.byte(0);out.byte(0);
      out.byte(0x2c);out.word(0);out.word(0);out.word(width);out.word(height);out.byte(0);compressed(out,quantize(image,check),check);
    }
    out.byte(0x3b);out.file.close();if(!out.file)throw Error("storage_error","Cannot close staged animated GIF");check();
    std::error_code error;fs::rename(temporary/"animation.gif",destination,error);if(error)throw Error("storage_error","Cannot finalize staged animated GIF");fs::remove_all(temporary,error);
    return {{"bytes",out.bytes},{"width",width},{"height",height},{"frame_count",delays.size()},{"duration_s",previous/100.},{"loop",loop},{"palette","RGB 3-3-2; alpha composited onto white"}};
  }catch(...){std::error_code ignored;fs::remove_all(temporary,ignored);throw;}
}
}
