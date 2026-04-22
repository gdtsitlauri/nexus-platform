function(nexus_apply_project_options target_name)
  target_compile_features("${target_name}" PUBLIC cxx_std_20)
  target_compile_definitions(
    "${target_name}"
    PUBLIC
      NEXUS_VERSION_LABEL="${NEXUS_VERSION_LABEL}"
      NEXUS_PHASE_LABEL="${NEXUS_PHASE_LABEL}")
endfunction()
