add_library(
  nexus_os
  STATIC
    src/scheduling.cpp
    src/concurrency.cpp
    src/resources.cpp
    src/storage.cpp)

target_include_directories(nexus_os PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
find_package(Threads REQUIRED)
target_link_libraries(nexus_os PUBLIC Threads::Threads)
nexus_apply_project_options(nexus_os)
nexus_set_warnings(nexus_os)
nexus_enable_sanitizers(nexus_os)
