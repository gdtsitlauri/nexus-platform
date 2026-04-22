add_library(
  nexus_sim_advanced
  src/model.cpp
  src/predictor.cpp)

target_include_directories(nexus_sim_advanced PUBLIC include)
target_link_libraries(
  nexus_sim_advanced
  PUBLIC
    nexus_mips_loader)
nexus_apply_project_options(nexus_sim_advanced)
nexus_set_warnings(nexus_sim_advanced)
nexus_enable_sanitizers(nexus_sim_advanced)
