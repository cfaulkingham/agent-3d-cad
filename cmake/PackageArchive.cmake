cmake_minimum_required(VERSION 3.24)
if(NOT CPACK_GENERATOR STREQUAL "TGZ")
  return()
endif()
# Operate only on CPack's own installed tree and generated archive. No source
# filesystem metadata is removed, and no extra archive tool is required.
foreach(archive IN LISTS CPACK_PACKAGE_FILES)
  get_filename_component(parent "${archive}" DIRECTORY)
  set(staged "${parent}/${CPACK_PACKAGE_FILE_NAME}")
  if(NOT EXISTS "${staged}/share/agent-3d-cad/provenance.json")
    message(FATAL_ERROR "Cannot locate the verified native bundle for ${archive}")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E tar czf "${archive}.pending"
    --format=gnutar "${CPACK_PACKAGE_FILE_NAME}" WORKING_DIRECTORY "${parent}"
    COMMAND_ERROR_IS_FATAL ANY)
  file(RENAME "${archive}.pending" "${archive}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E tar tf "${archive}"
    OUTPUT_VARIABLE entries COMMAND_ERROR_IS_FATAL ANY)
  if(entries MATCHES "(^|[\n/])\\._[^\n]*")
    message(FATAL_ERROR "AppleDouble metadata remains in native bundle archive")
  endif()
  message(STATUS "Native archive contains GNU tar entries without AppleDouble sidecars")
endforeach()
