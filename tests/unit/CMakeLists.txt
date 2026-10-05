add_executable(nexus_common_smoke common_smoke_test.cpp)
target_link_libraries(nexus_common_smoke PRIVATE nexus_common)
nexus_apply_project_options(nexus_common_smoke)
nexus_set_warnings(nexus_common_smoke)
nexus_enable_sanitizers(nexus_common_smoke)

add_test(NAME nexus_common_smoke COMMAND $<TARGET_FILE:nexus_common_smoke>)

add_executable(nexus_frontend_lexer_test frontend_lexer_test.cpp)
target_link_libraries(nexus_frontend_lexer_test PRIVATE nexus_frontend)
nexus_apply_project_options(nexus_frontend_lexer_test)
nexus_set_warnings(nexus_frontend_lexer_test)
nexus_enable_sanitizers(nexus_frontend_lexer_test)
add_test(NAME nexus_frontend_lexer_test COMMAND $<TARGET_FILE:nexus_frontend_lexer_test>)

add_executable(nexus_frontend_parser_test frontend_parser_test.cpp)
target_link_libraries(nexus_frontend_parser_test PRIVATE nexus_frontend)
nexus_apply_project_options(nexus_frontend_parser_test)
nexus_set_warnings(nexus_frontend_parser_test)
nexus_enable_sanitizers(nexus_frontend_parser_test)
add_test(NAME nexus_frontend_parser_test COMMAND $<TARGET_FILE:nexus_frontend_parser_test>)

add_executable(nexus_semantics_test frontend_semantics_test.cpp)
target_link_libraries(nexus_semantics_test PRIVATE nexus_frontend nexus_semantics)
nexus_apply_project_options(nexus_semantics_test)
nexus_set_warnings(nexus_semantics_test)
nexus_enable_sanitizers(nexus_semantics_test)
add_test(NAME nexus_semantics_test COMMAND $<TARGET_FILE:nexus_semantics_test>)

add_executable(nexus_ir_lowering_test ir_lowering_test.cpp)
target_link_libraries(
  nexus_ir_lowering_test PRIVATE nexus_frontend nexus_semantics nexus_ir)
nexus_apply_project_options(nexus_ir_lowering_test)
nexus_set_warnings(nexus_ir_lowering_test)
nexus_enable_sanitizers(nexus_ir_lowering_test)
add_test(NAME nexus_ir_lowering_test COMMAND $<TARGET_FILE:nexus_ir_lowering_test>)

add_executable(nexus_cfg_analysis_test cfg_analysis_test.cpp)
target_link_libraries(
  nexus_cfg_analysis_test PRIVATE nexus_frontend nexus_semantics nexus_ir nexus_analysis)
nexus_apply_project_options(nexus_cfg_analysis_test)
nexus_set_warnings(nexus_cfg_analysis_test)
nexus_enable_sanitizers(nexus_cfg_analysis_test)
add_test(NAME nexus_cfg_analysis_test COMMAND $<TARGET_FILE:nexus_cfg_analysis_test>)

add_executable(nexus_backend_mips_test backend_mips_test.cpp)
target_link_libraries(
  nexus_backend_mips_test
  PRIVATE
    nexus_frontend
    nexus_semantics
    nexus_ir
    nexus_backend_mips
    nexus_mips_assembler)
nexus_apply_project_options(nexus_backend_mips_test)
nexus_set_warnings(nexus_backend_mips_test)
nexus_enable_sanitizers(nexus_backend_mips_test)
add_test(NAME nexus_backend_mips_test COMMAND $<TARGET_FILE:nexus_backend_mips_test>)

add_executable(nexus_mips_loader_test mips_loader_test.cpp)
target_link_libraries(nexus_mips_loader_test PRIVATE nexus_mips_loader)
nexus_apply_project_options(nexus_mips_loader_test)
nexus_set_warnings(nexus_mips_loader_test)
nexus_enable_sanitizers(nexus_mips_loader_test)
add_test(NAME nexus_mips_loader_test COMMAND $<TARGET_FILE:nexus_mips_loader_test>)

add_executable(nexus_mips_functional_test mips_functional_test.cpp)
target_link_libraries(
  nexus_mips_functional_test PRIVATE nexus_mips_loader nexus_sim_functional)
