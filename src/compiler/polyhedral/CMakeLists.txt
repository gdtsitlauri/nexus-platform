add_library(
  nexus_polyhedral
  STATIC
    src/polyhedral.cpp)

add_library(Nexus::polyhedral ALIAS nexus_polyhedral)

target_include_directories(nexus_polyhedral PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
nexus_apply_project_options(nexus_polyhedral)
nexus_set_warnings(nexus_polyhedral)
nexus_enable_sanitizers(nexus_polyhedral)
