#include "artifact_internal.hpp"
#include <ft2build.h>
#include FT_GZIP_H
#include FT_SYSTEM_H
#include FT_ERRORS_H
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace agentcad::artifact_detail {
namespace {
constexpr std::size_t entry_limit=32*1024*1024;
struct MemoryBudget { std::size_t used=0; };
union Allocation { std::max_align_t alignment; std::size_t bytes; };
void* allocate(FT_Memory memory,long bytes) {
  auto& budget=*static_cast<MemoryBudget*>(memory->user);if(bytes<0||static_cast<std::size_t>(bytes)>1024*1024-budget.used)return nullptr;
  auto p=static_cast<Allocation*>(std::malloc(sizeof(Allocation)+static_cast<std::size_t>(bytes)));if(!p)return nullptr;p->bytes=static_cast<std::size_t>(bytes);budget.used+=p->bytes;return p+1;
}
void release(FT_Memory memory,void*p) {if(!p)return;auto raw=static_cast<Allocation*>(p)-1;static_cast<MemoryBudget*>(memory->user)->used-=raw->bytes;std::free(raw);}
void* resize(FT_Memory memory,long,long bytes,void*p) {if(!p)return allocate(memory,bytes);if(bytes<=0){release(memory,p);return nullptr;}auto old=static_cast<Allocation*>(p)-1;
  auto q=allocate(memory,bytes);if(!q)return nullptr;std::memcpy(q,p,std::min(old->bytes,static_cast<std::size_t>(bytes)));release(memory,p);return q;}
void put32(std::string& out,std::uint32_t value) {for(int n=0;n<4;++n)out.push_back(static_cast<char>((value>>(n*8))&255));}
// The public decoder does not report consumed compressed bytes. Independently
// parse the DEFLATE block structure to reject hidden suffixes and count output.
struct Bits {
  const std::string& bytes;std::size_t bit=0;
  unsigned take(unsigned count) {require(count<=16&&bit+count<=bytes.size()*8,"Truncated DEFLATE stream");unsigned v=0;for(unsigned n=0;n<count;++n)v|=((static_cast<unsigned char>(bytes[bit/8])>>(bit%8))&1u)<<n,++bit;return v;}
  void align(){bit=(bit+7)&~std::size_t(7);}
};
struct Huffman {
  std::map<std::pair<unsigned,unsigned>,unsigned> codes;unsigned maximum=0;
  explicit Huffman(const std::vector<unsigned>&lengths,bool allow_empty=false) {
    std::array<unsigned,16> counts{},next{};for(auto length:lengths){require(length<=15,"Invalid DEFLATE code length");if(length)++counts[length],maximum=std::max(maximum,length);}
    if(!maximum){require(allow_empty,"Empty DEFLATE alphabet");return;}unsigned code=0;int remaining=1;
    for(unsigned n=1;n<=15;++n){remaining=remaining*2-static_cast<int>(counts[n]);require(remaining>=0,"Oversubscribed DEFLATE alphabet");code=(code+counts[n-1])<<1;next[n]=code;}
    require(remaining==0 || (maximum==1&&counts[1]==1),"Incomplete DEFLATE alphabet");
    for(unsigned symbol=0;symbol<lengths.size();++symbol){const auto length=lengths[symbol];if(length)codes[{length,next[length]++}]=symbol;}
  }
  unsigned symbol(Bits& bits)const {require(maximum,"Missing DEFLATE distance alphabet");unsigned code=0;for(unsigned n=1;n<=maximum;++n){code=(code<<1)|bits.take(1);const auto it=codes.find({n,code});if(it!=codes.end())return it->second;}invalid("Invalid DEFLATE Huffman symbol");}
};
void structure(const std::string& raw,std::size_t expected) {
  Bits bits{raw};std::size_t output=0;bool last=false;unsigned blocks=0;
  constexpr unsigned length_base[]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
  constexpr unsigned length_bits[]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
  constexpr unsigned distance_base[]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
  constexpr unsigned distance_bits[]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
  while(!last){check_job_cancelled();require(++blocks<=100000,"DEFLATE block budget exceeded");last=bits.take(1);const auto type=bits.take(2);require(type!=3,"Reserved DEFLATE block type");
    if(type==0){bits.align();const auto count=bits.take(16);require((count^bits.take(16))==65535,"Bad DEFLATE stored-block length");require(count<=expected-output,"DEFLATE expansion exceeds declared size");output+=count;require(bits.bit/8+count<=raw.size(),"Truncated stored DEFLATE block");bits.bit+=count*8;continue;}
    std::vector<unsigned> lit,dist;
    if(type==1){lit.resize(288);for(unsigned n=0;n<288;++n)lit[n]=n<144?8:n<256?9:n<280?7:8;dist.assign(32,5);}
    else {const auto nl=bits.take(5)+257,nd=bits.take(5)+1,nc=bits.take(4)+4;require(nl<=286,"Reserved literal alphabet size");
      // RFC 1951's 19-entry permutation, explicit to avoid numeric aliasing.
      constexpr unsigned actual_order[]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
      std::vector<unsigned> lengths(19);for(unsigned n=0;n<nc;++n)lengths[actual_order[n]]=bits.take(3);Huffman alphabet(lengths);lengths.clear();
      while(lengths.size()<nl+nd){const auto value=alphabet.symbol(bits);if(value<16)lengths.push_back(value);else{unsigned count=0,repeat=0;if(value==16){require(!lengths.empty(),"Repeat before DEFLATE code length");repeat=lengths.back();count=bits.take(2)+3;}else if(value==17)count=bits.take(3)+3;else count=bits.take(7)+11;
          require(count<=nl+nd-lengths.size(),"DEFLATE code-length repeat overflow");lengths.insert(lengths.end(),count,repeat);}}
      lit.assign(lengths.begin(),lengths.begin()+nl);dist.assign(lengths.begin()+nl,lengths.end());}
    require(lit[256]!=0,"DEFLATE block has no end symbol");Huffman literals(lit),distances(dist,true);
    for(std::size_t symbols=0;;++symbols){if(symbols%65536==0)check_job_cancelled();const auto symbol=literals.symbol(bits);if(symbol==256)break;if(symbol<256){require(output<expected,"DEFLATE expansion exceeds declared size");++output;continue;}
      require(symbol>=257&&symbol<=285,"Reserved DEFLATE length symbol");const auto length=length_base[symbol-257]+bits.take(length_bits[symbol-257]);const auto d=distances.symbol(bits);require(d<30,"Reserved DEFLATE distance symbol");const auto distance=distance_base[d]+bits.take(distance_bits[d]);
      require(distance<=output&&length<=expected-output,"Invalid DEFLATE back-reference or expansion");output+=length;}
  }
  require(output==expected&&(bits.bit+7)/8==raw.size(),"DEFLATE actual size or consumed input differs from ZIP declaration");
}
std::string inflate(const std::string& raw,std::uint32_t checksum,std::uint32_t size) {
  structure(raw,size);std::string wrapped("\x1f\x8b\x08\0\0\0\0\0\0\0",10);wrapped+=raw;put32(wrapped,checksum);put32(wrapped,size);
  std::string output(std::max<std::uint32_t>(size,1), '\0');FT_ULong actual=output.size();MemoryBudget budget;FT_MemoryRec_ memory{&budget,allocate,release,resize};
  const auto error=FT_Gzip_Uncompress(&memory,reinterpret_cast<FT_Byte*>(output.data()),&actual,reinterpret_cast<const FT_Byte*>(wrapped.data()),wrapped.size());
  if(error==FT_Err_Unimplemented_Feature)unsupported("This native SDK was built without FreeType gzip support");
  require(error==0&&actual==size,"Invalid or truncated ZIP DEFLATE/gzip wrapper");output.resize(size);return output;
}
}
std::map<std::string,std::string> zip(const std::string& bytes) {
  require(bytes.size()>=22&&bytes.size()<=artifact_input_limit,"Invalid ZIP size");std::size_t end=std::string::npos;
  for(std::size_t n=bytes.size()-22;;--n){if(u32(bytes,n)==0x06054b50u&&n+22+u16(bytes,n+20)==bytes.size()){end=n;break;}if(n==0||bytes.size()-n>65557)break;}
  require(end!=std::string::npos,"ZIP end record missing");if(u16(bytes,end+4)||u16(bytes,end+6)||u16(bytes,end+8)!=u16(bytes,end+10))unsupported("Multi-disk ZIP is unsupported");
  const auto count=u16(bytes,end+10);const auto central_size=u32(bytes,end+12),central=u32(bytes,end+16);
  if(count==65535||central_size==0xffffffffu||central==0xffffffffu)unsupported("ZIP64 is unsupported in this review subset");
  require(count>0&&count<=256&&central<=end&&central_size==end-central,"Invalid or excessive ZIP directory");
  std::size_t offset=central,total=0;std::set<std::string> portable_names;std::vector<std::pair<std::size_t,std::size_t>> ranges;std::map<std::string,std::string> entries;
  for(unsigned item=0;item<count;++item){check_job_cancelled();require(offset+46<=end&&u32(bytes,offset)==0x02014b50u,"Invalid ZIP central header");
    const auto flags=u16(bytes,offset+8),method=u16(bytes,offset+10);if((flags&~0x80eu)||(method!=0&&method!=8))unsupported("Encrypted or unsupported ZIP compression/flags");
    const auto checksum=u32(bytes,offset+16),compressed=u32(bytes,offset+20),expanded=u32(bytes,offset+24),local=u32(bytes,offset+42);
    const auto name_len=u16(bytes,offset+28),extra=u16(bytes,offset+30),comment=u16(bytes,offset+32);require(offset+46+name_len+extra+comment<=end&&u16(bytes,offset+34)==0,"Truncated or multi-disk ZIP entry");
    if(compressed==0xffffffffu||expanded==0xffffffffu||local==0xffffffffu)unsupported("ZIP64 entry is unsupported");
    require(expanded<=entry_limit&&expanded<=artifact_expansion_limit-total,"ZIP expansion budget exceeded");total+=expanded;
    const auto name=bytes.substr(offset+46,name_len);relative_uri(name,name.ends_with('/'));auto lower=name;for(auto&c:lower)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));require(portable_names.insert(lower).second,"Duplicate or case-colliding ZIP entry");
    const auto mode=(u32(bytes,offset+38)>>16)&0170000u;require(mode==0||mode==0100000u||mode==0040000u,"ZIP symlink or special entry is prohibited");require(mode!=0040000u||name.ends_with('/'),"ZIP directory mode/name mismatch");
    require(local+30<=central&&u32(bytes,local)==0x04034b50u,"Invalid ZIP local header");require(u16(bytes,local+6)==flags&&u16(bytes,local+8)==method,"ZIP local/central flags differ");
    const auto local_name=u16(bytes,local+26),local_extra=u16(bytes,local+28);const std::size_t data=static_cast<std::size_t>(local)+30+local_name+local_extra;
    require(data<=central&&compressed<=central-data&&local_name==name_len&&bytes.compare(local+30,local_name,name)==0,"ZIP local name or compressed bounds differ");
    if(!(flags&8))require(u32(bytes,local+14)==checksum&&u32(bytes,local+18)==compressed&&u32(bytes,local+22)==expanded,"ZIP local/central size or CRC differs");
    else require((u32(bytes,local+14)==0||u32(bytes,local+14)==checksum)&&(u32(bytes,local+18)==0||u32(bytes,local+18)==compressed)&&(u32(bytes,local+22)==0||u32(bytes,local+22)==expanded),"ZIP descriptor local values differ");
    std::size_t finish=data+compressed;if(flags&8){require(finish+12<=central,"Missing ZIP data descriptor");if(u32(bytes,finish)==0x08074b50u)finish+=4;
      require(finish+12<=central&&u32(bytes,finish)==checksum&&u32(bytes,finish+4)==compressed&&u32(bytes,finish+8)==expanded,"ZIP descriptor size/CRC differs");finish+=12;}
    for(const auto&range:ranges)require(finish<=range.first||local>=range.second,"Overlapping ZIP entries");ranges.push_back({local,finish});
    const auto raw=bytes.substr(data,compressed);std::string decoded;if(method==0){require(compressed==expanded,"Stored ZIP entry size differs");decoded=raw;}else decoded=inflate(raw,checksum,expanded);
    require(decoded.size()==expanded&&crc32(decoded)==checksum,"ZIP actual length or independent CRC mismatch");
    if(name.ends_with('/'))require(decoded.empty(),"ZIP directory contains data");else entries.emplace(name,std::move(decoded));
    offset+=46+name_len+extra+comment;
  }
  require(offset==end,"ZIP central directory size differs");return entries;
}
}
