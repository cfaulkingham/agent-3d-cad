#pragma once
#include "agentcad/storage.hpp"

namespace agentcad {
inline constexpr std::size_t download_file_limit=64*1024*1024;
Json download_link_schema();
// Capture only files produced by these native export calls. No caller-supplied
// paths or arbitrary workspace resource reads are accepted.
void attach_export_downloads(Store& store,Json& result);
Json read_export_download(Store& store,const std::string& uri);
}
