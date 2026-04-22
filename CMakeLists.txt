cmake_minimum_required(VERSION 3.25)

project(
  Nexus
  VERSION 0.11.0
  DESCRIPTION "Nexus educational systems and compiler platform"
  LANGUAGES CXX)

set(NEXUS_VERSION_LABEL "0.11.0-phase11" CACHE STRING "Human-readable project version label" FORCE)
set(
  NEXUS_PHASE_LABEL
  "11 - HDL, Literature, and Final Validation"
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

include(CTest)
include(cmake/ProjectOptions.cmake)
include(cmake/Warnings.cmake)
include(cmake/Sanitizers.cmake)

add_subdirectory(src)
add_subdirectory(tests)

find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
  configure_file("${PROJECT_SOURCE_DIR}/config/Doxyfile.in" "${PROJECT_BINARY_DIR}/Doxyfile" @ONLY)
  add_custom_target(
    docs
    COMMAND "${DOXYGEN_EXECUTABLE}" "${PROJECT_BINARY_DIR}/Doxyfile"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Generating documentation with Doxygen"
    VERBATIM)
else()
  add_custom_target(
    docs
    COMMAND "${CMAKE_COMMAND}" -E echo "Doxygen not found; docs target skipped."
    COMMENT "Documentation generation is unavailable"
    VERBATIM)
endif()
