add_library(
  nexus_sim_parallel
  STATIC
    src/model.cpp)

add_library(Nexus::sim_parallel ALIAS nexus_sim_parallel)

target_include_directories(nexus_sim_parallel PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nexus_sim_parallel PUBLIC nexus_mips_loader)
nexus_apply_project_options(nexus_sim_parallel)
nexus_set_warnings(nexus_sim_parallel)
nexus_enable_sanitizers(nexus_sim_parallel)
