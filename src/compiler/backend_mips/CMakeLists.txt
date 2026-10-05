add_library(
  nexus_backend_mips
  STATIC
    src/codegen.cpp
    src/regalloc.cpp)

add_library(Nexus::backend_mips ALIAS nexus_backend_mips)

target_include_directories(nexus_backend_mips PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nexus_backend_mips PUBLIC nexus_ir nexus_mips_assembler nexus_mips_isa)
nexus_apply_project_options(nexus_backend_mips)
nexus_set_warnings(nexus_backend_mips)
nexus_enable_sanitizers(nexus_backend_mips)
