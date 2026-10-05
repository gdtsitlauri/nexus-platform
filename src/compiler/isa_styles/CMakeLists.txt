add_library(
  nexus_isa_styles
  STATIC
    src/isa_styles.cpp)

add_library(Nexus::isa_styles ALIAS nexus_isa_styles)

target_include_directories(nexus_isa_styles PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nexus_isa_styles PUBLIC nexus_ir)
nexus_apply_project_options(nexus_isa_styles)
nexus_set_warnings(nexus_isa_styles)
nexus_enable_sanitizers(nexus_isa_styles)
