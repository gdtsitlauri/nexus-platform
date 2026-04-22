add_library(
  nexus_frontend
  STATIC
    src/diagnostic.cpp
    src/token.cpp
    src/lexer.cpp
    src/parser.cpp
    src/ast_printer.cpp)

add_library(Nexus::frontend ALIAS nexus_frontend)

target_include_directories(nexus_frontend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nexus_frontend PUBLIC nexus_common)
nexus_apply_project_options(nexus_frontend)
nexus_set_warnings(nexus_frontend)
nexus_enable_sanitizers(nexus_frontend)
