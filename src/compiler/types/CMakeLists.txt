add_library(
  nexus_types
  STATIC
    src/hindley_milner.cpp)

add_library(Nexus::types ALIAS nexus_types)

target_include_directories(nexus_types PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
nexus_apply_project_options(nexus_types)
nexus_set_warnings(nexus_types)
nexus_enable_sanitizers(nexus_types)
