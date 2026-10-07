# Changes to service sources/headers or build settings invalidate disposable
# caches automatically, including uncommitted developer builds.
file(GLOB cache_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/agentcad/*.hpp")
list(APPEND cache_sources "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" "${CMAKE_CURRENT_LIST_FILE}")
# Bind to the selected native SDK binaries as well as the pinned version. Skip
# aliases so relocated copies have the same identity as their original build.
file(GLOB cache_kernel_libraries "${OpenCASCADE_LIBRARY_DIR}/libTK*.dylib"
  "${OpenCASCADE_LIBRARY_DIR}/libTK*.so*" "${OpenCASCADE_LIBRARY_DIR}/libTK*.a"
  "${OpenCASCADE_LIBRARY_DIR}/TK*.lib" "${OpenCASCADE_BINARY_DIR}/TK*.dll")
foreach(library IN LISTS cache_kernel_libraries)
  if(NOT IS_SYMLINK "${library}")
    list(APPEND cache_sources "${library}")
  endif()
endforeach()
set(cache_identity "${CMAKE_CXX_COMPILER_ID};${CMAKE_CXX_COMPILER_VERSION};${CMAKE_SYSTEM_NAME};${CMAKE_SYSTEM_PROCESSOR};${CMAKE_OSX_ARCHITECTURES};${CMAKE_SIZEOF_VOID_P};${CMAKE_CXX_FLAGS};${CMAKE_CXX_FLAGS_DEBUG};${CMAKE_CXX_FLAGS_RELEASE};${CMAKE_CXX_FLAGS_RELWITHDEBINFO};${CMAKE_CXX_FLAGS_MINSIZEREL}")
foreach(source IN LISTS cache_sources)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
  file(SHA256 "${source}" digest)
  string(APPEND cache_identity "${digest}")
endforeach()
string(SHA256 cache_identity "${cache_identity}")
target_compile_definitions(cad_core PRIVATE AGENTCAD_CACHE_BUILD="${cache_identity}-$<CONFIG>")
