if(NOT NEXUS_CUDA_ENABLED)
  message(STATUS "Optional CUDA demo disabled; configure with -DNEXUS_ENABLE_CUDA=ON to build gpu-bench.")
  return()
endif()

add_executable(gpu_optional_bench "${PROJECT_SOURCE_DIR}/benchmarks/gpu_optional/gpu_optional_bench.cu")
set_target_properties(
  gpu_optional_bench
  PROPERTIES
    OUTPUT_NAME "gpu-bench"
    CUDA_STANDARD 17
    CUDA_STANDARD_REQUIRED ON)
target_link_libraries(gpu_optional_bench PRIVATE nexus_common)
