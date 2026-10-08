set(viewer_web_dir "${CMAKE_CURRENT_SOURCE_DIR}/web")
set(viewer_assets viewer.html styles.css bridge.js renderer.js state.js shell.js app.js)
foreach(asset IN LISTS viewer_assets)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${viewer_web_dir}/${asset}")
endforeach()
file(READ "${viewer_web_dir}/viewer.html" viewer_content)
string(REPLACE "\r\n" "\n" viewer_content "${viewer_content}")
foreach(asset IN ITEMS styles bridge renderer state shell app)
  if(asset STREQUAL "styles")
    set(extension css)
  else()
    set(extension js)
  endif()
  file(READ "${viewer_web_dir}/${asset}.${extension}" asset_content)
  string(REPLACE "\r\n" "\n" asset_content "${asset_content}")
  string(TOUPPER "${asset}" token)
  string(REPLACE "@VIEWER_${token}@" "${asset_content}" viewer_content "${viewer_content}")
endforeach()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
# Canonicalize CRLF assets explicitly. Substitution treats HTML as data and
# preserves all other UTF-8 bytes without directives.
# A single substitution inserts data without recursively expanding tokens in
# HTML/JS. Explicit LF output avoids the Windows text writer's CRLF conversion.
string(LENGTH "${viewer_content}" viewer_length)
math(EXPR viewer_write_length "${viewer_length}-1")
string(SUBSTRING "${viewer_content}" ${viewer_write_length} 1 viewer_last_byte)
if(NOT viewer_last_byte STREQUAL "\n")
  message(FATAL_ERROR "The reviewed viewer.html template must end with LF")
endif()
# file(CONFIGURE) terminates its placeholder line; retain exactly the source's
# final newline rather than adding a second one after the inserted content.
string(SUBSTRING "${viewer_content}" 0 ${viewer_write_length} viewer_write_content)
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/viewer.html"
  CONTENT "@viewer_write_content@" @ONLY NEWLINE_STYLE UNIX)
string(HEX "${viewer_content}" viewer_html)
# Byte initialization preserves UTF-8 exactly, with no C++ escaping or MSVC
# string-literal length limit. The browser needs no external JS/CSS resources.
string(REGEX REPLACE "(..)" "0x\\1,\n" viewer_bytes "${viewer_html}")
set(viewer_cpp "#include \"agentcad/app.hpp\"\nnamespace agentcad {\nconst std::string& viewer_app_html() {\n  static constexpr unsigned char bytes[] = {${viewer_bytes}};\n  static const std::string html(reinterpret_cast<const char*>(bytes), sizeof(bytes));\n  return html;\n}\n}\n")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/viewer_app.cpp" CONTENT "${viewer_cpp}" @ONLY)
