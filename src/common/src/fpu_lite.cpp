#include "nexus/common/fpu_lite.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace nexus::common {

namespace {

FloatClass classify(std::uint32_t exponent_bits, std::uint32_t fraction_bits) {
  if (exponent_bits == 0U) {
    return fraction_bits == 0U ? FloatClass::Zero : FloatClass::Subnormal;
  }
  if (exponent_bits == 0xffU) {
    return fraction_bits == 0U ? FloatClass::Infinity : FloatClass::NaN;
  }
  return FloatClass::Normal;
}

std::string format_hex_bits(std::uint32_t bits) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(8) << std::setfill('0') << bits;
  return output.str();
}

std::string format_q8_8(std::int32_t raw) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(8) << std::setfill('0')
         << static_cast<std::uint32_t>(raw);
  return output.str();
}

}  // namespace

std::string_view float_class_name(FloatClass classification) {
  switch (classification) {
    case FloatClass::Zero:
      return "zero";
    case FloatClass::Subnormal:
      return "subnormal";
    case FloatClass::Normal:
      return "normal";
    case FloatClass::Infinity:
      return "infinity";
    case FloatClass::NaN:
      return "nan";
  }
  return "unknown";
}

Float32Decoded decode_float32(float value) {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  const auto exponent_bits = (bits >> 23U) & 0xffU;
  const auto fraction_bits = bits & 0x7fffffU;
  const auto classification = classify(exponent_bits, fraction_bits);
  return Float32Decoded{
      .value = value,
      .bits = bits,
      .sign = (bits & 0x80000000U) != 0U,
      .exponent_bits = exponent_bits,
      .fraction_bits = fraction_bits,
      .unbiased_exponent = exponent_bits == 0U ? -126 : static_cast<int>(exponent_bits) - 127,
      .classification = classification,
  };
}

std::string_view float_relation_name(FloatRelation relation) {
  switch (relation) {
    case FloatRelation::Less:
      return "less";
    case FloatRelation::Equal:
      return "equal";
    case FloatRelation::Greater:
      return "greater";
    case FloatRelation::Unordered:
      return "unordered";
  }
  return "unknown";
}

FixedQ8_8 to_fixed_q8_8(float value) {
  const double scaled = std::round(static_cast<double>(value) * 256.0);
  const double clamped = std::clamp(
      scaled,
      static_cast<double>(std::numeric_limits<std::int32_t>::min()),
      static_cast<double>(std::numeric_limits<std::int32_t>::max()));
  return FixedQ8_8{
      .raw = static_cast<std::int32_t>(clamped),
      .value = clamped / 256.0,
  };
}

FloatDemoResult run_float32_demo(float lhs, float rhs) {
  FloatRelation relation = FloatRelation::Equal;
  if (std::isnan(lhs) || std::isnan(rhs)) {
    relation = FloatRelation::Unordered;
  } else if (lhs < rhs) {
    relation = FloatRelation::Less;
  } else if (lhs > rhs) {
    relation = FloatRelation::Greater;
  }

  const float sum = lhs + rhs;
  return FloatDemoResult{
      .lhs = decode_float32(lhs),
      .rhs = decode_float32(rhs),
      .sum = decode_float32(sum),
      .product = decode_float32(lhs * rhs),
      .relation = relation,
      .lhs_fixed = to_fixed_q8_8(lhs),
      .rhs_fixed = to_fixed_q8_8(rhs),
      .sum_fixed = to_fixed_q8_8(sum),
  };
}

std::string format_float32_demo(const FloatDemoResult& result) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(6);
  output << "fp-demo f32:\n";

  const auto append_value = [&output](std::string_view label, const Float32Decoded& decoded) {
    output << "  " << label << ": value=" << decoded.value
           << " bits=" << format_hex_bits(decoded.bits)
           << " class=" << float_class_name(decoded.classification)
           << " sign=" << (decoded.sign ? 1 : 0)
           << " exponent=0x" << std::hex << std::setw(2) << std::setfill('0')
           << decoded.exponent_bits
           << " fraction=0x" << std::setw(6) << decoded.fraction_bits
           << std::dec << std::setfill(' ')
           << " unbiased=" << decoded.unbiased_exponent << '\n';
  };

  append_value("lhs", result.lhs);
  append_value("rhs", result.rhs);
  append_value("add", result.sum);
  append_value("mul", result.product);
  output << "  compare: " << float_relation_name(result.relation) << '\n';
  output << "  fixed-q8.8:\n";
  output << "    lhs=" << format_q8_8(result.lhs_fixed.raw) << " (" << result.lhs_fixed.value << ")\n";
  output << "    rhs=" << format_q8_8(result.rhs_fixed.raw) << " (" << result.rhs_fixed.value << ")\n";
  output << "    add=" << format_q8_8(result.sum_fixed.raw) << " (" << result.sum_fixed.value << ")\n";
  return output.str();
}

}  // namespace nexus::common
