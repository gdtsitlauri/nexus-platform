add_subdirectory(common)
add_subdirectory(compiler)
add_subdirectory(mips)
add_subdirectory(sim)
add_subdirectory(hdl)
add_subdirectory(benchmarks)

add_executable(nexusc nexusc_main.cpp)
target_link_libraries(
  nexusc
  PRIVATE
    nexus_common
    nexus_frontend
    nexus_experimental_parallel_parsing
    nexus_semantics
    nexus_ir
    nexus_analysis
    nexus_passes
    nexus_backend_mips
    nexus_mips_assembler)
nexus_apply_project_options(nexusc)
nexus_set_warnings(nexusc)
nexus_enable_sanitizers(nexusc)

add_executable(mips_sim mips_sim_main.cpp)
set_target_properties(mips_sim PROPERTIES OUTPUT_NAME "mips-sim")
target_link_libraries(
  mips_sim
  PRIVATE
    nexus_common
    nexus_mips_loader
    nexus_sim_functional
    nexus_sim_single_cycle
    nexus_sim_multi_cycle
    nexus_sim_pipeline
    nexus_sim_advanced
    nexus_sim_parallel
    nexus_sim_metrics)
nexus_apply_project_options(mips_sim)
nexus_set_warnings(mips_sim)
nexus_enable_sanitizers(mips_sim)
