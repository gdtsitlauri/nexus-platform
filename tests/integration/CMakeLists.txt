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

add_test(
  NAME phase1_scaffold_audit
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/audit_phase1.py"
          "${PROJECT_SOURCE_DIR}")

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
  NAME nexus_phase10_cli
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/nexusc_cli_test.py"
    "$<TARGET_FILE:nexusc>"
    "$<TARGET_FILE:mips_sim>"
    "${PROJECT_SOURCE_DIR}")

add_test(
  NAME nexus_phase7_benchmarks
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/benchmark_harness_test.py"
    "$<TARGET_FILE:nexusc>"
    "$<TARGET_FILE:mips_sim>"
    "${PROJECT_SOURCE_DIR}")

add_test(
  NAME nexus_phase8_amdahl
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/advanced_amdahl_test.py"
    "$<TARGET_FILE:nexusc>"
    "$<TARGET_FILE:mips_sim>"
    "${PROJECT_SOURCE_DIR}")

add_test(
  NAME nexus_toolchain_compare
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/toolchain_compare_test.py"
    "$<TARGET_FILE:nexusc>"
    "${PROJECT_SOURCE_DIR}")
set_tests_properties(nexus_toolchain_compare PROPERTIES SKIP_RETURN_CODE 77)

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

if(NEXUS_HAVE_OPENMP AND NEXUS_HAVE_MPI)
add_test(
  NAME nexus_bench_wrapper_phase10
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/parallel_bench_test.py"
    "$<TARGET_FILE_DIR:mips_sim>"
    "${PROJECT_SOURCE_DIR}")
endif()

add_test(
  NAME nexus_gpu_optional
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/gpu_optional_test.py"
    "$<TARGET_FILE_DIR:mips_sim>"
    "${PROJECT_SOURCE_DIR}")

add_test(
  NAME nexus_literature_docs
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/final_docs_test.py"
    literature
    "${PROJECT_SOURCE_DIR}")

add_test(
  NAME nexus_report_docs
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/final_docs_test.py"
    report
    "${PROJECT_SOURCE_DIR}")

add_test(
  NAME nexus_final_validation
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/final_docs_test.py"
    final
    "${PROJECT_SOURCE_DIR}")

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
