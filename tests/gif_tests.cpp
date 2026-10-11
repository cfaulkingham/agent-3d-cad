#include "agentcad/gif.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>

using namespace agentcad;
namespace {
int checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
struct DecodedFrame {unsigned delay=0;std::vector<std::uint8_t> indices;unsigned clear_count=0,max_width=0;};
struct Decoded {unsigned width=0,height=0;int loop=-1;std::vector<std::array<unsigned,3>> palette;std::vector<DecodedFrame> frames;};
struct Reader {
  std::string data;std::size_t offset=0;
  unsigned byte(){return static_cast<unsigned char>(data.at(offset++));}
  unsigned word(){const auto lo=byte();return lo|(byte()<<8);}
  std::string bytes(std::size_t count){if(count>data.size()-offset)throw std::runtime_error("GIF block overruns file");auto value=data.substr(offset,count);offset+=count;return value;}
  std::string blocks(){std::string result;for(unsigned n;(n=byte())!=0;)result+=bytes(n);return result;}
};
DecodedFrame lzw(const std::string& bytes,unsigned minimum,std::size_t expected) {
  require(minimum==8,"GIF palette uses the required 8-bit initial symbol size");DecodedFrame result;std::size_t bit=0;unsigned width=9,next=258;
  std::array<std::vector<std::uint8_t>,4096> table;const auto reset=[&]{for(unsigned i=0;i<256;++i)table[i]={static_cast<std::uint8_t>(i)};for(unsigned i=256;i<4096;++i)table[i].clear();width=9;next=258;};reset();std::vector<std::uint8_t> previous;bool ended=false;
  while(bit+width<=bytes.size()*8){unsigned code=0;result.max_width=std::max(result.max_width,width);for(unsigned k=0;k<width;++k,++bit)code|=((static_cast<unsigned char>(bytes.at(bit/8))>>(bit%8))&1)<<k;
    if(code==256){reset();previous.clear();++result.clear_count;continue;}
    if(code==257){ended=true;break;}
    std::vector<std::uint8_t> value;
    if(code<next&&!table[code].empty())value=table[code];else if(code==next&&!previous.empty()){value=previous;value.push_back(previous.front());}else throw std::runtime_error("Invalid LZW dictionary reference");
    if(value.size()>expected-result.indices.size())throw std::runtime_error("LZW output exceeds declared image");result.indices.insert(result.indices.end(),value.begin(),value.end());
    if(!previous.empty()&&next<4096){table[next]=previous;table[next].push_back(value.front());++next;if(next==(1u<<width)&&width<12)++width;}previous=std::move(value);
  }
  require(ended&&result.indices.size()==expected,"Independent LZW decoder reconstructs the exact full frame and sees the end code");return result;
}
Decoded decode(const fs::path& path) {
  Reader r{read_text(path,64*1024*1024)};require(r.bytes(6)=="GIF89a","Native animation uses GIF89a");Decoded result;result.width=r.word();result.height=r.word();const auto packed=r.byte();require(packed==0xf7&&r.byte()==255&&r.byte()==0,"GIF logical screen declares a stable global 256-color palette and white background");
  for(int i=0;i<256;++i){const auto red=r.byte(),green=r.byte(),blue=r.byte();result.palette.push_back({red,green,blue});}unsigned delay=0;bool trailer=false;
  while(r.offset<r.data.size()){const auto marker=r.byte();if(marker==0x3b){trailer=true;break;}
    if(marker==0x21){const auto kind=r.byte();if(kind==0xf9){require(r.byte()==4&&r.byte()==4,"Graphic controls retain complete opaque frames with disposal 1");delay=r.word();require(r.byte()==0&&r.byte()==0,"GIF graphics have no transparent palette entry or reserved flags");}
      else if(kind==0xff){const auto name=r.bytes(r.byte()),payload=r.blocks();require(name=="NETSCAPE2.0"&&payload.size()==3&&payload[0]==1,"Looping uses a bounded standard application extension");result.loop=static_cast<unsigned char>(payload[1])|(static_cast<unsigned char>(payload[2])<<8);}
      else throw std::runtime_error("Unexpected GIF extension");continue;}
    require(marker==0x2c,"GIF frame begins with an image descriptor");require(r.word()==0&&r.word()==0&&r.word()==result.width&&r.word()==result.height&&r.byte()==0,"GIF frames replace the complete canvas without local palettes or interlacing");
    const auto minimum=r.byte();auto frame=lzw(r.blocks(),minimum,static_cast<std::size_t>(result.width)*result.height);frame.delay=delay;result.frames.push_back(std::move(frame));
  }
  require(trailer&&r.offset==r.data.size(),"GIF trailer closes the stream without trailing partial data");return result;
}
void rejected(const fs::path& path,const std::function<void()>& fn,const std::string& code={}){try{fn();}catch(const Error& error){require(code.empty()||error.code==code,"GIF rejects invalid inputs with the required error");require(!fs::exists(path),"A failed or cancelled encode leaves no destination artifact");return;}throw std::runtime_error("Invalid animated GIF accepted");}
RgbaImage solid(unsigned width,unsigned height,unsigned r,unsigned g,unsigned b,unsigned a=255){RgbaImage im{width,height,std::vector<std::uint8_t>(static_cast<std::size_t>(width)*height*4)};for(std::size_t i=0;i<im.pixels.size();i+=4){im.pixels[i]=r;im.pixels[i+1]=g;im.pixels[i+2]=b;im.pixels[i+3]=a;}return im;}
}
int main(int argc,char** argv){const auto directory=temporary_directory(fs::temp_directory_path());try{
  const std::vector<RgbaImage> frames={solid(3,2,255,0,0),solid(3,2,0,255,0),solid(3,2,0,0,255,0)};std::size_t callbacks=0;
  const auto report=export_gif({0,.033,.067,.1},directory/"loop.gif",[&](std::size_t i,double time){require(i==callbacks++&&std::abs(time-std::vector<double>{0,.033,.067}[i])<1e-9,"Streaming callback evaluates each native starting time exactly once");return frames.at(i);},true);const auto loop=decode(directory/"loop.gif");
  require(report.at("frame_count")==3&&report.at("bytes")==fs::file_size(directory/"loop.gif")&&report.at("duration_s")==.1,"GIF reports exact byte count, frame count and quantized duration");require(loop.width==3&&loop.height==2&&loop.loop==0,"Portable animation dimensions and infinite loop are independently decoded");
  require(loop.frames[0].delay==3&&loop.frames[1].delay==4&&loop.frames[2].delay==3,"Rounded time boundaries preserve total duration without cumulative frame drift");
  for(unsigned i=0;i<3;++i){const auto expected=i==0?std::array<unsigned,3>{255,0,0}:i==1?std::array<unsigned,3>{0,255,0}:std::array<unsigned,3>{255,255,255};for(const auto index:loop.frames[i].indices)require(loop.palette.at(index)==expected,"Decoded native animation colors and white alpha composition match source frames");}
  auto noise=solid(512,128,0,0,0);const unsigned levels[8]={0,36,73,109,146,182,219,255},blues[4]={0,85,170,255};std::vector<std::uint8_t> expected;std::uint32_t random=1234567;
  for(std::size_t i=0;i<noise.pixels.size()/4;++i){random^=random<<13;random^=random>>17;random^=random<<5;const auto index=static_cast<std::uint8_t>(random);expected.push_back(index);noise.pixels[i*4]=levels[index>>5];noise.pixels[i*4+1]=levels[(index>>2)&7];noise.pixels[i*4+2]=blues[index&3];}
  export_gif({0,.1,.2},directory/"noise.gif",[&](std::size_t,double){return noise;});const auto decoded=decode(directory/"noise.gif");require(decoded.loop==-1,"A non-looping animation omits the looping extension");for(const auto& frame:decoded.frames){require(frame.indices==expected,"Independent variable-width LZW reconstructs random frame palette indices");require(frame.max_width==12&&frame.clear_count>1,"Dictionary width transitions and full-table resets occur in a real encoded frame");}
  auto boundary=solid(255,1,0,0,0);std::vector<std::uint8_t> edge_indices;for(unsigned i=0;i<255;++i){edge_indices.push_back(static_cast<std::uint8_t>(i));boundary.pixels[i*4]=levels[i>>5];boundary.pixels[i*4+1]=levels[(i>>2)&7];boundary.pixels[i*4+2]=blues[i&3];}
  export_gif({0,.1,.2},directory/"boundary.gif",[&](std::size_t,double){return boundary;});const auto edge=decode(directory/"boundary.gif");for(const auto& frame:edge.frames)require(frame.indices==edge_indices&&frame.max_width==10,"End-of-stream code width grows when the final literal completes a dictionary boundary");
  const auto bad=directory/"failed.gif";for(const auto& timeline:std::vector<std::vector<double>>{{0,.1},{1,2,3},{0,0,.1},{0,.001,.1},{0,700,701},{0,.1,std::numeric_limits<double>::quiet_NaN()},{0,.1,3601}})rejected(bad,[&]{export_gif(timeline,bad,[&](std::size_t,double){return frames[0];});});
  rejected(bad,[&]{export_gif({0,.1,.2},bad,[&](std::size_t i,double){return i?solid(2,2,0,0,0):frames[0];});},"invalid_argument");
  rejected(bad,[&]{export_gif({0,.1,.2},bad,[&](std::size_t,double){auto broken=frames[0];broken.pixels.pop_back();return broken;});},"limit_exceeded");
  rejected(bad,[&]{export_gif({0,.1,.2},bad,[&](std::size_t,double){return RgbaImage{4097,1,{}};});},"limit_exceeded");
  std::vector<double> excessive_plan;for(unsigned i=0;i<=129;++i)excessive_plan.push_back(i*.1);unsigned evaluations=0;
  rejected(bad,[&]{export_gif(excessive_plan,bad,[&](std::size_t,double){++evaluations;return solid(1024,1024,255,0,0);});},"limit_exceeded");require(evaluations==1,"Oversized total pixel plans fail before evaluating subsequent native frames");
  auto entropy=noise;entropy.height=512;entropy.pixels.reserve(512*512*4);for(int i=1;i<4;++i)entropy.pixels.insert(entropy.pixels.end(),noise.pixels.begin(),noise.pixels.end());std::vector<double> byte_plan;for(unsigned i=0;i<=256;++i)byte_plan.push_back(i*.1);
  rejected(bad,[&]{export_gif(byte_plan,bad,[&](std::size_t,double){return entropy;});},"limit_exceeded");
  std::size_t calls=0;rejected(bad,[&]{export_gif({0,.1,.2},bad,[&](std::size_t,double){++calls;if(calls==2)throw Error("job_cancelled","frame evaluation cancelled");return noise;});},"job_cancelled");require(calls==2,"Cancellation after a written first frame removes the temporary animation");
  unsigned budget_checks=0;rejected(bad,[&]{export_gif({0,.1,.2},bad,[&](std::size_t,double){return noise;},false,[&]{if(++budget_checks==8)throw Error("job_cancelled","encoding cancelled");});},"job_cancelled");require(budget_checks==8,"Encoding and quantization cooperate with cancellation within bounded pixel chunks");
  require(std::distance(fs::directory_iterator(directory),fs::directory_iterator{})==3,"Failed encodes clean all temporary files and directories");
  if(argc>1){const auto evidence=path_from_utf8(argv[1]);agentcad::directory(evidence);for(const auto* file:{"loop.gif","noise.gif","boundary.gif"})fs::copy_file(directory/file,evidence/file,fs::copy_options::overwrite_existing);}
  fs::remove_all(directory);std::cout<<"gif: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){fs::remove_all(directory);std::cerr<<error.what()<<'\n';return 1;}}
