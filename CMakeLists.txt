cmake_minimum_required(VERSION 3.24)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" AGENTCAD_VERSION LIMIT_COUNT 1)
if(NOT AGENTCAD_VERSION MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)(-preview\\.[1-9][0-9]*)?$")
  message(FATAL_ERROR "VERSION must be X.Y.Z or X.Y.Z-preview.N")
endif()
project(agent_3d_cad VERSION "${CMAKE_MATCH_1}" LANGUAGES CXX)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(AGENTCAD_PORTABLE_BUNDLE "Bundle native libraries, resources and notices at install time" ON)

# A release pin, not a floating branch: upgrades require geometry regression tests.
find_package(OpenCASCADE 8.0.1 EXACT CONFIG REQUIRED)
# Some OCCT static SDKs export the FreeType dependency as a bare library name.
# Resolve it to the selected platform/toolchain's actual imported library.
if(OpenCASCADE_WITH_FREETYPE AND NOT TARGET freetype)
  find_package(Freetype REQUIRED)
  add_library(freetype ALIAS Freetype::Freetype)
endif()
find_package(nlohmann_json 3.12.0 EXACT CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz
    URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

include(cmake/EmbedViewerApp.cmake)
add_library(cad_core src/json.cpp src/model.cpp src/kernel.cpp src/storage.cpp src/service.cpp src/mcp.cpp src/runtime.cpp src/jobs.cpp src/cache.cpp src/viewer.cpp src/live.cpp src/drawing.cpp src/bom.cpp src/desktop.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/viewer_app.cpp")
target_compile_definitions(cad_core PUBLIC AGENTCAD_VERSION="${AGENTCAD_VERSION}")
include(cmake/CacheIdentity.cmake)
target_include_directories(cad_core PUBLIC include)
target_link_libraries(cad_core PUBLIC nlohmann_json::nlohmann_json
  PRIVATE TKPrim TKBO TKFillet TKMesh TKDESTEP TKDESTL TKOffset TKHLR)
find_package(Threads REQUIRED)
target_link_libraries(cad_core PRIVATE Threads::Threads)
target_include_directories(cad_core SYSTEM PRIVATE "${OpenCASCADE_INCLUDE_DIR}")
if(MSVC)
  target_compile_options(cad_core PRIVATE /W4 /permissive- /EHsc)
  target_compile_options(cad_core PUBLIC /utf-8)
  target_compile_definitions(cad_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  target_compile_options(cad_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(agent-3d-cad src/main.cpp)
target_link_libraries(agent-3d-cad PRIVATE cad_core)
install(TARGETS agent-3d-cad RUNTIME DESTINATION bin)
if(APPLE)
  set_target_properties(agent-3d-cad PROPERTIES INSTALL_RPATH "@executable_path/../lib")
elseif(UNIX)
  set_target_properties(agent-3d-cad PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endif()
if(AGENTCAD_PORTABLE_BUNDLE)
  include(cmake/Bundle.cmake)
endif()

include(CTest)
if(BUILD_TESTING)
  add_test(NAME installer COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/installer-smoke"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/installer_smoke.cmake")
  set_tests_properties(installer PROPERTIES TIMEOUT 30)
  add_test(NAME embedded_assets COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/embedded-assets-smoke"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/embed_viewer_smoke.cmake")
  set_tests_properties(embedded_assets PROPERTIES TIMEOUT 30)
  add_test(NAME runtime_filters COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_filters_smoke.cmake")
  set_tests_properties(runtime_filters PROPERTIES TIMEOUT 30)
  add_test(NAME notices COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/notice_consistency.cmake")
  set_tests_properties(notices PROPERTIES TIMEOUT 30)
  add_executable(cad_tests tests/test_main.cpp)
  target_link_libraries(cad_tests PRIVATE cad_core)
  target_include_directories(cad_tests SYSTEM PRIVATE "${OpenCASCADE_INCLUDE_DIR}")
  # Test programs name the service executable that runs their geometry workers.
  target_compile_definitions(cad_tests PRIVATE CAD_SERVICE_EXE="$<TARGET_FILE:agent-3d-cad>" CAD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_dependencies(cad_tests agent-3d-cad)
  foreach(suite model geometry transactions protocol)
    add_test(NAME ${suite} COMMAND cad_tests ${suite})
    set_tests_properties(${suite} PROPERTIES TIMEOUT 60)
  endforeach()
  add_test(NAME cli_smoke COMMAND "${CMAKE_COMMAND}"
    -DCAD_EXE=$<TARGET_FILE:agent-3d-cad> -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
    -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-smoke -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke.cmake)
  set_tests_properties(cli_smoke PROPERTIES TIMEOUT 60)
  add_test(NAME renamed_worker COMMAND "${CMAKE_COMMAND}"
    -DCAD_EXE=$<TARGET_FILE:agent-3d-cad> -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
    -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/renamed-worker -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/renamed_worker_smoke.cmake)
  set_tests_properties(renamed_worker PROPERTIES TIMEOUT 120)
  foreach(suite topology jobs viewer modeling live app_protocol drawing cache assembly assembly_model assembly_kernel assembly_drawing bom balloon_kernel bom_drawing performance_geometry service)
    add_executable(cad_${suite}_tests tests/${suite}_tests.cpp)
    target_link_libraries(cad_${suite}_tests PRIVATE cad_core)
    target_include_directories(cad_${suite}_tests SYSTEM PRIVATE "${OpenCASCADE_INCLUDE_DIR}")
    target_compile_definitions(cad_${suite}_tests PRIVATE CAD_SERVICE_EXE="$<TARGET_FILE:agent-3d-cad>" CAD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_dependencies(cad_${suite}_tests agent-3d-cad)
    if(suite STREQUAL "jobs")
      add_test(NAME ${suite} COMMAND cad_${suite}_tests $<TARGET_FILE:agent-3d-cad>)
    else()
      add_test(NAME ${suite} COMMAND cad_${suite}_tests)
    endif()
    set_tests_properties(${suite} PROPERTIES TIMEOUT 120)
  endforeach()
  find_program(AGENTCAD_NODE_EXECUTABLE NAMES node nodejs)
  if(AGENTCAD_NODE_EXECUTABLE)
    add_test(NAME desktop_bridge COMMAND "${AGENTCAD_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/desktop_bridge_tests.mjs")
    set_tests_properties(desktop_bridge PROPERTIES TIMEOUT 30)
    add_test(NAME offline_renderer COMMAND "${AGENTCAD_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/viewer_renderer_tests.js")
    set_tests_properties(offline_renderer PROPERTIES TIMEOUT 30)
    add_test(NAME live_ui COMMAND "${AGENTCAD_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/live_ui_tests.mjs")
    set_tests_properties(live_ui PROPERTIES TIMEOUT 30)
    add_test(NAME live_mcp_flow COMMAND "${AGENTCAD_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/live_mcp_flow.mjs" "$<TARGET_FILE:agent-3d-cad>")
    set_tests_properties(live_mcp_flow PROPERTIES TIMEOUT 120)
    add_test(NAME webgl_renderer COMMAND "${AGENTCAD_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/webgl_renderer_tests.mjs" "$<TARGET_FILE:agent-3d-cad>")
    set_tests_properties(webgl_renderer PROPERTIES TIMEOUT 120)
  endif()
endif()
