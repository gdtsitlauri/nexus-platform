#include <cmath>
#include <iostream>
#include <limits>
#include <string>

#include "nexus/common/fpu_lite.hpp"

int main() {
  using nexus::common::FloatClass;
  using nexus::common::FloatRelation;

  const auto decoded = nexus::common::decode_float32(1.5F);
  if (decoded.bits != 0x3fc00000U || decoded.classification != FloatClass::Normal || decoded.sign ||
      decoded.exponent_bits != 0x7fU || decoded.fraction_bits != 0x400000U ||
      decoded.unbiased_exponent != 0) {
    std::cerr << "Unexpected float32 decode for 1.5.\n";
    return 1;
  }

  const auto demo = nexus::common::run_float32_demo(1.5F, 0.25F);
  if (demo.relation != FloatRelation::Greater || demo.sum.bits != 0x3fe00000U ||
      demo.product.bits != 0x3ec00000U || demo.lhs_fixed.raw != 384 || demo.rhs_fixed.raw != 64 ||
      demo.sum_fixed.raw != 448) {
    std::cerr << "Unexpected floating-point arithmetic demo result.\n";
    return 1;
  }

  const auto nan_demo =
      nexus::common::run_float32_demo(std::numeric_limits<float>::quiet_NaN(), 2.0F);
  if (nan_demo.lhs.classification != FloatClass::NaN || nan_demo.relation != FloatRelation::Unordered ||
      nan_demo.sum.classification != FloatClass::NaN) {
    std::cerr << "NaN classification or comparison was incorrect.\n";
    return 1;
  }

  const std::string text = nexus::common::format_float32_demo(demo);
  if (text.find("fp-demo f32:") == std::string::npos || text.find("bits=0x3fc00000") == std::string::npos ||
      text.find("compare: greater") == std::string::npos || text.find("fixed-q8.8:") == std::string::npos) {
    std::cerr << "Formatted floating-point demo output was missing expected sections.\n";
    return 1;
  }

  return 0;
}
