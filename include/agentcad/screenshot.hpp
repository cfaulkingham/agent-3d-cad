#pragma once
#include "agentcad/storage.hpp"
namespace agentcad {
inline constexpr std::size_t screenshot_byte_limit=2*1024*1024;
Json screenshot_tool_definition();
Json screenshot_reply_schema();
Json screenshot_request_schema();
Json screenshot_submit_schema();
Json capture_view_screenshot(Store& store,const Json& arguments);
// Called only from live synchronization/submission. These functions never
// rebuild geometry or publish source revisions.
Json pending_view_screenshots(Store& store,const std::string& view_id);
Json submit_view_screenshot(Store& store,const Json& arguments);
}
