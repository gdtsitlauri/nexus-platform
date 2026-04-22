find_package(OpenMP REQUIRED)

add_executable(openmp_bench "${PROJECT_SOURCE_DIR}/benchmarks/openmp/openmp_bench.cpp")
set_target_properties(openmp_bench PROPERTIES OUTPUT_NAME "openmp-bench")
target_link_libraries(openmp_bench PRIVATE OpenMP::OpenMP_CXX nexus_common)
nexus_apply_project_options(openmp_bench)
nexus_set_warnings(openmp_bench)
nexus_enable_sanitizers(openmp_bench)
