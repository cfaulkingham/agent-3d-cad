# Portable archives need no developer SDK, compiler or scripts to execute a
# model. Modified dependency sources accompany the notices; dependency inspection
# happens while installing.
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
# TinyXML2 is compiled from the unchanged pinned release sources. Verify the
# provenance record at configure time and ship complete source/license notices.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinyxml2/PROVENANCE.json" AGENTCAD_ARTIFACT_DEPENDENCIES)
foreach(source tinyxml2.cpp tinyxml2.h LICENSE.txt readme.md)
  string(JSON expected_hash GET "${AGENTCAD_ARTIFACT_DEPENDENCIES}" tinyxml2 files "${source}")
  file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinyxml2/${source}" actual_hash)
  if(NOT actual_hash STREQUAL expected_hash)
    message(FATAL_ERROR "Pinned TinyXML2 source/notice changed: ${source}")
  endif()
endforeach()
install(FILES third_party/tinyxml2/tinyxml2.cpp third_party/tinyxml2/tinyxml2.h
  third_party/tinyxml2/LICENSE.txt third_party/tinyxml2/readme.md third_party/tinyxml2/PROVENANCE.json
  DESTINATION share/agent-3d-cad/notices/tinyxml2)
get_target_property(AGENTCAD_OCCT_LINKAGE TKernel TYPE)
file(SHA256 "${CMAKE_CURRENT_BINARY_DIR}/generated/viewer.html" AGENTCAD_APP_SHA256)
install(DIRECTORY "${OpenCASCADE_RESOURCE_DIR}/" DESTINATION share/agent-3d-cad/occt)
install(DIRECTORY examples/ DESTINATION share/agent-3d-cad/examples)
install(DIRECTORY skills/native-cad DESTINATION share/agent-3d-cad/skills)
install(FILES docs/ANNOTATIONS.md DESTINATION share/agent-3d-cad)
install(FILES docs/DISTRIBUTION.md docs/DEPENDENCIES.md docs/PROTOCOL.md docs/SPEC.md docs/LIVE_VIEWER.md docs/ARTIFACT_REVIEW.md docs/PRESENTATION.md docs/APPEARANCE.md docs/MEASUREMENTS.md docs/SECTIONS.md docs/DRAWINGS.md docs/ASSEMBLIES.md docs/ROBOT_EXPORT.md docs/MANUFACTURING.md docs/FABRICATION_REVIEW.md docs/GCODE_REVIEW.md docs/PRINTER_HANDOFF.md docs/SLICING.md docs/PURCHASED_PARTS.md docs/COMPONENTS.md docs/DEPENDENCY_CACHE.md docs/COMPOSITION_FABRICATION_REVIEW.md DESTINATION share/agent-3d-cad)
install(FILES docs/PLAYBACK.md DESTINATION share/agent-3d-cad)
install(FILES docs/SHELL_OFFSET_THICKEN.md docs/SKETCH_OPERATIONS.md docs/AUTHORING_IMPORTS.md
  docs/RICHER_MODELING.md docs/SHEET_METAL.md docs/PARITY_SHEET_METAL.md docs/MESH_RECONSTRUCTION.md docs/SURFACES.md DESTINATION share/agent-3d-cad)
# References must resolve beside the installed skill in both desktop packages.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/skills/native-cad/SKILL.md")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/skills/native-cad/SKILL.md" AGENTCAD_SKILL_TEXT)
string(REGEX MATCHALL "[A-Z][A-Z_0-9]+[.]md" AGENTCAD_SKILL_REFERENCES "${AGENTCAD_SKILL_TEXT}")
list(REMOVE_DUPLICATES AGENTCAD_SKILL_REFERENCES)
foreach(reference IN LISTS AGENTCAD_SKILL_REFERENCES)
  install(FILES "docs/${reference}" DESTINATION share/agent-3d-cad/skills/native-cad)
endforeach()
install(FILES LICENSE NOTICE packaging/THIRD_PARTY.md DESTINATION share/agent-3d-cad/notices)
configure_file(cmake/InstallBundle.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/InstallBundle.cmake" @ONLY)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/InstallBundle-$<CONFIG>.cmake"
  INPUT "${CMAKE_CURRENT_BINARY_DIR}/InstallBundle.cmake")
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/InstallBundle-$<CONFIG>.cmake")
set(CPACK_PACKAGE_NAME agent-3d-cad)
set(CPACK_PACKAGE_VERSION "${AGENTCAD_VERSION}")
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" package_arch)
if(package_arch MATCHES "^(amd64|x86_64|x64)$")
  set(package_arch x64)
elseif(package_arch MATCHES "^(aarch64|arm64)$")
  set(package_arch arm64)
else()
  message(FATAL_ERROR "Unsupported package architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(CPACK_PACKAGE_FILE_NAME "agent-3d-cad-${AGENTCAD_VERSION}-${CMAKE_SYSTEM_NAME}-${package_arch}")
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
    "-DSLICER_FIXTURE=$<$<TARGET_EXISTS:cad_slicer_fixture>:$<TARGET_FILE:cad_slicer_fixture>>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/bundle_smoke.cmake"
  DEPENDS agent-3d-cad USES_TERMINAL)
