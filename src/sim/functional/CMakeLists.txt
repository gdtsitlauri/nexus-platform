add_library(
  nexus_sim_functional
  STATIC
    src/interpreter.cpp)

add_library(Nexus::sim_functional ALIAS nexus_sim_functional)

target_include_directories(nexus_sim_functional PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nexus_sim_functional PUBLIC nexus_mips_loader nexus_mips_isa)
nexus_apply_project_options(nexus_sim_functional)
nexus_set_warnings(nexus_sim_functional)
nexus_enable_sanitizers(nexus_sim_functional)
