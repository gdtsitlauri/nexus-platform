cmake_minimum_required(VERSION 3.25)

project(
  Nexus
  VERSION 4.1.0
  DESCRIPTION "Nexus educational systems and compiler platform"
  LANGUAGES CXX)

set(NEXUS_VERSION_LABEL "4.1.0" CACHE STRING "Human-readable project version label" FORCE)
set(
  NEXUS_PHASE_LABEL
  "4.0 - study guides"
  CACHE STRING "Current project phase" FORCE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(CheckLanguage)
option(NEXUS_ENABLE_CUDA "Enable optional CUDA benchmark demos." OFF)
set(NEXUS_CUDA_ENABLED OFF CACHE BOOL "Whether the optional CUDA demo is enabled." FORCE)
if(NEXUS_ENABLE_CUDA)
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(NEXUS_CUDA_ENABLED ON CACHE BOOL "Whether the optional CUDA demo is enabled." FORCE)
  else()
    message(WARNING "NEXUS_ENABLE_CUDA=ON but no CUDA compiler was found; GPU demo disabled.")
  endif()
endif()

# NEY709 project option: passes inside an open-source compiler.  LLVM loads pass plugins only on Unix-like hosts.
option(NEXUS_ENABLE_LLVM_PASS "Build the LLVM pass plugin when LLVM development files are found." ON)
set(NEXUS_LLVM_PASS_ENABLED OFF)
if(NEXUS_ENABLE_LLVM_PASS AND NOT WIN32)
  find_package(LLVM CONFIG QUIET)
  if(LLVM_FOUND)
    set(NEXUS_LLVM_PASS_ENABLED ON)
    message(STATUS "LLVM ${LLVM_PACKAGE_VERSION} found: building the NexusPasses plugin")
  else()
    message(STATUS "LLVM development files not found: the LLVM pass plugin is skipped")
  endif()
endif()

include(CTest)
include(cmake/ProjectOptions.cmake)
include(cmake/Warnings.cmake)
include(cmake/Sanitizers.cmake)

add_subdirectory(src)
if(NEXUS_LLVM_PASS_ENABLED)
  add_subdirectory(src/llvm_pass)
endif()
add_subdirectory(tests)