nexus_apply_project_options(nexus_mips_functional_test)
nexus_set_warnings(nexus_mips_functional_test)
nexus_enable_sanitizers(nexus_mips_functional_test)
add_test(NAME nexus_mips_functional_test COMMAND $<TARGET_FILE:nexus_mips_functional_test>)

add_executable(nexus_arithmetic_units_test arithmetic_units_test.cpp)
target_link_libraries(nexus_arithmetic_units_test PRIVATE nexus_common)
nexus_apply_project_options(nexus_arithmetic_units_test)
nexus_set_warnings(nexus_arithmetic_units_test)
nexus_enable_sanitizers(nexus_arithmetic_units_test)
add_test(NAME nexus_arithmetic_units_test COMMAND $<TARGET_FILE:nexus_arithmetic_units_test>)

add_executable(nexus_fpu_lite_test fpu_lite_test.cpp)
target_link_libraries(nexus_fpu_lite_test PRIVATE nexus_common)
nexus_apply_project_options(nexus_fpu_lite_test)
nexus_set_warnings(nexus_fpu_lite_test)
nexus_enable_sanitizers(nexus_fpu_lite_test)
add_test(NAME nexus_fpu_lite_test COMMAND $<TARGET_FILE:nexus_fpu_lite_test>)

add_executable(nexus_single_cycle_model_test single_cycle_model_test.cpp)
target_link_libraries(
  nexus_single_cycle_model_test
  PRIVATE
    nexus_mips_loader
    nexus_sim_functional
    nexus_sim_single_cycle)
nexus_apply_project_options(nexus_single_cycle_model_test)
nexus_set_warnings(nexus_single_cycle_model_test)
nexus_enable_sanitizers(nexus_single_cycle_model_test)
add_test(NAME nexus_single_cycle_model_test COMMAND $<TARGET_FILE:nexus_single_cycle_model_test>)

add_executable(nexus_multi_cycle_model_test multi_cycle_model_test.cpp)
target_link_libraries(
  nexus_multi_cycle_model_test
  PRIVATE
    nexus_mips_loader
    nexus_sim_functional
    nexus_sim_multi_cycle)
nexus_apply_project_options(nexus_multi_cycle_model_test)
nexus_set_warnings(nexus_multi_cycle_model_test)
nexus_enable_sanitizers(nexus_multi_cycle_model_test)
add_test(NAME nexus_multi_cycle_model_test COMMAND $<TARGET_FILE:nexus_multi_cycle_model_test>)

add_executable(nexus_pipeline_model_test pipeline_model_test.cpp)
target_link_libraries(
  nexus_pipeline_model_test
  PRIVATE
    nexus_mips_loader
    nexus_sim_functional
    nexus_sim_pipeline)
nexus_apply_project_options(nexus_pipeline_model_test)
nexus_set_warnings(nexus_pipeline_model_test)
nexus_enable_sanitizers(nexus_pipeline_model_test)
add_test(NAME nexus_pipeline_model_test COMMAND $<TARGET_FILE:nexus_pipeline_model_test>)

add_executable(nexus_memory_cache_test memory_cache_test.cpp)
target_link_libraries(nexus_memory_cache_test PRIVATE nexus_sim_memory)
nexus_apply_project_options(nexus_memory_cache_test)
nexus_set_warnings(nexus_memory_cache_test)
nexus_enable_sanitizers(nexus_memory_cache_test)
add_test(NAME nexus_memory_cache_test COMMAND $<TARGET_FILE:nexus_memory_cache_test>)

add_executable(nexus_io_system_test io_system_test.cpp)
target_link_libraries(nexus_io_system_test PRIVATE nexus_sim_io)
nexus_apply_project_options(nexus_io_system_test)
nexus_set_warnings(nexus_io_system_test)
nexus_enable_sanitizers(nexus_io_system_test)
add_test(NAME nexus_io_system_test COMMAND $<TARGET_FILE:nexus_io_system_test>)

add_executable(nexus_pipeline_memory_system_test pipeline_memory_system_test.cpp)
target_link_libraries(
  nexus_pipeline_memory_system_test
  PRIVATE
    nexus_mips_loader
    nexus_sim_functional
    nexus_sim_pipeline)
nexus_apply_project_options(nexus_pipeline_memory_system_test)
nexus_set_warnings(nexus_pipeline_memory_system_test)
nexus_enable_sanitizers(nexus_pipeline_memory_system_test)
add_test(NAME nexus_pipeline_memory_system_test COMMAND $<TARGET_FILE:nexus_pipeline_memory_system_test>)

