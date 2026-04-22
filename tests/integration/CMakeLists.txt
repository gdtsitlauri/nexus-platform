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

add_test(NAME nexus_hdl_adder COMMAND "${PROJECT_SOURCE_DIR}/scripts/test_hdl.sh" adder)
add_test(NAME nexus_hdl_alu COMMAND "${PROJECT_SOURCE_DIR}/scripts/test_hdl.sh" alu)
add_test(NAME nexus_hdl_multiplier COMMAND "${PROJECT_SOURCE_DIR}/scripts/test_hdl.sh" multiplier)
add_test(NAME nexus_hdl_divider COMMAND "${PROJECT_SOURCE_DIR}/scripts/test_hdl.sh" divider)
add_test(NAME nexus_hdl_register_file COMMAND "${PROJECT_SOURCE_DIR}/scripts/test_hdl.sh" register-file)
add_test(NAME nexus_hdl_control COMMAND "${PROJECT_SOURCE_DIR}/scripts/test_hdl.sh" control)
add_test(NAME nexus_hdl_pipeline_reg COMMAND "${PROJECT_SOURCE_DIR}/scripts/test_hdl.sh" pipeline-reg)
add_test(NAME nexus_hdl_cpu_slice COMMAND "${PROJECT_SOURCE_DIR}/scripts/test_hdl.sh" cpu-slice)
add_test(NAME nexus_hdl_all COMMAND "${PROJECT_SOURCE_DIR}/scripts/test_hdl.sh" all)

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

add_test(
  NAME nexus_openmp_benchmark
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/parallel_benchmark_modes_test.py"
    openmp
    "$<TARGET_FILE_DIR:mips_sim>"
    "${PROJECT_SOURCE_DIR}")

add_test(
  NAME nexus_mpi_benchmark
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/parallel_benchmark_modes_test.py"
    mpi
    "$<TARGET_FILE_DIR:mips_sim>"
    "${PROJECT_SOURCE_DIR}")

add_test(
  NAME nexus_simd_benchmark
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/parallel_benchmark_modes_test.py"
    simd
    "$<TARGET_FILE_DIR:mips_sim>"
    "${PROJECT_SOURCE_DIR}")

add_test(
  NAME nexus_bench_wrapper_phase10
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/integration/parallel_bench_test.py"
    "$<TARGET_FILE_DIR:mips_sim>"
    "${PROJECT_SOURCE_DIR}")

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
