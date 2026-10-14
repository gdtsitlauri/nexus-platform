add_library(
  nexus_vlsi
  STATIC
    src/cmos.cpp
    src/logical_effort.cpp
    src/aig.cpp
    src/netlist.cpp
    src/analysis.cpp
    src/partition.cpp
    src/floorplan.cpp
    src/placement.cpp
    src/routing.cpp
    src/cli.cpp)

target_include_directories(nexus_vlsi PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(nexus_vlsi PUBLIC nexus_logic)
nexus_apply_project_options(nexus_vlsi)
nexus_set_warnings(nexus_vlsi)
nexus_enable_sanitizers(nexus_vlsi)
