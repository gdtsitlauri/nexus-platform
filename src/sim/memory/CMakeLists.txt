add_library(
  nexus_sim_memory
  STATIC
    src/system.cpp)

add_library(Nexus::sim_memory ALIAS nexus_sim_memory)

target_include_directories(nexus_sim_memory PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nexus_sim_memory PUBLIC nexus_sim_io)
nexus_apply_project_options(nexus_sim_memory)
nexus_set_warnings(nexus_sim_memory)
nexus_enable_sanitizers(nexus_sim_memory)
