# Portable archives deliberately contain no SDK headers, compiler or scripts
# needed to execute a model. All dependency inspection happens while installing.
set(AGENTCAD_NOTICE_DIRECTORY "${OpenCASCADE_INSTALL_PREFIX}/share/agentcad-dependency-notices" CACHE PATH
  "Notices installed by the pinned dependency recipe")
set(AGENTCAD_JSON_LICENSE "" CACHE FILEPATH "nlohmann JSON 3.12.0 LICENSE.MIT (for external SDK builds)")
if(NOT AGENTCAD_JSON_LICENSE AND EXISTS "${nlohmann_json_SOURCE_DIR}/LICENSE.MIT")
  set(AGENTCAD_JSON_LICENSE "${nlohmann_json_SOURCE_DIR}/LICENSE.MIT")
endif()
set(AGENTCAD_EXTRA_NOTICE_FILES "" CACHE STRING "Additional notices for nonstandard native dependencies")
if(UNIX AND NOT APPLE)
  find_program(AGENTCAD_PATCHELF patchelf)
endif()
if(MSVC)
  get_filename_component(linker_dir "${CMAKE_LINKER}" DIRECTORY)
  find_program(AGENTCAD_DUMPBIN dumpbin HINTS "${linker_dir}" REQUIRED)
  # Redistributable C/C++ runtime goes beside the executable; no system install
  # or administrator privileges are required on the target Windows machine.
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
  set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
  include(InstallRequiredSystemLibraries)
endif()
get_target_property(AGENTCAD_OCCT_LINKAGE TKernel TYPE)
file(SHA256 "${CMAKE_CURRENT_BINARY_DIR}/generated/viewer.html" AGENTCAD_APP_SHA256)
install(DIRECTORY "${OpenCASCADE_RESOURCE_DIR}/" DESTINATION share/agent-3d-cad/occt)
install(DIRECTORY examples/ DESTINATION share/agent-3d-cad/examples)
install(DIRECTORY skills/native-cad DESTINATION share/agent-3d-cad/skills)
install(FILES docs/DISTRIBUTION.md docs/DEPENDENCIES.md docs/PROTOCOL.md docs/SPEC.md docs/LIVE_VIEWER.md docs/DRAWINGS.md DESTINATION share/agent-3d-cad)
install(FILES packaging/THIRD_PARTY.md DESTINATION share/agent-3d-cad/notices)
configure_file(cmake/InstallBundle.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/InstallBundle.cmake" @ONLY)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/InstallBundle-$<CONFIG>.cmake"
  INPUT "${CMAKE_CURRENT_BINARY_DIR}/InstallBundle.cmake")
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/InstallBundle-$<CONFIG>.cmake")
set(CPACK_PACKAGE_NAME agent-3d-cad)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "agent-3d-cad-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
if(WIN32)
  set(CPACK_GENERATOR ZIP)
else()
  set(CPACK_GENERATOR TGZ)
endif()
if(APPLE)
  # libarchive's pax writer adds AppleDouble sidecars for macOS file metadata,
  # even with COPYFILE_DISABLE. The GNU tar writer preserves native modes and
  # symlinks without adding files outside our integrity manifest.
  set(CPACK_POST_BUILD_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageArchive.cmake")
endif()
include(CPack)
add_custom_target(bundle-check
  COMMAND "${CMAKE_COMMAND}" "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
    "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/bundle-check"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/bundle_smoke.cmake"
  DEPENDS agent-3d-cad USES_TERMINAL)
