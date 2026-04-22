#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include <omp.h>

namespace {

struct Options {
  std::string kernel = "all";
  std::size_t size = 32;
  int threads = 2;
};

Options parse_options(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--kernel" && index + 1 < argc) {
      options.kernel = argv[++index];
    } else if (arg == "--size" && index + 1 < argc) {
      options.size = static_cast<std::size_t>(std::strtoull(argv[++index], nullptr, 10));
    } else if (arg == "--threads" && index + 1 < argc) {
      options.threads = std::max(1, std::atoi(argv[++index]));
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  return options;
}

void emit_result(const std::string& kernel, int threads, std::size_t size, long long checksum) {
  std::cout << "benchmark=openmp"
            << " kernel=" << kernel
            << " threads=" << threads
            << " size=" << size
            << " checksum=" << checksum
            << " status=ok\n";
}

long long vector_add_kernel(std::size_t size) {
  std::vector<long long> a(size);
  std::vector<long long> b(size);
  std::vector<long long> c(size, 0);
  for (std::size_t index = 0; index < size; ++index) {
    a[index] = static_cast<long long>(index);
    b[index] = static_cast<long long>(index * 2U);
  }

#pragma omp parallel for schedule(static)
  for (std::ptrdiff_t index = 0; index < static_cast<std::ptrdiff_t>(size); ++index) {
    c[static_cast<std::size_t>(index)] = a[static_cast<std::size_t>(index)] + b[static_cast<std::size_t>(index)];
  }

  return std::accumulate(c.begin(), c.end(), 0LL);
}

long long reduction_kernel(std::size_t size) {
  long long total = 0;
#pragma omp parallel for reduction(+ : total) schedule(static)
  for (std::ptrdiff_t index = 0; index < static_cast<std::ptrdiff_t>(size); ++index) {
    total += static_cast<long long>((index % 7) + 1);
  }
  return total;
}

long long matmul_kernel(std::size_t size) {
  const std::size_t dim = std::max<std::size_t>(2, std::min<std::size_t>(size, 12));
  std::vector<long long> a(dim * dim);
  std::vector<long long> b(dim * dim);
  std::vector<long long> c(dim * dim, 0);
  for (std::size_t row = 0; row < dim; ++row) {
    for (std::size_t col = 0; col < dim; ++col) {
      a[row * dim + col] = static_cast<long long>(row + col + 1);
      b[row * dim + col] = static_cast<long long>((row == col) ? 2 : 1);
    }
  }

#pragma omp parallel for schedule(static)
  for (std::ptrdiff_t row = 0; row < static_cast<std::ptrdiff_t>(dim); ++row) {
    for (std::size_t col = 0; col < dim; ++col) {
      long long cell = 0;
      for (std::size_t k = 0; k < dim; ++k) {
        cell += a[static_cast<std::size_t>(row) * dim + k] * b[k * dim + col];
      }
      c[static_cast<std::size_t>(row) * dim + col] = cell;
    }
  }

  return std::accumulate(c.begin(), c.end(), 0LL);
}

long long branch_mix_kernel(std::size_t size) {
  long long total = 0;
#pragma omp parallel for reduction(+ : total) schedule(static)
  for (std::ptrdiff_t index = 0; index < static_cast<std::ptrdiff_t>(size); ++index) {
    const long long value = static_cast<long long>((index * 17) % 29);
    if ((value % 3) == 0) {
      total += value;
    } else {
      total -= value / 2;
    }
  }
  return total;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = parse_options(argc, argv);
    omp_set_dynamic(0);
    omp_set_num_threads(options.threads);

    if (options.kernel == "vector-add" || options.kernel == "all") {
      emit_result("vector-add", options.threads, options.size, vector_add_kernel(options.size));
    }
    if (options.kernel == "reduction" || options.kernel == "all") {
      emit_result("reduction", options.threads, options.size, reduction_kernel(options.size));
    }
    if (options.kernel == "matmul" || options.kernel == "all") {
      emit_result("matmul", options.threads, options.size, matmul_kernel(options.size));
    }
    if (options.kernel == "branch-mix" || options.kernel == "all") {
      emit_result("branch-mix", options.threads, options.size, branch_mix_kernel(options.size));
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
