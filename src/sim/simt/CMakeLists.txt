add_library(
  nexus_sim_simt
  STATIC
    src/model.cpp)

add_library(Nexus::sim_simt ALIAS nexus_sim_simt)

target_include_directories(nexus_sim_simt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nexus_sim_simt PUBLIC nexus_mips_loader nexus_mips_isa)
nexus_apply_project_options(nexus_sim_simt)
nexus_set_warnings(nexus_sim_simt)
nexus_enable_sanitizers(nexus_sim_simt)
