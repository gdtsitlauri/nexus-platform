add_subdirectory(common)
add_subdirectory(logic)
add_subdirectory(vlsi)
add_subdirectory(os)
add_subdirectory(embedded)
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
    nexus_isa_styles
    nexus_formal
    nexus_types
    nexus_polyhedral
    nexus_mips_assembler
    nexus_mips_loader
    nexus_sim_functional)
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
    nexus_sim_simt
    nexus_sim_metrics)
nexus_apply_project_options(mips_sim)
nexus_set_warnings(mips_sim)
nexus_enable_sanitizers(mips_sim)

add_executable(nexus_eda eda_main.cpp)
set_target_properties(nexus_eda PROPERTIES OUTPUT_NAME "nexus-eda")
target_link_libraries(nexus_eda PRIVATE nexus_logic nexus_vlsi)
nexus_apply_project_options(nexus_eda)
nexus_set_warnings(nexus_eda)
nexus_enable_sanitizers(nexus_eda)

add_executable(nexus_os_cli os_main.cpp)
set_target_properties(nexus_os_cli PROPERTIES OUTPUT_NAME "nexus-os")
target_link_libraries(nexus_os_cli PRIVATE nexus_os)
nexus_apply_project_options(nexus_os_cli)
nexus_set_warnings(nexus_os_cli)
nexus_enable_sanitizers(nexus_os_cli)

add_executable(nexus_embedded_cli embedded_main.cpp)
set_target_properties(nexus_embedded_cli PROPERTIES OUTPUT_NAME "nexus-embedded")
target_link_libraries(nexus_embedded_cli PRIVATE nexus_embedded)
nexus_apply_project_options(nexus_embedded_cli)
nexus_set_warnings(nexus_embedded_cli)
nexus_enable_sanitizers(nexus_embedded_cli)

# EY517 Linux lab programs (POSIX only)
if(UNIX)
  enable_language(C)
  find_package(Threads REQUIRED)
  foreach(prog fork_pipe exec_signal threads)
    add_executable(nexus_posix_${prog} "${PROJECT_SOURCE_DIR}/examples/os/posix/${prog}.c")
    target_link_libraries(nexus_posix_${prog} PRIVATE Threads::Threads)
  endforeach()
endif()
