find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

BISON_TARGET(
  NexusFlexBisonParser
  "${CMAKE_CURRENT_SOURCE_DIR}/src/flex_bison_parser.y"
  "${CMAKE_CURRENT_BINARY_DIR}/flex_bison_parser.cpp"
  DEFINES_FILE "${CMAKE_CURRENT_BINARY_DIR}/flex_bison_parser.hpp")

FLEX_TARGET(
  NexusFlexBisonLexer
  "${CMAKE_CURRENT_SOURCE_DIR}/src/flex_bison_lexer.l"
  "${CMAKE_CURRENT_BINARY_DIR}/flex_bison_lexer.cpp")

ADD_FLEX_BISON_DEPENDENCY(NexusFlexBisonLexer NexusFlexBisonParser)

add_library(
  nexus_experimental_parallel_parsing
  STATIC
    src/prototype.cpp
    src/bison_lr.cpp
    ${BISON_NexusFlexBisonParser_OUTPUTS}
    ${FLEX_NexusFlexBisonLexer_OUTPUTS})

add_library(Nexus::experimental_parallel_parsing ALIAS nexus_experimental_parallel_parsing)

target_include_directories(
  nexus_experimental_parallel_parsing
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(
  nexus_experimental_parallel_parsing
  PUBLIC
    nexus_frontend)
nexus_apply_project_options(nexus_experimental_parallel_parsing)
nexus_set_warnings(nexus_experimental_parallel_parsing)
nexus_enable_sanitizers(nexus_experimental_parallel_parsing)
