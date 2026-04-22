add_library(
  nexus_sim_io
  STATIC
    src/system.cpp)

add_library(Nexus::sim_io ALIAS nexus_sim_io)

target_include_directories(nexus_sim_io PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
nexus_apply_project_options(nexus_sim_io)
nexus_set_warnings(nexus_sim_io)
nexus_enable_sanitizers(nexus_sim_io)
