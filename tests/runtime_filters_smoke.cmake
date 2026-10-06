cmake_minimum_required(VERSION 3.24)
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR required")
endif()
include("${SOURCE_DIR}/cmake/WindowsRuntimeFilters.cmake")
set(checks 0)
# Reproduce the Windows runner's actual mixed-separator dependency paths,
# as well as normalized and entirely native paths on either system drive.
foreach(path IN ITEMS
    [=[C:\Windows\system32/advapi32.dll]=]
    [=[C:\Windows\system32/user32.dll]=]
    [=[C:\Windows\system32/wsock32.dll]=]
    [=[C:\Windows\system32/kernel32.dll]=]
    [=[C:\Windows\system32\kernel32.dll]=]
    [=[C:/Windows/system32/kernel32.dll]=]
    [=[D:\WINDOWS/System32\kernel32.dll]=]
    [=[d:/windows/SYSTEM32/kernel32.dll]=])
  if(NOT path MATCHES "${AGENTCAD_WINDOWS_SYSTEM_DLL_REGEX}")
    message(FATAL_ERROR "OS dependency was not excluded: ${path}")
  endif()
  math(EXPR checks "${checks}+1")
endforeach()
# Do not hide a missing application DLL, an SDK DLL, or a lookalike directory.
foreach(path IN ITEMS
    "wtdccm.dll" "missing-cad-runtime.dll"
    [=[D:\a\agent-3d-cad\.deps\native\bin\TKernel.dll]=]
    [=[D:/a/agent-3d-cad/.deps/native/bin/freetype.dll]=]
    [=[D:\a\bundle\bin\msvcp140.dll]=]
    [=[C:\Program Files\CAD\Windows\system32\runtime.dll]=]
    [=[C:/Windows/system32-backup/runtime.dll]=]
    [=[C:/Windows/System/runtime.dll]=])
  if(path MATCHES "${AGENTCAD_WINDOWS_SYSTEM_DLL_REGEX}")
    message(FATAL_ERROR "Application dependency incorrectly excluded: ${path}")
  endif()
  math(EXPR checks "${checks}+1")
endforeach()
message(STATUS "Runtime dependency filters: ${checks} checks passed")
