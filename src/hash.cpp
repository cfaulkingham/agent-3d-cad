#include "agentcad/hash.hpp"
#include "agentcad/storage.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace agentcad {
namespace {
class Digest {
  static constexpr std::array<std::uint32_t,64> constants = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
  std::array<std::uint32_t,8> state = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  std::array<std::uint8_t,64> pending{};std::size_t used=0;std::uint64_t bytes=0;
  void block(const std::uint8_t* data) {
    auto rotate=[](std::uint32_t value,int amount){return (value>>amount)|(value<<(32-amount));};
    std::array<std::uint32_t,64> words{};
    for (int i = 0; i < 16; ++i) for (int j = 0; j < 4; ++j) words[i] = (words[i] << 8) | data[i*4+j];
    for (int i = 16; i < 64; ++i) {
      const auto a = words[i-15], b = words[i-2];
      words[i] = words[i-16] + (rotate(a,7)^rotate(a,18)^(a>>3)) + words[i-7] + (rotate(b,17)^rotate(b,19)^(b>>10));
    }
    auto [a,b,c,d,e,f,g,h] = state;
    for (int i = 0; i < 64; ++i) {
      const auto first = h + (rotate(e,6)^rotate(e,11)^rotate(e,25)) + ((e&f)^(~e&g)) + constants[i] + words[i];
      const auto second = (rotate(a,2)^rotate(a,13)^rotate(a,22)) + ((a&b)^(a&c)^(b&c));
      h=g; g=f; f=e; e=d+first; d=c; c=b; b=a; a=first+second;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d; state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
  }
public:
  void update(std::string_view value) {
    bytes+=value.size();
    while(!value.empty()) {
      const auto n=std::min<std::size_t>(64-used,value.size());
      std::memcpy(pending.data()+used,value.data(),n);used+=n;value.remove_prefix(n);
      if(used==64){block(pending.data());used=0;}
    }
  }
  std::string finish() {
    const auto bits=bytes*8;pending[used++]=0x80;
    if(used>56){std::fill(pending.begin()+used,pending.end(),0);block(pending.data());used=0;}
    std::fill(pending.begin()+used,pending.begin()+56,0);
    for(int i=0;i<8;++i)pending[56+i]=static_cast<std::uint8_t>(bits>>(56-8*i));
    block(pending.data());std::ostringstream result;result<<std::hex<<std::setfill('0');
    for(const auto value:state)result<<std::setw(8)<<value;
    return result.str();
  }
};
}
std::string sha256(const std::string& text){Digest digest;digest.update(text);return digest.finish();}
std::string sha256_file(const std::filesystem::path& path,std::size_t max_bytes,const std::function<void()>& checkpoint) {
  std::ifstream input(path,std::ios::binary);if(!input)throw Error("not_found","Cannot read checksummed file");
  Digest digest;std::array<char,65536> chunk{};std::size_t total=0;
  while(input) {
    if(checkpoint)checkpoint();input.read(chunk.data(),chunk.size());const auto n=static_cast<std::size_t>(input.gcount());
    if(n>max_bytes-total)throw Error("limit_exceeded","Checksummed file exceeds byte limit");
    total+=n;digest.update(std::string_view(chunk.data(),n));
  }
  if(!input.eof())throw Error("storage_error","Cannot finish reading checksummed file");
  return digest.finish();
}
}
