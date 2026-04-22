add_library(
  nexus_semantics
  STATIC
    src/semantic_analyzer.cpp)

add_library(Nexus::semantics ALIAS nexus_semantics)

target_include_directories(nexus_semantics PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nexus_semantics PUBLIC nexus_frontend)
nexus_apply_project_options(nexus_semantics)
nexus_set_warnings(nexus_semantics)
nexus_enable_sanitizers(nexus_semantics)
