add_library(
  nexus_ir
  STATIC
    src/ir.cpp
    src/lowering.cpp
    src/printer.cpp)

add_library(Nexus::ir ALIAS nexus_ir)

target_include_directories(nexus_ir PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nexus_ir PUBLIC nexus_frontend nexus_semantics)
nexus_apply_project_options(nexus_ir)
nexus_set_warnings(nexus_ir)
nexus_enable_sanitizers(nexus_ir)
