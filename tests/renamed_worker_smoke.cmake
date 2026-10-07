# Packaged executables may carry platform suffixes. Geometry workers and job
# coordinators must spawn the running executable itself, never a sibling chosen
# by its development file name. The copy has no agent-3d-cad sibling.
file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}/bin" "${TEST_DIR}/copy")
get_filename_component(suffix "${CAD_EXE}" LAST_EXT)
file(COPY "${CAD_EXE}" DESTINATION "${TEST_DIR}/copy")
get_filename_component(original_name "${CAD_EXE}" NAME)
set(renamed "${TEST_DIR}/bin/agent-3d-cad-renamed-test${suffix}")
file(RENAME "${TEST_DIR}/copy/${original_name}" "${renamed}")
if(EXISTS "${TEST_DIR}/bin/agent-3d-cad${suffix}")
  message(FATAL_ERROR "The renamed executable must not have a development-named sibling")
endif()

# Job admission is a nonblocking lock shared with the coordinator; a caller
# retries workspace_busy, as documented for cad_job.
function(call_tool tool input)
  foreach(attempt RANGE 200)
    execute_process(COMMAND "${renamed}" call "${tool}" --workspace "${TEST_DIR}/workspace" --input "${input}"
      RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE logs TIMEOUT 60)
    if(status EQUAL 0)
      set(reply "${output}" PARENT_SCOPE)
      return()
    endif()
    if(NOT logs MATCHES "workspace_busy")
      break()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 0.02)
  endforeach()
  message(FATAL_ERROR "${tool} through the renamed executable failed: ${logs}")
endfunction()

# Synchronous geometry: one bounded worker spawned by the renamed executable.
call_tool(cad_create "${SOURCE_DIR}/examples/plate.create.json")
string(JSON revision GET "${reply}" revision)
if(NOT revision EQUAL 1)
  message(FATAL_ERROR "Renamed executable did not commit revision 1: ${reply}")
endif()

# Asynchronous job: the coordinator and its worker are both the renamed image.
file(WRITE "${TEST_DIR}/submit.json"
  [=[{"action":"submit","request_id":"renamedQuery","tool":"cad_query","arguments":{"document_id":"plate","revision":1}}]=])
file(WRITE "${TEST_DIR}/get.json" [=[{"action":"get","job_id":"renamedQuery"}]=])
call_tool(cad_job "${TEST_DIR}/submit.json")
set(state "")
foreach(attempt RANGE 600)
  call_tool(cad_job "${TEST_DIR}/get.json")
  string(JSON state GET "${reply}" state)
  if(state STREQUAL "succeeded" OR state STREQUAL "failed" OR state STREQUAL "cancelled" OR state STREQUAL "interrupted")
    break()
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 0.05)
endforeach()
if(NOT state STREQUAL "succeeded")
  message(FATAL_ERROR "Job through the renamed executable did not succeed: ${reply}")
endif()
string(JSON volume GET "${reply}" result summary volume_mm3)
if(volume LESS 1)
  message(FATAL_ERROR "Renamed executable job returned no geometry: ${reply}")
endif()
