add_library(
  nexus_sim_single_cycle
  STATIC
    src/control.cpp
    src/model.cpp)

add_library(Nexus::sim_single_cycle ALIAS nexus_sim_single_cycle)

target_include_directories(nexus_sim_single_cycle PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nexus_sim_single_cycle PUBLIC nexus_mips_loader nexus_mips_isa nexus_common)
nexus_apply_project_options(nexus_sim_single_cycle)
nexus_set_warnings(nexus_sim_single_cycle)
nexus_enable_sanitizers(nexus_sim_single_cycle)
