# osxcross CMake toolchain file for cross-compiling to macOS from Linux.
# Used by vcpkg triplets and the WoWee build.
# Auto-detects SDK, darwin version, and arch from the osxcross installation
# and the VCPKG_OSX_ARCHITECTURES / CMAKE_OSX_ARCHITECTURES setting.

set(CMAKE_SYSTEM_NAME Darwin)

# ── osxcross paths ──────────────────────────────────────────────────
set(_target_dir "/opt/osxcross/target")
if(DEFINED ENV{OSXCROSS_TARGET_DIR})
    set(_target_dir "$ENV{OSXCROSS_TARGET_DIR}")
endif()

# Auto-detect SDK (pick the newest if several are present)
file(GLOB _sdk_dirs "${_target_dir}/SDK/MacOSX*.sdk")
list(SORT _sdk_dirs)
list(GET _sdk_dirs -1 _sdk_dir)
set(CMAKE_OSX_SYSROOT "${_sdk_dir}" CACHE PATH "" FORCE)

# Deployment target
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "" FORCE)
if(DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
    set(CMAKE_OSX_DEPLOYMENT_TARGET "$ENV{MACOSX_DEPLOYMENT_TARGET}" CACHE STRING "" FORCE)
endif()

# ── auto-detect darwin version from compiler names ──────────────────
file(GLOB _darwin_compilers "${_target_dir}/bin/*-apple-darwin*-clang")
list(GET _darwin_compilers 0 _first_compiler)
get_filename_component(_compiler_name "${_first_compiler}" NAME)
string(REGEX MATCH "apple-darwin[0-9.]+" _darwin_part "${_compiler_name}")

# ── pick architecture ───────────────────────────────────────────────
# CMAKE_OSX_ARCHITECTURES is set by vcpkg from VCPKG_OSX_ARCHITECTURES
if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    set(_arch "arm64")
elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
    set(_arch "x86_64")
elseif(DEFINED ENV{OSXCROSS_ARCH})
    set(_arch "$ENV{OSXCROSS_ARCH}")
else()
    set(_arch "arm64")
endif()

set(_host "${_arch}-${_darwin_part}")
set(CMAKE_SYSTEM_PROCESSOR "${_arch}" CACHE STRING "" FORCE)

# ── compilers ───────────────────────────────────────────────────────
set(CMAKE_C_COMPILER   "${_target_dir}/bin/${_host}-clang"   CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${_target_dir}/bin/${_host}-clang++" CACHE FILEPATH "" FORCE)

# ── tools ───────────────────────────────────────────────────────────
set(CMAKE_AR             "${_target_dir}/bin/${_host}-ar"               CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB         "${_target_dir}/bin/${_host}-ranlib"           CACHE FILEPATH "" FORCE)
set(CMAKE_STRIP          "${_target_dir}/bin/${_host}-strip"            CACHE FILEPATH "" FORCE)
set(CMAKE_INSTALL_NAME_TOOL "${_target_dir}/bin/${_host}-install_name_tool" CACHE FILEPATH "" FORCE)

# ── search paths ────────────────────────────────────────────────────
set(CMAKE_FIND_ROOT_PATH "${_sdk_dir}" "${_target_dir}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
