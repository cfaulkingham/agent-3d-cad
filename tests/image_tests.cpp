#include "agentcad/image.hpp"
#include "agentcad/storage.hpp"
#include "agentcad/hash.hpp"
#include <iostream>
using namespace agentcad;
namespace {
int checks=0;
void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
template<class F>void invalid(F fn){try{fn();}catch(const Error& e){require(e.code=="invalid_image","Expected image validation failure");return;}throw std::runtime_error("Invalid image accepted");}
RgbaImage pattern(){RgbaImage image{128,97,{}};for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x)for(unsigned c=0;c<4;++c)image.pixels.push_back((x*17+y*31+c*53)&255);return image;}
void run(){
 for(int type:{0,2,3,4,6}){
  const auto bytes=read_text(path_from_utf8(CAD_SOURCE_DIR)/"tests/image_fixtures"/("type-"+std::to_string(type)+".png"));const auto image=decode_png(bytes);
  require(image.width==137&&image.height==65,"Independent PNG dimensions");
  for(unsigned y=0;y<65;++y)for(unsigned x=0;x<137;++x)for(unsigned c=0;c<4;++c){
   unsigned expected=255;if(type==3){constexpr unsigned palette[][4]={{255,0,0,255},{0,255,0,192},{0,0,255,128},{240,230,220,64}};expected=palette[(x+y)%4][c];}
   else if(c<3)expected=(x*17+y*31+((type==0||type==4)?0:c)*53)&255;
   else if(type==4||type==6)expected=(x*17+y*31+(type==4?1:3)*53)&255;
   require(image.pixels[(y*137+x)*4+c]==expected,"Independent zlib/filter/palette pixel oracle");
  }
  require(image_unbase64(image_base64(bytes),bytes.size())==bytes,"Base64 bytes preserve independent fixture");
  invalid([&]{decode_png(bytes,64);});auto bad=bytes;bad[bad.size()/2]^=1;invalid([&]{decode_png(bad);});invalid([&]{decode_png(bytes.substr(0,bytes.size()-1));});invalid([&]{decode_png(bytes+"x");});
 }
 const auto source=pattern();const auto png=encode_png(source);require(decode_png(png).pixels==source.pixels,"Portable PNG roundtrip");require(png.size()<source.pixels.size()/2,"Repetitive native image is compressed");
 for(const auto* b:{"", "A===","AAAA=","AA=A","AB==","AAB=","____","AAAA===="})invalid([&]{image_unbase64(b,64);});
 require(image_unbase64("AA==",1)==std::string(1,'\0'),"Single-byte padding");require(image_unbase64("AAA=",2)==std::string(2,'\0'),"Two-byte padding");invalid([&]{image_unbase64("AAAA",2);});
 invalid([&]{encode_png({0,1,{}});});invalid([&]{encode_png({4097,4097,{}});});invalid([&]{encode_png({1,1,{0,1,2}});});
}
}
int main(int argc,char** argv){try{run();if(argc==2)atomic_text(path_from_utf8(argv[1]),encode_png(pattern()));std::cout<<"image: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
