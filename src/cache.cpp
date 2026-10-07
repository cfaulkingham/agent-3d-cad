#include "agentcad/cache.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/kernel.hpp"
#include <algorithm>
#include <fstream>
#include <vector>

namespace agentcad {
namespace {
void write_entry(const fs::path& path,const std::string& bytes) {
  if(bytes.size()>cache_entry_bytes) return;
  const auto temporary=temporary_file(path.parent_path());
  try {
    std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);
    stream.write(bytes.data(),static_cast<std::streamsize>(bytes.size())); stream.close();
    if(!stream) throw Error("storage_error","Cannot write cache entry");
    publish_file(temporary,path);
  } catch(...) { std::error_code ignored; fs::remove(temporary,ignored); throw; }
}
bool valid_key(const std::string& key) {
  return key.size()==64 && key.find_first_not_of("0123456789abcdef")==std::string::npos;
}
std::string key_for(const Json& value) {
  return sha256(Json{{"format",1},{"build",AGENTCAD_CACHE_BUILD},
    {"kernel",kernel_version()},{"intent",value}}.dump());
}
Json unpack(const std::string& bytes, const std::string& key) {
  const auto envelope=parse_json(bytes,cache_entry_bytes);
  if(envelope.at("key")!=key) throw Error("cache_miss","Cache key mismatch");
  const auto& payload=envelope.at("payload").get_ref<const std::string&>();
  if(envelope.at("sha256")!=sha256(payload)) throw Error("cache_miss","Cache checksum mismatch");
  return parse_json(payload,cache_entry_bytes);
}
}
std::string geometry_cache_key(const Json& model) {
  return key_for({{"kind","geometry"},{"model",model}});
}
std::string projection_cache_key(const std::string& geometry_key, const Json& projection) {
  return key_for({{"kind","projection"},{"geometry",geometry_key},{"projection",projection}});
}
std::optional<Json> read_cache(const fs::path& root, const std::string& key) {
  try {
    if(!valid_key(key) || fs::is_symlink(fs::symlink_status(root))) return {};
    return unpack(read_text(root/(key+".json"),cache_entry_bytes),key);
  } catch(const std::exception&) { return {}; }
}
void stage_cache(const fs::path& path, const std::string& key, const Json& payload) {
  try {
    if(!valid_key(key)) return;
    const auto content=payload.dump();
    if(content.size()>cache_entry_bytes) return;
    const auto bytes=Json{{"key",key},{"sha256",sha256(content)},{"payload",content}}.dump();
    if(bytes.size()<=cache_entry_bytes) write_entry(path,bytes);
  } catch(const std::exception&) { /* A cache is never required for success. */ }
}
void publish_cache(const fs::path& root, const fs::path& staged, const std::string& key) {
  try {
    if(!valid_key(key)) return;
    const auto bytes=read_text(staged,cache_entry_bytes);
    // This bounded file was written by our successful worker, not model code.
    // Verify before publication, including when a process was interrupted.
    unpack(bytes,key);
    directory(root);
    WorkspaceLock lock(root);
    struct Entry { fs::path path; std::uintmax_t bytes; fs::file_time_type time; };
    std::vector<Entry> entries;
    std::uintmax_t total=0;
    const auto target=root/(key+".json");
    for(const auto& item:fs::directory_iterator(root)) {
      if(item.is_symlink()) return;
      if(item.is_regular_file() && path_to_utf8(item.path().filename()).starts_with(".pending-")) {
        fs::remove(item.path()); continue; // Recover an interrupted atomic write.
      }
      if(!item.is_regular_file() || item.path().extension()!=".json") continue;
      if(item.path()==target) continue;
      const auto size=item.file_size(); total+=size;
      entries.push_back({item.path(),size,item.last_write_time()});
    }
    std::sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.time<b.time;});
    std::size_t removed=0;
    while(removed<entries.size() && (total+bytes.size()>cache_total_bytes || entries.size()-removed>=cache_max_entries)) {
      const auto& entry=entries[removed++]; fs::remove(entry.path); total-=entry.bytes;
    }
    write_entry(target,bytes);
  } catch(const std::exception&) { /* Busy, missing, unwritable or damaged: skip. */ }
}
}
