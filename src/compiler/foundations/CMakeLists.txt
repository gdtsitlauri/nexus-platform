add_library(
  nexus_foundations
  STATIC
    src/foundations.cpp)

add_library(Nexus::foundations ALIAS nexus_foundations)

target_include_directories(nexus_foundations PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
nexus_apply_project_options(nexus_foundations)
nexus_set_warnings(nexus_foundations)
nexus_enable_sanitizers(nexus_foundations)
