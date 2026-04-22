#include <cuda_runtime.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace {

__global__ void vector_add_kernel(const int* a, const int* b, int* out, int count) {
  const int index = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
  if (index < count) {
    out[index] = a[index] + b[index];
  }
}

__global__ void reduction_kernel(const int* input, int* output, int count) {
  __shared__ int partial[128];
  const int tid = static_cast<int>(threadIdx.x);
  const int index = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
  partial[tid] = index < count ? input[index] : 0;
  __syncthreads();

  for (int stride = blockDim.x / 2; stride > 0; stride >>= 1) {
    if (tid < stride) {
      partial[tid] += partial[tid + stride];
    }
    __syncthreads();
  }

  if (tid == 0) {
    output[blockIdx.x] = partial[0];
  }
}

int parse_int(const char* value, const char* label) {
  char* end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  if (end == nullptr || *end != '\0' || parsed <= 0) {
    std::cerr << "invalid " << label << ": " << value << '\n';
    std::exit(1);
  }
  return static_cast<int>(parsed);
}

bool check_cuda(cudaError_t status, const char* message) {
  if (status == cudaSuccess) {
    return true;
  }
  std::cerr << message << ": " << cudaGetErrorString(status) << '\n';
  return false;
}

int emit_skip_lines(std::string_view kernel, int size) {
  auto print_skip = [size](std::string_view name) {
    std::cout << "benchmark=gpu kernel=" << name << " size=" << size
              << " status=skipped reason=no-device\n";
  };
  if (kernel == "all") {
    print_skip("vector-add");
    print_skip("reduction");
  } else {
    print_skip(kernel);
  }
  return 0;
}

int run_vector_add(int size) {
  std::vector<int> a(static_cast<std::size_t>(size));
  std::vector<int> b(static_cast<std::size_t>(size));
  std::vector<int> cpu_out(static_cast<std::size_t>(size));
  std::vector<int> gpu_out(static_cast<std::size_t>(size));

  for (int index = 0; index < size; ++index) {
    a[static_cast<std::size_t>(index)] = index;
    b[static_cast<std::size_t>(index)] = size - index;
    cpu_out[static_cast<std::size_t>(index)] = a[static_cast<std::size_t>(index)] + b[static_cast<std::size_t>(index)];
  }

  int* device_a = nullptr;
  int* device_b = nullptr;
  int* device_out = nullptr;
  if (!check_cuda(cudaMalloc(&device_a, sizeof(int) * static_cast<std::size_t>(size)), "cudaMalloc(device_a)") ||
      !check_cuda(cudaMalloc(&device_b, sizeof(int) * static_cast<std::size_t>(size)), "cudaMalloc(device_b)") ||
      !check_cuda(cudaMalloc(&device_out, sizeof(int) * static_cast<std::size_t>(size)), "cudaMalloc(device_out)")) {
    return 1;
  }

  check_cuda(cudaMemcpy(device_a, a.data(), sizeof(int) * static_cast<std::size_t>(size), cudaMemcpyHostToDevice),
             "cudaMemcpy(device_a)");
  check_cuda(cudaMemcpy(device_b, b.data(), sizeof(int) * static_cast<std::size_t>(size), cudaMemcpyHostToDevice),
             "cudaMemcpy(device_b)");

  const int threads_per_block = 64;
  const int blocks = (size + threads_per_block - 1) / threads_per_block;
  vector_add_kernel<<<blocks, threads_per_block>>>(device_a, device_b, device_out, size);
  if (!check_cuda(cudaDeviceSynchronize(), "cudaDeviceSynchronize(vector-add)")) {
    cudaFree(device_a);
    cudaFree(device_b);
    cudaFree(device_out);
    return 1;
  }

  check_cuda(cudaMemcpy(
                 gpu_out.data(),
                 device_out,
                 sizeof(int) * static_cast<std::size_t>(size),
                 cudaMemcpyDeviceToHost),
             "cudaMemcpy(vector-add out)");

  cudaFree(device_a);
  cudaFree(device_b);
  cudaFree(device_out);

  const std::int64_t cpu_checksum = std::accumulate(cpu_out.begin(), cpu_out.end(), std::int64_t{0});
  const std::int64_t gpu_checksum = std::accumulate(gpu_out.begin(), gpu_out.end(), std::int64_t{0});
  const bool matches = (cpu_out == gpu_out);
  std::cout << "benchmark=gpu kernel=vector-add size=" << size
            << " checksum=" << gpu_checksum
            << " cpu_checksum=" << cpu_checksum
            << " gpu_checksum=" << gpu_checksum
            << " status=" << (matches ? "ok" : "mismatch") << '\n';
  return matches ? 0 : 1;
}

int run_reduction(int size) {
  std::vector<int> input(static_cast<std::size_t>(size));
  for (int index = 0; index < size; ++index) {
    input[static_cast<std::size_t>(index)] = (index % 7) + 1;
  }
  const std::int64_t cpu_sum = std::accumulate(input.begin(), input.end(), std::int64_t{0});

  int* device_input = nullptr;
  int* device_output = nullptr;
  if (!check_cuda(
          cudaMalloc(&device_input, sizeof(int) * static_cast<std::size_t>(size)),
          "cudaMalloc(device_input)") ||
      !check_cuda(cudaMalloc(&device_output, sizeof(int)), "cudaMalloc(device_output)")) {
    return 1;
  }

  check_cuda(
      cudaMemcpy(device_input, input.data(), sizeof(int) * static_cast<std::size_t>(size), cudaMemcpyHostToDevice),
      "cudaMemcpy(device_input)");

  const int threads = 128;
  reduction_kernel<<<1, threads>>>(device_input, device_output, size);
  if (!check_cuda(cudaDeviceSynchronize(), "cudaDeviceSynchronize(reduction)")) {
    cudaFree(device_input);
    cudaFree(device_output);
    return 1;
  }

  int gpu_sum = 0;
  check_cuda(cudaMemcpy(&gpu_sum, device_output, sizeof(int), cudaMemcpyDeviceToHost), "cudaMemcpy(reduction out)");

  cudaFree(device_input);
  cudaFree(device_output);

  const bool matches = static_cast<std::int64_t>(gpu_sum) == cpu_sum;
  std::cout << "benchmark=gpu kernel=reduction size=" << size
            << " checksum=" << gpu_sum
            << " cpu_checksum=" << cpu_sum
            << " gpu_checksum=" << gpu_sum
            << " status=" << (matches ? "ok" : "mismatch") << '\n';
  return matches ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  std::string kernel = "vector-add";
  int size = 64;

  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--kernel" && index + 1 < argc) {
      kernel = argv[++index];
    } else if (argument == "--size" && index + 1 < argc) {
      size = parse_int(argv[++index], "size");
    } else if (argument == "--help") {
      std::cout << "usage: gpu-bench [--kernel vector-add|reduction|all] [--size N]\n";
      return 0;
    } else {
      std::cerr << "unknown argument: " << argument << '\n';
      return 1;
    }
  }

  if (kernel != "vector-add" && kernel != "reduction" && kernel != "all") {
    std::cerr << "unsupported kernel: " << kernel << '\n';
    return 1;
  }

  int device_count = 0;
  if (cudaGetDeviceCount(&device_count) != cudaSuccess || device_count == 0) {
    return emit_skip_lines(kernel, size);
  }

  if (kernel == "vector-add") {
    return run_vector_add(size);
  }
  if (kernel == "reduction") {
    return run_reduction(size);
  }

  const int add_status = run_vector_add(size);
  const int reduction_status = run_reduction(size);
  return (add_status == 0 && reduction_status == 0) ? 0 : 1;
}
