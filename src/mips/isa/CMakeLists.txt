add_library(
  nexus_mips_isa
  STATIC
    src/instruction.cpp)

add_library(Nexus::mips_isa ALIAS nexus_mips_isa)

target_include_directories(nexus_mips_isa PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
nexus_apply_project_options(nexus_mips_isa)
nexus_set_warnings(nexus_mips_isa)
nexus_enable_sanitizers(nexus_mips_isa)
