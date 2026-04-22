#include <cstdlib>
#include <immintrin.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
  std::string kernel = "all";
  std::size_t size = 32;
};

Options parse_options(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--kernel" && index + 1 < argc) {
      options.kernel = argv[++index];
    } else if (arg == "--size" && index + 1 < argc) {
      options.size = static_cast<std::size_t>(std::strtoull(argv[++index], nullptr, 10));
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  return options;
}

float scalar_vector_add(std::size_t size) {
  std::vector<float> a(size);
  std::vector<float> b(size);
  std::vector<float> c(size, 0.0f);
  for (std::size_t index = 0; index < size; ++index) {
    a[index] = static_cast<float>(index);
    b[index] = static_cast<float>(index * 2U);
  }
  for (std::size_t index = 0; index < size; ++index) {
    c[index] = a[index] + b[index];
  }
  float checksum = 0.0f;
  for (const float value : c) {
    checksum += value;
  }
  return checksum;
}

float simd_vector_add(std::size_t size) {
  std::vector<float> a(size);
  std::vector<float> b(size);
  std::vector<float> c(size, 0.0f);
  for (std::size_t index = 0; index < size; ++index) {
    a[index] = static_cast<float>(index);
    b[index] = static_cast<float>(index * 2U);
  }

  const std::size_t step = 4;
  std::size_t index = 0;
  for (; index + step <= size; index += step) {
    const __m128 va = _mm_loadu_ps(&a[index]);
    const __m128 vb = _mm_loadu_ps(&b[index]);
    _mm_storeu_ps(&c[index], _mm_add_ps(va, vb));
  }
  for (; index < size; ++index) {
    c[index] = a[index] + b[index];
  }

  float checksum = 0.0f;
  for (const float value : c) {
    checksum += value;
  }
  return checksum;
}

float scalar_dot(std::size_t size) {
  std::vector<float> a(size);
  std::vector<float> b(size);
  for (std::size_t index = 0; index < size; ++index) {
    a[index] = static_cast<float>((index % 5U) + 1U);
    b[index] = static_cast<float>((index % 7U) + 2U);
  }
  float total = 0.0f;
  for (std::size_t index = 0; index < size; ++index) {
    total += a[index] * b[index];
  }
  return total;
}

float simd_dot(std::size_t size) {
  std::vector<float> a(size);
  std::vector<float> b(size);
  for (std::size_t index = 0; index < size; ++index) {
    a[index] = static_cast<float>((index % 5U) + 1U);
    b[index] = static_cast<float>((index % 7U) + 2U);
  }

  const std::size_t step = 4;
  std::size_t index = 0;
  __m128 accum = _mm_setzero_ps();
  for (; index + step <= size; index += step) {
    const __m128 va = _mm_loadu_ps(&a[index]);
    const __m128 vb = _mm_loadu_ps(&b[index]);
    accum = _mm_add_ps(accum, _mm_mul_ps(va, vb));
  }

  alignas(16) float lanes[4];
  _mm_store_ps(lanes, accum);
  float total = lanes[0] + lanes[1] + lanes[2] + lanes[3];
  for (; index < size; ++index) {
    total += a[index] * b[index];
  }
  return total;
}

void emit_result(const std::string& kernel, std::size_t size, float scalar_value, float simd_value) {
  std::cout << "benchmark=simd"
            << " kernel=" << kernel
            << " size=" << size
            << " checksum=" << scalar_value
            << " scalar=" << scalar_value
            << " simd=" << simd_value
            << " lanes=4"
            << " status=" << ((scalar_value == simd_value) ? "ok" : "mismatch") << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = parse_options(argc, argv);
    if (options.kernel == "vector-add" || options.kernel == "all") {
      emit_result("vector-add", options.size, scalar_vector_add(options.size), simd_vector_add(options.size));
    }
    if (options.kernel == "dot" || options.kernel == "all") {
      emit_result("dot", options.size, scalar_dot(options.size), simd_dot(options.size));
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
