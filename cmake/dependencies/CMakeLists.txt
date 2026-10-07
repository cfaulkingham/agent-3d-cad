cmake_minimum_required(VERSION 3.24)
project(agentcad_dependencies LANGUAGES C CXX)
include(ExternalProject)

set(AGENTCAD_DEPS_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Dependency installation directory")
set(AGENTCAD_JOBS 2 CACHE STRING "Parallel OCCT compiler jobs")
set(AGENTCAD_OCCT_ARCHIVE "" CACHE FILEPATH "Optional local copy of the pinned OCCT source archive")
set(AGENTCAD_FREETYPE_ARCHIVE "" CACHE FILEPATH "Optional local copy of pinned FreeType source archive")
option(AGENTCAD_SYSTEM_FREETYPE "Use host FreeType instead of the pinned standalone build (developer builds only)" OFF)
set(archive "https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V8_0_1.tar.gz")
if(AGENTCAD_OCCT_ARCHIVE)
  set(archive "${AGENTCAD_OCCT_ARCHIVE}")
endif()

set(platform_options)
if(UNIX AND NOT APPLE)
  # DT_RUNPATH on a service resolves only direct dependencies. Each SDK library
  # must also resolve its siblings, including the pinned FreeType build.
  list(APPEND platform_options "-DCMAKE_INSTALL_RPATH:STRING=$ORIGIN")
endif()
foreach(variable CMAKE_TOOLCHAIN_FILE CMAKE_SYSROOT CMAKE_OSX_ARCHITECTURES CMAKE_OSX_SYSROOT CMAKE_OSX_DEPLOYMENT_TARGET)
  if(DEFINED ${variable} AND NOT "${${variable}}" STREQUAL "")
    string(REPLACE ";" "|" forwarded "${${variable}}")
    list(APPEND platform_options "-D${variable}:STRING=${forwarded}")
  endif()
endforeach()
if(AGENTCAD_SYSTEM_FREETYPE)
  find_package(Freetype REQUIRED)
  list(GET FREETYPE_INCLUDE_DIRS 0 ft_include)
  get_filename_component(ft_library_dir "${FREETYPE_LIBRARY}" DIRECTORY)
  get_filename_component(ft_prefix "${ft_library_dir}" DIRECTORY)
else()
  set(ft_archive "https://downloads.sourceforge.net/project/freetype/freetype2/2.14.3/freetype-2.14.3.tar.xz")
  if(AGENTCAD_FREETYPE_ARCHIVE)
    set(ft_archive "${AGENTCAD_FREETYPE_ARCHIVE}")
  endif()
  ExternalProject_Add(freetype_build
    URL "${ft_archive}"
    URL_HASH SHA256=36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    LIST_SEPARATOR "|"
    CMAKE_ARGS "-DCMAKE_INSTALL_PREFIX:PATH=${AGENTCAD_DEPS_PREFIX}"
      -DCMAKE_BUILD_TYPE:STRING=Release -DBUILD_SHARED_LIBS:BOOL=ON
      "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
      -DFT_DISABLE_ZLIB:BOOL=ON -DFT_DISABLE_BZIP2:BOOL=ON -DFT_DISABLE_PNG:BOOL=ON
      -DFT_DISABLE_HARFBUZZ:BOOL=ON -DFT_DISABLE_BROTLI:BOOL=ON ${platform_options}
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --config Release --parallel ${AGENTCAD_JOBS}
    INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR> --config Release)
  ExternalProject_Get_Property(freetype_build SOURCE_DIR)
  set(ft_source "${SOURCE_DIR}")
  set(ft_prefix "${AGENTCAD_DEPS_PREFIX}")
  set(ft_include "${ft_prefix}/include/freetype2")
  set(ft_library_dir "${ft_prefix}/lib")
  if(WIN32)
    set(FREETYPE_LIBRARY "${ft_library_dir}/freetype.lib")
  else()
    set(FREETYPE_LIBRARY "${ft_library_dir}/${CMAKE_SHARED_LIBRARY_PREFIX}freetype${CMAKE_SHARED_LIBRARY_SUFFIX}")
  endif()
endif()

set(options
  "-DCMAKE_INSTALL_PREFIX:PATH=${AGENTCAD_DEPS_PREFIX}"
  "-DCMAKE_BUILD_TYPE:STRING=Release"
  "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
  "-DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}"
  "-DCMAKE_EXPORT_NO_PACKAGE_REGISTRY:BOOL=ON"
  "-DBUILD_LIBRARY_TYPE:STRING=Shared"
  "-DBUILD_CPP_STANDARD:STRING=C++17"
  "-DBUILD_RELEASE_DISABLE_EXCEPTIONS:BOOL=OFF"
  "-DBUILD_ADDITIONAL_TOOLKITS:STRING=TKPrim|TKBO|TKFillet|TKMesh|TKDESTEP|TKDESTL|TKOffset"
  "-DINSTALL_DIR_LAYOUT:STRING=Unix"
  "-DINSTALL_DIR_LIB:PATH=lib"
  "-DINSTALL_DIR_INCLUDE:PATH=include/opencascade"
  "-DINSTALL_DIR_WITH_VERSION:BOOL=OFF"
  "-DUSE_FREETYPE:BOOL=ON"
  "-D3RDPARTY_FREETYPE_DIR:PATH=${ft_prefix}"
  "-D3RDPARTY_FREETYPE_INCLUDE_DIR_ft2build:PATH=${ft_include}"
  "-D3RDPARTY_FREETYPE_INCLUDE_DIR_freetype2:PATH=${ft_include}"
  "-D3RDPARTY_FREETYPE_LIBRARY_DIR:PATH=${ft_library_dir}"
  "-D3RDPARTY_FREETYPE_LIBRARY:FILEPATH=${FREETYPE_LIBRARY}"
  "-D3RDPARTY_FREETYPE_DLL_DIR:PATH=${ft_prefix}/bin"
  ${platform_options})
foreach(module FoundationClasses ModelingData ModelingAlgorithms Visualization ApplicationFramework DataExchange Draw)
  list(APPEND options "-DBUILD_MODULE_${module}:BOOL=OFF")
endforeach()
foreach(product D3D DRACO FFMPEG FREEIMAGE GLES2 OPENVR RAPIDJSON TBB TCL TK VTK XLIB)
  list(APPEND options "-DUSE_${product}:BOOL=OFF")
endforeach()

if(NOT AGENTCAD_SYSTEM_FREETYPE)
  set(occt_dependencies freetype_build)
endif()
ExternalProject_Add(occt
  DEPENDS ${occt_dependencies}
  URL "${archive}"
  URL_HASH SHA256=0d6913eae4bcc09a3653ceced6dda1aec11c35a1513d4c06762c9b002092c68a
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  LIST_SEPARATOR "|"
  PATCH_COMMAND "${CMAKE_COMMAND}" "-DOCCT_SOURCE_DIR=<SOURCE_DIR>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/PatchOcctHlr.cmake"
  CMAKE_ARGS ${options}
  BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --config Release --parallel ${AGENTCAD_JOBS}
  INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR> --config Release)
# A command's script path alone does not invalidate an ExternalProject stamp
# when that script changes. This input step propagates changes into patching.
ExternalProject_Add_Step(occt patch_inputs
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/PatchOcctHlr.cmake"
  DEPENDEES update DEPENDERS patch INDEPENDENT TRUE)
ExternalProject_Add_StepTargets(occt configure)
ExternalProject_Get_Property(occt SOURCE_DIR)
set(occt_source "${SOURCE_DIR}")
configure_file(InstallNotices.cmake.in InstallNotices.cmake @ONLY)
ExternalProject_Add_Step(occt notices
  COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/InstallNotices.cmake"
  DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/InstallNotices.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/InstallNotices.cmake.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/PatchOcctHlr.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/OCCT-HLR-PATCH.md"
  DEPENDEES install)
