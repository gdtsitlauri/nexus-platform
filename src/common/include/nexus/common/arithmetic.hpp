#pragma once

#include <cstdint>

namespace nexus::common {

enum class AdderStyle {
  RippleCarry,
  Native,
};

struct AdderResult {
  std::int32_t sum = 0;
  bool carry_out = false;
  bool overflow = false;
};

AdderResult add_signed(std::int32_t lhs, std::int32_t rhs, AdderStyle style = AdderStyle::Native);
AdderResult subtract_signed(
    std::int32_t lhs,
    std::int32_t rhs,
    AdderStyle style = AdderStyle::Native);

enum class ALUOperation {
  Add,
  Subtract,
  And,
  Or,
  Xor,
  SetLessThanSigned,
  SetLessThanUnsigned,
  ShiftLeftLogical,
  LoadUpperImmediate,
  PassThrough,
};

struct ALUResult {
  std::int32_t value = 0;
  bool zero = false;
  bool negative = false;
  bool carry_out = false;
  bool overflow = false;
};

ALUResult alu_compute(
    ALUOperation operation,
    std::int32_t lhs,
    std::int32_t rhs,
    std::uint32_t shift_amount = 0,
    AdderStyle adder_style = AdderStyle::Native);

enum class MultiplierStyle {
  ShiftAdd,
  Native,
};

struct MultiplyResult {
  std::int32_t hi = 0;
  std::int32_t lo = 0;
};

MultiplyResult multiply_signed(
    std::int32_t lhs,
    std::int32_t rhs,
    MultiplierStyle style = MultiplierStyle::Native);

enum class DividerStyle {
  Restoring,
  Native,
};

struct DivideResult {
  std::int32_t quotient = 0;
  std::int32_t remainder = 0;
  bool divide_by_zero = false;
};

DivideResult divide_signed(
    std::int32_t dividend,
    std::int32_t divisor,
    DividerStyle style = DividerStyle::Native);

}  // namespace nexus::common
