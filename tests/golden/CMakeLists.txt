add_test(
  NAME nexus_phase6_golden
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/golden/mips_golden_test.py"
    "$<TARGET_FILE:nexusc>"
    "$<TARGET_FILE:mips_sim>"
    "${PROJECT_SOURCE_DIR}")

add_test(
  NAME nexus_phase8_golden
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/golden/advanced_golden_test.py"
    "$<TARGET_FILE:mips_sim>"
    "${PROJECT_SOURCE_DIR}")

add_test(
  NAME nexus_phase10_golden
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/golden/parallel_golden_test.py"
    "$<TARGET_FILE:mips_sim>"
    "${PROJECT_SOURCE_DIR}")
