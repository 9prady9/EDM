cmake_minimum_required(VERSION 3.12)

# There are three targets in this CMake project:
# - edm_plugin makes the main plugin (DLL) which is loaded by Stata.
# - edm_cli creates a command-line executable to run the core EDM
#   algorithms by reading & saving data to JSON files. This doesn't
#   require Stata to run (unlike the main plugin).
# - gbench creates a Google Benchmarks executable which performs
#   microbenchmarking on the core EDM methods.

# As we use vcpkg for package management, we need to run CMake will
# the vcpkg toolchain file. As such, we require the environment variable
# VCPKG_ROOT to be set before running cmake. This contains.
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()
if(DEFINED ENV{VCPKG_TARGET_TRIPLET} AND NOT DEFINED VCPKG_TARGET_TRIPLET)
  set(VCPKG_TARGET_TRIPLET "$ENV{VCPKG_TARGET_TRIPLET}" CACHE STRING "")
endif()

# Optionally, the ARM_MODE environment variable specifies whether we are
# compiling for ARM or x86, though it defaults to x86 and can normally
# be ignored.
if(DEFINED ENV{ARMMODE})
  set(ARMMODE TRUE CACHE BOOL "Compiling to ARM")
else()
  set(ARMMODE FALSE CACHE BOOL "Compiling to x86")
endif()

# The default behaviour of the 'install' target is to build everything first.
# As we often don't want to build the CLI executable, we disable this behaviour.
set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY TRUE)

project(edm-stata-package CXX)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

# Output paths
set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_SOURCE_DIR})

# Write compile_commands.json file (used by IDEs & static analysers)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Compiler flags
if(WIN32)
  set(SYSTEM "STWIN32" CACHE STRING "Stata plugin SYSTEM option")
else()
  set(SYSTEM "APPLEMAC" CACHE STRING "Stata plugin SYSTEM option")
endif()
set(CACHE SYSTEM PROPERTY STRINGS APPLEMAC HP9000 OPUNIX STWIN32)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSYSTEM=${SYSTEM}")

if(WIN32)
  set(CMAKE_CXX_FLAGS_DEBUG "/MTd /W4 /permissive /ZI /fsanitize=address /bigobj")
  set(CMAKE_CXX_FLAGS_RELEASE "/MT /Ox /fp:fast")
else()
  set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -fsanitize=address")
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -ffast-math")
endif()

# Tell MSVC to compile in parallel
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/MP>)

# Source files common to all targets
set(COMMON_SRC src/common.cpp src/common.h
               src/cpu.cpp src/cpu.h
               src/distances.cpp src/distances.h
               src/edm.cpp src/edm.h
               src/manifold.cpp src/manifold.h
               src/mersennetwister.h
               src/stats.cpp src/stats.h
               src/train_predict_split.h
               src/core.h src/EMD_wrapper.cpp src/EMD_wrapper.h
               src/network_simplex_simple.h src/full_bipartitegraph.h
               src/thread_pool.cpp src/thread_pool.h)

# The main EDM plugin for Stata
add_library(edm_plugin SHARED ${COMMON_SRC} src/stata.cpp vendor/stplugin.cpp vendor/stplugin.h)
set_target_properties(edm_plugin PROPERTIES CXX_STANDARD 17 PREFIX "")

# Rename the output plugin to 'edm_Windows_x64.plugin' or 'edm_MacOSX_arm.plugin' as appropriate
if(WIN32)
  set(BUILD_SUFFIX "_Windows")
elseif(APPLE)
  set(BUILD_SUFFIX "_MacOSX")
else()
  set(BUILD_SUFFIX "_Unix")
endif()

if (ARMMODE)
  set(BUILD_SUFFIX "${BUILD_SUFFIX}_arm")
else()
  set(BUILD_SUFFIX "${BUILD_SUFFIX}_x64")
endif()

set(BUILD_SUFFIX ${BUILD_SUFFIX} CACHE STRING "Compile to edm{BUILD_SUFFIX}.plugin")
set_target_properties(edm_plugin PROPERTIES OUTPUT_NAME "edm")
set_target_properties(edm_plugin PROPERTIES SUFFIX ${BUILD_SUFFIX}.plugin)

