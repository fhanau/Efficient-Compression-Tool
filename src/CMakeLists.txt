cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(ect VERSION 0.9.6 LANGUAGES CXX C)
cmake_policy(SET CMP0048 NEW)
# Allow use of ZLIB_ROOT
cmake_policy(SET CMP0074 NEW)
# timestamp does not matter for local external project
cmake_policy(SET CMP0135 NEW)

include(GNUInstallDirs)
include(ExternalProject)

# C++17 is required for <filesystem>/directory support.
set (CMAKE_CXX_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Check that submodules are present only if source was downloaded with git
if(EXISTS "${CMAKE_SOURCE_DIR}/../.git" AND NOT EXISTS "${CMAKE_SOURCE_DIR}/../src/libpng/README")
    message (FATAL_ERROR "Submodules are not initialized. Run \n\tgit submodule update --init --recursive\n within the repository")
endif()

add_library(ect_static STATIC
  ect_core.cpp
  libect.cpp
  gztools.cpp
  jpegtran.cpp
  LzFind.c
  support.cpp
  zopflipng.cpp
  # Add headers so they get added to things like Xcode projects
  gztools.h
  LzFind.h
  main.h
  pngusr.h
  support.h
  mozjpeg/transupp.c
)

add_executable(ect main.cpp)

if(MINGW)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-mno-ms-bitfields>)
  add_compile_options($<$<COMPILE_LANGUAGE:C>:-mno-ms-bitfields>)
endif()

if(NOT MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-sign-compare -Wno-unused -Wno-unused-parameter")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-sign-compare -Wno-unused -Wno-unused-parameter")
endif()

# Use -O3 in release builds
foreach(var CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO)
  if(${var} MATCHES "-O2")
    string(REGEX REPLACE "-O2" "-O3" ${var} "${${var}}")
  endif()
endforeach()

# Detect CPU type and whether we're building 64-bit or 32-bit code, courtesy of mozjpeg
math(EXPR BITS "${CMAKE_SIZEOF_VOID_P} * 8")
string(TOLOWER ${CMAKE_SYSTEM_PROCESSOR} CMAKE_SYSTEM_PROCESSOR_LC)
set(COUNT 1)
foreach(ARCH ${CMAKE_OSX_ARCHITECTURES})
  if(COUNT GREATER 1)
    message(FATAL_ERROR "The libjpeg-turbo build system does not support multiple values in CMAKE_OSX_ARCHITECTURES.")
  endif()
  math(EXPR COUNT "${COUNT}+1")
endforeach()
if(CMAKE_SYSTEM_PROCESSOR_LC MATCHES "x86_64" OR
    CMAKE_SYSTEM_PROCESSOR_LC MATCHES "amd64" OR
    CMAKE_SYSTEM_PROCESSOR_LC MATCHES "i[0-9]86" OR
    CMAKE_SYSTEM_PROCESSOR_LC MATCHES "x86" OR
    CMAKE_SYSTEM_PROCESSOR_LC MATCHES "ia32")
  if(BITS EQUAL 64 OR CMAKE_C_COMPILER_ABI MATCHES "ELF X32")
    set(CPU_TYPE x86_64)
  else()
    set(CPU_TYPE i386)
  endif()
  if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL ${CPU_TYPE})
    set(CMAKE_SYSTEM_PROCESSOR ${CPU_TYPE})
  endif()
elseif(CMAKE_SYSTEM_PROCESSOR_LC STREQUAL "aarch64" OR
    CMAKE_SYSTEM_PROCESSOR_LC MATCHES "^arm")
  if(BITS EQUAL 64)
    set(CPU_TYPE arm64)
  else()
    set(CPU_TYPE arm)
  endif()
else()
  set(CPU_TYPE ${CMAKE_SYSTEM_PROCESSOR_LC})
endif()
if(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64" OR
    CMAKE_OSX_ARCHITECTURES MATCHES "arm64" OR
    CMAKE_OSX_ARCHITECTURES MATCHES "i386")
  set(CPU_TYPE ${CMAKE_OSX_ARCHITECTURES})
endif()
if(MSVC_IDE AND CMAKE_GENERATOR_PLATFORM MATCHES "arm64")
  set(CPU_TYPE arm64)
endif()

#Enable vector extensions. May also add these for ARM if needed.
#32-bit x86 is no longer officially supported and might not work, but do enable the compile flags for it.
if(CPU_TYPE STREQUAL "x86_64" OR CPU_TYPE STREQUAL "i386")
  add_definitions(-DADLER32_SIMD_SSSE3)
  add_definitions(-DINFLATE_CHUNK_SIMD_SSE2)
  add_definitions(-DINFLATE_CHUNK_READ_64LE)
  add_definitions(-DHAS_PCLMUL)
