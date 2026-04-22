add_library(
  nexus_passes
  STATIC
    src/loop_unroll.cpp
    src/affine_stripmine.cpp
    src/interprocedural_pass.cpp)

add_library(Nexus::passes ALIAS nexus_passes)

target_include_directories(nexus_passes PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  nexus_passes
  PUBLIC
    nexus_ir
    nexus_analysis)
nexus_apply_project_options(nexus_passes)
nexus_set_warnings(nexus_passes)
nexus_enable_sanitizers(nexus_passes)