# Store output as both bin/edm_OS_ARCH.plugin and test/edm.plugin
install(TARGETS edm_plugin DESTINATION bin)
install(FILES bin/edm${BUILD_SUFFIX}.plugin DESTINATION test RENAME edm.plugin)
install(FILES stata/edm.ado DESTINATION test)

# To make CI builds easier to access, have the outputs be copied to Dropbox also.
# Copy to $EDM_CI_DIR, though if on the master branch, also copy to $EDM_CI_DIR-master.
set(CI_DIR $ENV{EDM_CI_DIR} CACHE STRING "Location of CI output directory")

if(DEFINED ENV{EDM_CI_DIR})
  # Get the current working branch
  execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  install(FILES bin/edm${BUILD_SUFFIX}.plugin stata/edm.ado DESTINATION $ENV{EDM_CI_DIR})
  if (${GIT_BRANCH} STREQUAL master)
    install(FILES bin/edm${BUILD_SUFFIX}.plugin stata/edm.ado DESTINATION $ENV{EDM_CI_DIR}-master)
  endif()
endif()

# Grab vendored files (e.g. the Stata/C interface files)
include(vendor/CMakeLists.txt)
target_include_directories(edm_plugin PRIVATE vendor)
target_include_directories(edm_plugin PRIVATE include)
target_include_directories(edm_plugin PRIVATE include/eigen3)

# ↓↓↓ Third-party code ↓↓↓
find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)

if(NOT WIN32)
  set(THREADS_PREFER_PTHREAD_FLAG ON)
endif()
find_package(Threads REQUIRED)

target_link_libraries(edm_plugin PRIVATE fmt::fmt-header-only)
target_link_libraries(edm_plugin PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(edm_plugin PRIVATE Threads::Threads)

# Formatting with clang-format
include(ClangFormat)
clangformat_setup(${COMMON_SRC} src/benchmark.cpp src/cli.cpp src/cli.h src/test.cpp src/stata.cpp)

# ↓↓↓ Developer builds ↓↓↓
# Command-line interface executable
add_executable(edm_cli ${COMMON_SRC} src/cli.cpp src/cli.h)
set_target_properties(edm_cli PROPERTIES CXX_STANDARD 17)
set_target_properties(edm_cli PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")
target_include_directories(edm_cli PRIVATE include)
target_include_directories(edm_cli PRIVATE include/eigen3)
target_link_libraries(edm_cli PRIVATE fmt::fmt-header-only)
target_link_libraries(edm_cli PRIVATE Threads::Threads)
target_link_libraries(edm_cli PRIVATE nlohmann_json::nlohmann_json)
install(TARGETS edm_cli DESTINATION bin OPTIONAL)
install(TARGETS edm_cli DESTINATION test OPTIONAL)

# Google Benchmarks
add_executable(gbench ${COMMON_SRC} src/benchmark.cpp src/cli.h)
set_target_properties(gbench PROPERTIES CXX_STANDARD 17 PREFIX "")
if(WIN32)
  target_compile_options(gbench PRIVATE "/MD")
endif()
find_package(benchmark CONFIG REQUIRED)
target_link_libraries(gbench PRIVATE benchmark::benchmark benchmark::benchmark_main)
target_include_directories(gbench PRIVATE include)
target_include_directories(gbench PRIVATE include/eigen3)
target_link_libraries(gbench PRIVATE fmt::fmt-header-only)
target_link_libraries(gbench PRIVATE Threads::Threads)
target_link_libraries(gbench PRIVATE nlohmann_json::nlohmann_json)
install(TARGETS gbench DESTINATION test OPTIONAL)

find_package(Catch2 REQUIRED)
add_executable(edm_test ${COMMON_SRC} src/test.cpp)
target_link_libraries(edm_test PRIVATE Catch2::Catch2)

set_target_properties(edm_test PROPERTIES CXX_STANDARD 17)

target_include_directories(edm_test PRIVATE include)
target_include_directories(edm_test PRIVATE include/eigen3)
target_link_libraries(edm_test PRIVATE fmt::fmt-header-only)
target_link_libraries(edm_test PRIVATE Threads::Threads)
target_link_libraries(edm_test PRIVATE nlohmann_json::nlohmann_json)
install(TARGETS edm_test DESTINATION test OPTIONAL)
