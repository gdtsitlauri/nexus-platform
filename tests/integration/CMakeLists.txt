add_test(NAME nexusc_cli_smoke COMMAND $<TARGET_FILE:nexusc>)
set_tests_properties(
  nexusc_cli_smoke
  PROPERTIES
    PASS_REGULAR_EXPRESSION "Nexus / NexusLang Platform")

add_test(NAME mips_sim_cli_smoke COMMAND $<TARGET_FILE:mips_sim>)
set_tests_properties(
  mips_sim_cli_smoke
  PROPERTIES
    PASS_REGULAR_EXPRESSION "Nexus / NexusLang Platform")

add_test(NAME nexus_hdl_adder COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/test_hdl.py" adder)
add_test(NAME nexus_hdl_alu COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/test_hdl.py" alu)
add_test(NAME nexus_hdl_multiplier COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/test_hdl.py" multiplier)
add_test(NAME nexus_hdl_divider COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/test_hdl.py" divider)
add_test(NAME nexus_hdl_register_file COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/test_hdl.py" register-file)
add_test(NAME nexus_hdl_control COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/test_hdl.py" control)
add_test(NAME nexus_hdl_pipeline_reg COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/test_hdl.py" pipeline-reg)
add_test(NAME nexus_hdl_cpu_slice COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/test_hdl.py" cpu-slice)
add_test(NAME nexus_hdl_all COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/test_hdl.py" all)
set_tests_properties(
  nexus_hdl_adder nexus_hdl_alu nexus_hdl_multiplier nexus_hdl_divider nexus_hdl_register_file
  nexus_hdl_control nexus_hdl_pipeline_reg nexus_hdl_cpu_slice nexus_hdl_all
  PROPERTIES SKIP_RETURN_CODE 77)

add_test(
  NAME nexus_cli
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/nexusc_cli_test.py"
    "$<TARGET_FILE:nexusc>"
    "$<TARGET_FILE:mips_sim>"
    "${PROJECT_SOURCE_DIR}")

if(NEXUS_HAVE_OPENMP)
add_test(
  NAME nexus_openmp_benchmark
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/parallel_benchmark_modes_test.py"
    openmp
    "$<TARGET_FILE_DIR:mips_sim>"
    "${PROJECT_SOURCE_DIR}")
endif()

if(NEXUS_HAVE_MPI)
add_test(
  NAME nexus_mpi_benchmark
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/parallel_benchmark_modes_test.py"
    mpi
    "$<TARGET_FILE_DIR:mips_sim>"
    "${PROJECT_SOURCE_DIR}")
endif()

add_test(
  NAME nexus_simd_benchmark
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/parallel_benchmark_modes_test.py"
    simd
    "$<TARGET_FILE_DIR:mips_sim>"
    "${PROJECT_SOURCE_DIR}")

add_test(
  NAME nexus_gpu_optional
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/gpu_optional_test.py"
    "$<TARGET_FILE_DIR:mips_sim>"
    "${PROJECT_SOURCE_DIR}")
set_tests_properties(nexus_gpu_optional PROPERTIES SKIP_RETURN_CODE 77)

add_test(
  NAME nexus_cross_model_differential
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/cross_model_test.py"
    "$<TARGET_FILE:nexusc>"
    "$<TARGET_FILE:mips_sim>"
    "${PROJECT_SOURCE_DIR}")

add_test(
  NAME nexus_isa_styles
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/isa_styles_test.py"
    "$<TARGET_FILE:nexusc>"
    "${PROJECT_SOURCE_DIR}")

add_test(
  NAME nexus_hdl_pipeline_cosim
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/hdl_cosim_test.py"
    "$<TARGET_FILE:nexusc>"
    "$<TARGET_FILE:mips_sim>"
    "${PROJECT_SOURCE_DIR}")
set_tests_properties(nexus_hdl_pipeline_cosim PROPERTIES SKIP_RETURN_CODE 77)

add_test(
  NAME nexus_hdl_multicycle_cosim
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/hdl_multicycle_cosim_test.py"
    "$<TARGET_FILE:nexusc>"
    "$<TARGET_FILE:mips_sim>"
    "${PROJECT_SOURCE_DIR}")
set_tests_properties(nexus_hdl_multicycle_cosim PROPERTIES SKIP_RETURN_CODE 77)

add_test(
  NAME nexus_hdl_branch_prediction
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/hdl_predictor_test.py"
    "$<TARGET_FILE:nexusc>"
    "$<TARGET_FILE:mips_sim>"
    "${PROJECT_SOURCE_DIR}")
set_tests_properties(nexus_hdl_branch_prediction PROPERTIES SKIP_RETURN_CODE 77)

add_test(
  NAME nexus_manycore
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/manycore_test.py"
    "$<TARGET_FILE:mips_sim>"
    "${PROJECT_SOURCE_DIR}")

add_test(
  NAME nexus_coverage_cli
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/coverage_cli_test.py"
    "$<TARGET_FILE:nexusc>"
    "$<TARGET_FILE:mips_sim>"
    "${PROJECT_SOURCE_DIR}")

add_test(
  NAME nexus_full_flow
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/nexus_flow.py" "$<TARGET_FILE_DIR:mips_sim>")
set_tests_properties(nexus_full_flow PROPERTIES SKIP_RETURN_CODE 77)

add_test(
  NAME nexus_docs_links
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/integration/docs_links_test.py" "${PROJECT_SOURCE_DIR}")