add_executable(nexus_advanced_predictor_test advanced_predictor_test.cpp)
target_link_libraries(
  nexus_advanced_predictor_test
  PRIVATE
    nexus_mips_loader
    nexus_sim_advanced)
nexus_apply_project_options(nexus_advanced_predictor_test)
nexus_set_warnings(nexus_advanced_predictor_test)
nexus_enable_sanitizers(nexus_advanced_predictor_test)
add_test(NAME nexus_advanced_predictor_test COMMAND $<TARGET_FILE:nexus_advanced_predictor_test>)

add_executable(nexus_advanced_model_test advanced_model_test.cpp)
target_link_libraries(
  nexus_advanced_model_test
  PRIVATE
    nexus_mips_loader
    nexus_sim_functional
    nexus_sim_advanced)
nexus_apply_project_options(nexus_advanced_model_test)
nexus_set_warnings(nexus_advanced_model_test)
nexus_enable_sanitizers(nexus_advanced_model_test)
add_test(NAME nexus_advanced_model_test COMMAND $<TARGET_FILE:nexus_advanced_model_test>)

add_executable(nexus_experimental_parse_test experimental_parse_test.cpp)
target_link_libraries(
  nexus_experimental_parse_test PRIVATE nexus_frontend nexus_experimental_parallel_parsing)
nexus_apply_project_options(nexus_experimental_parse_test)
nexus_set_warnings(nexus_experimental_parse_test)
nexus_enable_sanitizers(nexus_experimental_parse_test)
add_test(NAME nexus_experimental_parse_test COMMAND $<TARGET_FILE:nexus_experimental_parse_test>)

add_executable(nexus_symbolic_analysis_test symbolic_analysis_test.cpp)
target_link_libraries(
  nexus_symbolic_analysis_test
  PRIVATE
    nexus_frontend
    nexus_semantics
    nexus_ir
    nexus_analysis)
nexus_apply_project_options(nexus_symbolic_analysis_test)
nexus_set_warnings(nexus_symbolic_analysis_test)
nexus_enable_sanitizers(nexus_symbolic_analysis_test)
add_test(NAME nexus_symbolic_analysis_test COMMAND $<TARGET_FILE:nexus_symbolic_analysis_test>)

add_executable(nexus_affine_analysis_test affine_analysis_test.cpp)
target_link_libraries(
  nexus_affine_analysis_test
  PRIVATE
    nexus_frontend
    nexus_semantics
    nexus_ir
    nexus_analysis
    nexus_passes
    nexus_backend_mips
    nexus_mips_assembler
    nexus_mips_loader
    nexus_sim_functional)
nexus_apply_project_options(nexus_affine_analysis_test)
nexus_set_warnings(nexus_affine_analysis_test)
nexus_enable_sanitizers(nexus_affine_analysis_test)
add_test(NAME nexus_affine_analysis_test COMMAND $<TARGET_FILE:nexus_affine_analysis_test>)

add_executable(nexus_unroll_pass_test unroll_pass_test.cpp)
target_link_libraries(
  nexus_unroll_pass_test
  PRIVATE
    nexus_frontend
    nexus_semantics
    nexus_ir
    nexus_passes
    nexus_backend_mips
    nexus_mips_assembler
    nexus_mips_loader
    nexus_sim_functional)
nexus_apply_project_options(nexus_unroll_pass_test)
nexus_set_warnings(nexus_unroll_pass_test)
nexus_enable_sanitizers(nexus_unroll_pass_test)
add_test(NAME nexus_unroll_pass_test COMMAND $<TARGET_FILE:nexus_unroll_pass_test>)

add_executable(nexus_alias_analysis_test alias_analysis_test.cpp)
target_link_libraries(
  nexus_alias_analysis_test
  PRIVATE
    nexus_frontend
    nexus_semantics
    nexus_ir
    nexus_analysis)
nexus_apply_project_options(nexus_alias_analysis_test)
nexus_set_warnings(nexus_alias_analysis_test)
nexus_enable_sanitizers(nexus_alias_analysis_test)
add_test(NAME nexus_alias_analysis_test COMMAND $<TARGET_FILE:nexus_alias_analysis_test>)