elseif(CPU_TYPE STREQUAL arm64)
  add_definitions(-DADLER32_SIMD_NEON)
  add_definitions(-DINFLATE_CHUNK_SIMD_NEON)
  add_definitions(-DINFLATE_CHUNK_READ_64LE)
  set(PNG_ARM_NEON "on")
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
    OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" OR CMAKE_CXX_COMPILER_ID STREQUAL "ARMClang")
  if(CPU_TYPE STREQUAL "x86_64" OR CPU_TYPE STREQUAL "i386")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mpclmul -msse4.2")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mpclmul -msse4.2")
  elseif(CPU_TYPE STREQUAL arm64)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a+crc")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8-a+crc")
  endif()
endif()

# Enable ZLIB_CONST for everything
add_definitions(-DZLIB_CONST)

# Ensure that zlib target points to our custom zlib zlib
set(ZLIB_ROOT "zlib/")
find_package(ZLIB)

option(ECT_MULTITHREADING "Enable multithreaded processing support" ON)
option(ECT_MP3_SUPPORT "Enable MP3 support (not currently working)" OFF)

add_subdirectory(leanify EXCLUDE_FROM_ALL)
add_subdirectory(lodepng EXCLUDE_FROM_ALL)
add_subdirectory(miniz EXCLUDE_FROM_ALL)
add_subdirectory(zlib EXCLUDE_FROM_ALL)
# Point any later find_package(ZLIB) call (e.g. from the bundled libpng, via
# optipng below) at the zlib target we just configured above, instead of
# letting it independently resolve a different system/vcpkg copy. Two zlibs
# ending up linked into the same binary means two definitions of the same
# symbols (inflate, deflate, crc32, ...); GNU ld silently keeps the first one
# it sees, but stricter linkers (e.g. MSVC's link.exe) treat that as a hard
# "already defined" error.
set(ZLIB_LIBRARY ${CMAKE_CURRENT_BINARY_DIR}/zlib/${CMAKE_STATIC_LIBRARY_PREFIX}zlib${CMAKE_STATIC_LIBRARY_SUFFIX})
set(ZLIB_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/zlib)
add_subdirectory(zopfli EXCLUDE_FROM_ALL)
file(COPY ${CMAKE_SOURCE_DIR}/pngusr.h DESTINATION ${CMAKE_SOURCE_DIR}/libpng/)
add_subdirectory(optipng EXCLUDE_FROM_ALL)
# Mozjpeg changes the install prefix if it thinks the current is defaulted
set(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT FALSE)
if(MSVC)
  # mozjpeg only renames the jpeg-static target's output to libjpeg.a when
  # NOT building with MSVC; under MSVC it keeps the target's own name.
  set(MOZJPEG_LIB_NAME jpeg-static.lib)
else()
  set(MOZJPEG_LIB_NAME libjpeg.a)
endif()
# Use cmake --build rather than hardcoding a build tool: mozjpeg's
# ExternalProject sub-build is configured with the same generator as this
# project (NMake Makefiles, Ninja, Unix Makefiles, ...), and a hardcoded
# `make` only matches one of those - under MSVC/NMake it's simply the wrong
# tool.
#
# BUILD_BYPRODUCTS is required for the Ninja generator specifically: unlike
# Make-based generators, Ninja's dependency graph needs this step's output
# declared explicitly, or it never schedules the mozjpeg sub-build as a
# prerequisite of ect.exe at all and fails immediately with "no known rule
# to make it" for the (never built) library.
ExternalProject_Add(mozjpeg
  URL ${CMAKE_SOURCE_DIR}/mozjpeg
  BUILD_COMMAND ${CMAKE_COMMAND} --build . --target jpeg-static
  BUILD_BYPRODUCTS <BINARY_DIR>/${MOZJPEG_LIB_NAME}
  INSTALL_COMMAND ""
  EXCLUDE_FROM_ALL)
ExternalProject_Get_property(mozjpeg BINARY_DIR)
add_library(mozjpeg-static STATIC IMPORTED GLOBAL)
set_property(TARGET mozjpeg-static PROPERTY IMPORTED_LOCATION ${BINARY_DIR}/${MOZJPEG_LIB_NAME})


target_link_libraries(ect
  leanify::leanify
  lodepng::lodepng
  miniz::miniz
  optipng::optipng
  zopfli::zopfli
  zlib::zlib
  mozjpeg-static
)

# mozjpeg generates some header files that we need to be able to include
target_include_directories(ect
  PRIVATE
  ${BINARY_DIR}/
)
add_dependencies(ect mozjpeg)

if(NOT ECT_MULTITHREADING)
  target_compile_definitions(ect
    PRIVATE
    NOMULTI=1)
endif()

if(ECT_MP3_SUPPORT)
  target_compile_definitions(ect
    PRIVATE
    MP3_SUPPORTED=1)
endif()

install(TARGETS ect RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

target_link_libraries(ect ect_static)

target_include_directories(ect_static
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/mozjpeg
)
