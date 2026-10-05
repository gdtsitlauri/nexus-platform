add_library(
  nexus_analysis
  STATIC
    src/cfg.cpp
    src/dominators.cpp
    src/data_flow.cpp
    src/liveness.cpp
    src/region_flow.cpp
    src/symbolic.cpp
    src/affine_analysis.cpp
    src/alias_analysis.cpp
    src/interprocedural.cpp
    src/ssa.cpp)

add_library(Nexus::analysis ALIAS nexus_analysis)

target_include_directories(nexus_analysis PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nexus_analysis PUBLIC nexus_ir)
nexus_apply_project_options(nexus_analysis)
nexus_set_warnings(nexus_analysis)
nexus_enable_sanitizers(nexus_analysis)
