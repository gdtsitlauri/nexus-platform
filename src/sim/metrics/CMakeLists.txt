add_library(
  nexus_sim_metrics
  STATIC
    src/report.cpp)

add_library(Nexus::sim_metrics ALIAS nexus_sim_metrics)

target_include_directories(nexus_sim_metrics PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
nexus_apply_project_options(nexus_sim_metrics)
nexus_set_warnings(nexus_sim_metrics)
nexus_enable_sanitizers(nexus_sim_metrics)
