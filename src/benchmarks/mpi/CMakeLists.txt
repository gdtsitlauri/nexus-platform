find_program(NEXUS_MPICXX_EXECUTABLE NAMES mpicxx mpic++)
if(NOT NEXUS_MPICXX_EXECUTABLE)
  message(FATAL_ERROR "MPI C++ wrapper (mpicxx/mpic++) was not found.")
endif()

execute_process(
  COMMAND "${NEXUS_MPICXX_EXECUTABLE}" --showme:compile
  OUTPUT_VARIABLE NEXUS_MPI_COMPILE_FLAGS
  ERROR_VARIABLE NEXUS_MPI_COMPILE_DIAGNOSTICS
  OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
  COMMAND "${NEXUS_MPICXX_EXECUTABLE}" --showme:link
  OUTPUT_VARIABLE NEXUS_MPI_LINK_FLAGS
  ERROR_VARIABLE NEXUS_MPI_LINK_DIAGNOSTICS
  OUTPUT_STRIP_TRAILING_WHITESPACE)

separate_arguments(NEXUS_MPI_COMPILE_ARGS UNIX_COMMAND "${NEXUS_MPI_COMPILE_FLAGS}")
separate_arguments(NEXUS_MPI_LINK_ARGS UNIX_COMMAND "${NEXUS_MPI_LINK_FLAGS}")

add_executable(mpi_bench "${PROJECT_SOURCE_DIR}/benchmarks/mpi/mpi_bench.cpp")
set_target_properties(mpi_bench PROPERTIES OUTPUT_NAME "mpi-bench")
target_compile_options(mpi_bench PRIVATE ${NEXUS_MPI_COMPILE_ARGS})
target_link_libraries(mpi_bench PRIVATE nexus_common ${NEXUS_MPI_LINK_ARGS})
nexus_apply_project_options(mpi_bench)
nexus_set_warnings(mpi_bench)
nexus_enable_sanitizers(mpi_bench)
