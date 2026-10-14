add_library(
  nexus_logic
  STATIC
    src/boolean.cpp
    src/minimize.cpp
    src/pla.cpp
    src/bdd.cpp
    src/sat.cpp
    src/codes.cpp
    src/fsm.cpp
    src/gatesim.cpp)

target_include_directories(nexus_logic PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
nexus_apply_project_options(nexus_logic)
nexus_set_warnings(nexus_logic)
nexus_enable_sanitizers(nexus_logic)
