#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace nexus::common {

enum class FloatClass {
  Zero,
  Subnormal,
  Normal,
  Infinity,
  NaN,
};

std::string_view float_class_name(FloatClass classification);

struct Float32Decoded {
  float value = 0.0F;
  std::uint32_t bits = 0;
  bool sign = false;
  std::uint32_t exponent_bits = 0;
  std::uint32_t fraction_bits = 0;
  int unbiased_exponent = 0;
  FloatClass classification = FloatClass::Zero;
};

Float32Decoded decode_float32(float value);

enum class FloatRelation {
  Less,
  Equal,
  Greater,
  Unordered,
};

std::string_view float_relation_name(FloatRelation relation);

struct FixedQ8_8 {
  std::int32_t raw = 0;
  double value = 0.0;
};

FixedQ8_8 to_fixed_q8_8(float value);

struct FloatDemoResult {
  Float32Decoded lhs{};
  Float32Decoded rhs{};
  Float32Decoded sum{};
  Float32Decoded product{};
  FloatRelation relation = FloatRelation::Equal;
  FixedQ8_8 lhs_fixed{};
  FixedQ8_8 rhs_fixed{};
  FixedQ8_8 sum_fixed{};
};

FloatDemoResult run_float32_demo(float lhs, float rhs);
std::string format_float32_demo(const FloatDemoResult& result);

}  // namespace nexus::common