add_executable(nexus_interproc_test interproc_test.cpp)
target_link_libraries(
  nexus_interproc_test
  PRIVATE
    nexus_frontend
    nexus_semantics
    nexus_ir
    nexus_analysis
    nexus_passes
    nexus_backend_mips
    nexus_mips_assembler
    nexus_mips_loader
    nexus_sim_functional)
nexus_apply_project_options(nexus_interproc_test)
nexus_set_warnings(nexus_interproc_test)
nexus_enable_sanitizers(nexus_interproc_test)
add_test(NAME nexus_interproc_test COMMAND $<TARGET_FILE:nexus_interproc_test>)

add_executable(nexus_parallel_model_test parallel_model_test.cpp)
target_link_libraries(
  nexus_parallel_model_test
  PRIVATE
    nexus_mips_loader
    nexus_sim_functional
    nexus_sim_parallel)
nexus_apply_project_options(nexus_parallel_model_test)
nexus_set_warnings(nexus_parallel_model_test)
nexus_enable_sanitizers(nexus_parallel_model_test)
add_test(NAME nexus_parallel_model_test COMMAND $<TARGET_FILE:nexus_parallel_model_test>)

add_executable(nexus_coherence_test coherence_model_test.cpp)
target_link_libraries(
  nexus_coherence_test
  PRIVATE
    nexus_mips_loader
    nexus_sim_parallel)
nexus_apply_project_options(nexus_coherence_test)
nexus_set_warnings(nexus_coherence_test)
nexus_enable_sanitizers(nexus_coherence_test)
add_test(NAME nexus_coherence_test COMMAND $<TARGET_FILE:nexus_coherence_test>)

add_executable(nexus_consistency_test consistency_model_test.cpp)
target_link_libraries(
  nexus_consistency_test
  PRIVATE
    nexus_mips_loader
    nexus_sim_parallel)
nexus_apply_project_options(nexus_consistency_test)
nexus_set_warnings(nexus_consistency_test)
nexus_enable_sanitizers(nexus_consistency_test)
add_test(NAME nexus_consistency_test COMMAND $<TARGET_FILE:nexus_consistency_test>)

add_executable(nexus_interconnect_test interconnect_demo_test.cpp)
target_link_libraries(
  nexus_interconnect_test
  PRIVATE
    nexus_mips_loader
    nexus_sim_parallel)
nexus_apply_project_options(nexus_interconnect_test)
nexus_set_warnings(nexus_interconnect_test)
nexus_enable_sanitizers(nexus_interconnect_test)
add_test(NAME nexus_interconnect_test COMMAND $<TARGET_FILE:nexus_interconnect_test>)

add_executable(nexus_tomasulo_test tomasulo_test.cpp)
target_link_libraries(nexus_tomasulo_test PRIVATE nexus_mips_loader nexus_sim_functional nexus_sim_advanced)
nexus_apply_project_options(nexus_tomasulo_test)
nexus_set_warnings(nexus_tomasulo_test)
nexus_enable_sanitizers(nexus_tomasulo_test)
add_test(NAME nexus_tomasulo_test COMMAND $<TARGET_FILE:nexus_tomasulo_test>)

add_executable(nexus_formal_test formal_test.cpp)
target_link_libraries(nexus_formal_test PRIVATE nexus_formal)
nexus_apply_project_options(nexus_formal_test)
nexus_set_warnings(nexus_formal_test)
nexus_enable_sanitizers(nexus_formal_test)
add_test(NAME nexus_formal_test COMMAND $<TARGET_FILE:nexus_formal_test> "${PROJECT_SOURCE_DIR}")

add_executable(nexus_type_inference_test type_inference_test.cpp)
target_link_libraries(nexus_type_inference_test PRIVATE nexus_types)
nexus_apply_project_options(nexus_type_inference_test)
nexus_set_warnings(nexus_type_inference_test)
nexus_enable_sanitizers(nexus_type_inference_test)
add_test(NAME nexus_type_inference_test COMMAND $<TARGET_FILE:nexus_type_inference_test>)

add_executable(nexus_ssa_test ssa_test.cpp)
target_link_libraries(nexus_ssa_test PRIVATE nexus_frontend nexus_semantics nexus_ir nexus_analysis)
nexus_apply_project_options(nexus_ssa_test)
nexus_set_warnings(nexus_ssa_test)
nexus_enable_sanitizers(nexus_ssa_test)
add_test(NAME nexus_ssa_test COMMAND $<TARGET_FILE:nexus_ssa_test> "${PROJECT_SOURCE_DIR}")
