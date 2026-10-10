#include "agentcad/download.hpp"
#include "agentcad/hash.hpp"
#include "agentcad/jobs.hpp"
#include <set>

namespace agentcad {
namespace {
const std::string scheme="cad-export://";
std::string mime(const std::string& extension) {
  if(extension==".step"||extension==".stp")return "model/step";
  if(extension==".stl")return "model/stl";
  if(extension==".3mf")return "model/3mf";
  if(extension==".pdf")return "application/pdf";
  if(extension==".svg")return "image/svg+xml";
  if(extension==".dxf")return "image/vnd.dxf";
  if(extension==".json")return "application/json";
  if(extension==".csv")return "text/csv";
  throw Error("invalid_argument","Unsupported download file type");
}
bool safe_name(const std::string& name) {
  return !name.empty()&&name.size()<=180&&name!="."&&name!=".."&&name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-")==std::string::npos;
}
void regular_path(const fs::path& root,const fs::path& path) {
  const auto relative=path.lexically_relative(root);
  if(relative.empty()||relative.is_absolute())throw Error("invalid_argument","Download path must remain within its native artifact directory");
  auto current=root;
  if(fs::is_symlink(fs::symlink_status(root)))throw Error("storage_error","Download directories cannot be symlinks");
  for(const auto& piece:relative) {
    if(piece==".."||piece==".")throw Error("invalid_argument","Download path cannot traverse directories");
    current/=piece;
    if(fs::is_symlink(fs::symlink_status(current)))throw Error("storage_error","Download paths cannot contain symlinks");
  }
  if(!fs::is_regular_file(path))throw Error("storage_error","Download artifact is not a regular file");
}
std::string base64(const std::string& bytes) {
  constexpr auto digits="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string encoded;encoded.reserve((bytes.size()+2)/3*4);
  for(std::size_t i=0;i<bytes.size();i+=3) {
    const unsigned a=static_cast<unsigned char>(bytes[i]),b=i+1<bytes.size()?static_cast<unsigned char>(bytes[i+1]):0,c=i+2<bytes.size()?static_cast<unsigned char>(bytes[i+2]):0;
    encoded+=digits[a>>2];encoded+=digits[((a&3)<<4)|(b>>4)];encoded+=i+1<bytes.size()?digits[((b&15)<<2)|(c>>6)]:'=';encoded+=i+2<bytes.size()?digits[c&63]:'=';
  }
  return encoded;
}
}
Json download_link_schema() {
  return {{"type","object"},{"properties",{{"type",{{"const","resource_link"}}},{"uri",{{"type","string"},{"pattern","^cad-export://[a-f0-9]{64}/[A-Za-z0-9._-]{1,180}$"}}},
    {"name",{{"type","string"},{"minLength",1},{"maxLength",180}}},{"mimeType",{{"type","string"}}},{"size",{{"type","integer"},{"minimum",1},{"maximum",download_file_limit}}}}},
    {"required",{"type","uri","name","mimeType","size"}},{"additionalProperties",false}};
}
void attach_export_downloads(Store& store,Json& result) {
  const auto id=text_field(result,"document_id");const auto revision=revision_number(result.at("revision"));
  DocumentLock lock(store.root(),id,LockWait::publication);check_job_cancelled();
  const auto record=store.read(id,revision);const auto source_hash=sha256(record.dump());
  Json paths=Json::array();
  if(result.contains("artifacts"))for(const auto& artifact:result.at("artifacts"))paths.push_back(artifact.at("path"));
  else if(result.contains("plates")) {
    for(const auto& plate:result.at("plates"))paths.push_back(plate.at("path"));
    if(result.contains("layout_path"))paths.push_back(result.at("layout_path"));
  } else paths.push_back(result.at("path"));
  if(paths.size()>128)throw Error("limit_exceeded","Export download set exceeds 128 files");
  const auto folder=store.root()/"downloads";directory(folder);result["downloads"]=Json::array();result["download_errors"]=Json::array();
  std::set<std::string> seen;
  for(const auto& item:paths) {
    check_job_cancelled();const auto path=path_from_utf8(item.get<std::string>());const auto name=path_to_utf8(path.filename());
    if(!seen.insert(path_to_utf8(path)).second)continue;
    try {
      if(!safe_name(name))throw Error("invalid_argument","Export filename cannot be used as a download filename");
      regular_path(store.root()/"exports",path);
      const auto bytes=read_text(path,download_file_limit);if(bytes.empty())throw Error("invalid_argument","Download artifact must not be empty");
      const auto content_hash=sha256(bytes),type=mime(path_to_utf8(path.extension()));
      const Json manifest={{"schema_version",1},{"document_id",id},{"revision",revision},{"source_record_sha256",source_hash},
        {"sha256",content_hash},{"name",name},{"mimeType",type},{"size",bytes.size()}};
      const auto key=sha256(manifest.dump());const auto data=folder/(key+".bin"),metadata=folder/(key+".json");
      // Publish bytes before their manifest. Readers must verify both hashes;
      // an interrupted capture is never mistaken for a completed resource.
      atomic_text(data,bytes,download_file_limit);check_job_cancelled();atomic_text(metadata,manifest.dump());
      result["downloads"].push_back({{"type","resource_link"},{"uri",scheme+key+"/"+name},{"name",name},{"mimeType",type},{"size",bytes.size()}});
    }catch(const Error& error) {
      if(error.code=="job_cancelled"||error.code=="job_timeout")throw;
      result["download_errors"].push_back({{"name",name},{"code",error.code},{"message",error.what()}});
    }
  }
}
Json read_export_download(Store& store,const std::string& uri) {
  if(!uri.starts_with(scheme))throw Error("resource_not_found","Resource not found");
  const auto token=uri.substr(scheme.size());
  if(token.size()<66||token[64]!='/'||token.substr(0,64).find_first_not_of("abcdef0123456789")!=std::string::npos||!safe_name(token.substr(65)))
    throw Error("resource_not_found","Invalid export resource URI");
  const auto key=token.substr(0,64),name=token.substr(65);const auto folder=store.root()/"downloads",metadata=folder/(key+".json"),data=folder/(key+".bin");
  regular_path(folder,metadata);regular_path(folder,data);
  const auto manifest=parse_json(read_text(metadata));
  fields(manifest,{"schema_version","document_id","revision","source_record_sha256","sha256","name","mimeType","size"});
  if(manifest.at("schema_version")!=1||sha256(manifest.dump())!=key||manifest.at("name")!=name||manifest.at("mimeType")!=mime(path_to_utf8(path_from_utf8(name).extension())))
    throw Error("artifact_mismatch","Export resource manifest does not match its URI");
  const auto id=text_field(manifest,"document_id");DocumentLock lock(store.root(),id,LockWait::publication);
  const auto record=store.read(id,revision_number(manifest.at("revision")));
  if(sha256(record.dump())!=text_field(manifest,"source_record_sha256"))throw Error("artifact_mismatch","Export source revision changed");
  const auto bytes=read_text(data,download_file_limit);
  if(bytes.empty()||bytes.size()!=manifest.at("size")||sha256(bytes)!=text_field(manifest,"sha256"))throw Error("artifact_mismatch","Export resource bytes changed");
  return {{"contents",Json::array({{{"uri",uri},{"mimeType",manifest.at("mimeType")},{"blob",base64(bytes)}}})}};
}
}
