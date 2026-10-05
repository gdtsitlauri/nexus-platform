add_library(
  nexus_common
  STATIC
    src/build_info.cpp
    src/banner.cpp
    src/arithmetic.cpp
    src/fpu_lite.cpp
    src/number_systems.cpp)

add_library(Nexus::common ALIAS nexus_common)

target_include_directories(nexus_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
nexus_apply_project_options(nexus_common)
nexus_set_warnings(nexus_common)
nexus_enable_sanitizers(nexus_common)
