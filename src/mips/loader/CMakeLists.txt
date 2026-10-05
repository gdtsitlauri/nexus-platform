add_library(
  nexus_mips_loader
  STATIC
    src/parser.cpp
    src/encoder.cpp)

add_library(Nexus::mips_loader ALIAS nexus_mips_loader)

target_include_directories(nexus_mips_loader PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nexus_mips_loader PUBLIC nexus_mips_isa)
nexus_apply_project_options(nexus_mips_loader)
nexus_set_warnings(nexus_mips_loader)
nexus_enable_sanitizers(nexus_mips_loader)
