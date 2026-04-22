add_library(
  nexus_mips_assembler
  STATIC
    src/printer.cpp)

add_library(Nexus::mips_assembler ALIAS nexus_mips_assembler)

target_include_directories(nexus_mips_assembler PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nexus_mips_assembler PUBLIC nexus_mips_isa)
nexus_apply_project_options(nexus_mips_assembler)
nexus_set_warnings(nexus_mips_assembler)
nexus_enable_sanitizers(nexus_mips_assembler)
