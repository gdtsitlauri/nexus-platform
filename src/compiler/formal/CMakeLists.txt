add_library(
  nexus_formal
  STATIC
    src/automata.cpp
    src/grammar.cpp)

add_library(Nexus::formal ALIAS nexus_formal)

target_include_directories(nexus_formal PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
nexus_apply_project_options(nexus_formal)
nexus_set_warnings(nexus_formal)
nexus_enable_sanitizers(nexus_formal)
