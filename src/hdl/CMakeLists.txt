add_subdirectory(alu)
add_subdirectory(register_file)
add_subdirectory(control)
add_subdirectory(pipeline_regs)
add_subdirectory(cpu_slice)
add_subdirectory(optional_cache_ctrl)

add_custom_target(
  hdl-test
  COMMAND "${PROJECT_SOURCE_DIR}/scripts/test_hdl.sh" all
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  COMMENT "Running Nexus HDL testbenches"
  VERBATIM)
