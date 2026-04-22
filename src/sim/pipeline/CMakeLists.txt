add_library(
  nexus_sim_pipeline
  STATIC
    src/model.cpp)

add_library(Nexus::sim_pipeline ALIAS nexus_sim_pipeline)

target_include_directories(nexus_sim_pipeline PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  nexus_sim_pipeline
  PUBLIC
    nexus_mips_loader
    nexus_mips_isa
    nexus_common
    nexus_sim_memory
    nexus_sim_io)
nexus_apply_project_options(nexus_sim_pipeline)
nexus_set_warnings(nexus_sim_pipeline)
nexus_enable_sanitizers(nexus_sim_pipeline)
