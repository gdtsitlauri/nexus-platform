#include "nexus/common/arithmetic.hpp"

#include <cstdint>
#include <limits>

namespace nexus::common {

namespace {

AdderResult add_unsigned_impl(std::uint32_t lhs, std::uint32_t rhs, bool carry_in) {
  std::uint32_t sum = 0;
  bool carry = carry_in;
  for (std::uint32_t bit = 0; bit < 32; ++bit) {
    const bool lhs_bit = ((lhs >> bit) & 1U) != 0U;
    const bool rhs_bit = ((rhs >> bit) & 1U) != 0U;
    const bool partial = lhs_bit ^ rhs_bit ^ carry;
    if (partial) {
      sum |= (1U << bit);
    }
    carry = (lhs_bit && rhs_bit) || (lhs_bit && carry) || (rhs_bit && carry);
  }

  AdderResult result;
  result.sum = static_cast<std::int32_t>(sum);
  result.carry_out = carry;
  const bool lhs_sign = (lhs & 0x80000000U) != 0U;
  const bool rhs_sign = (rhs & 0x80000000U) != 0U;
  const bool sum_sign = (sum & 0x80000000U) != 0U;
  result.overflow = (lhs_sign == rhs_sign) && (lhs_sign != sum_sign);
  return result;
}

MultiplyResult from_wide_product(std::int64_t wide) {
  MultiplyResult result;
  result.lo = static_cast<std::int32_t>(wide & 0xffffffffLL);
  result.hi = static_cast<std::int32_t>((wide >> 32) & 0xffffffffLL);
  return result;
}

}  // namespace

AdderResult add_signed(std::int32_t lhs, std::int32_t rhs, AdderStyle style) {
  if (style == AdderStyle::RippleCarry) {
    return add_unsigned_impl(
        static_cast<std::uint32_t>(lhs), static_cast<std::uint32_t>(rhs), false);
  }

  const std::int64_t wide = static_cast<std::int64_t>(lhs) + static_cast<std::int64_t>(rhs);
  AdderResult result;
  result.sum = static_cast<std::int32_t>(wide);
  result.carry_out =
      (static_cast<std::uint64_t>(static_cast<std::uint32_t>(lhs)) +
       static_cast<std::uint64_t>(static_cast<std::uint32_t>(rhs))) > 0xffffffffULL;
  result.overflow =
      wide < static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()) ||
      wide > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max());
  return result;
}

AdderResult subtract_signed(std::int32_t lhs, std::int32_t rhs, AdderStyle style) {
  if (style == AdderStyle::RippleCarry) {
    const auto result = add_unsigned_impl(
        static_cast<std::uint32_t>(lhs), ~static_cast<std::uint32_t>(rhs), true);
    AdderResult adjusted = result;
    const auto lhs_bits = static_cast<std::uint32_t>(lhs);
    const auto rhs_bits = static_cast<std::uint32_t>(rhs);
    const auto sum_bits = static_cast<std::uint32_t>(adjusted.sum);
    adjusted.overflow = (((lhs_bits ^ rhs_bits) & (lhs_bits ^ sum_bits)) & 0x80000000U) != 0U;
    return adjusted;
  }

  const std::int64_t wide = static_cast<std::int64_t>(lhs) - static_cast<std::int64_t>(rhs);
  AdderResult result;
  result.sum = static_cast<std::int32_t>(wide);
  result.carry_out = static_cast<std::uint32_t>(lhs) >= static_cast<std::uint32_t>(rhs);
  result.overflow =
      wide < static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()) ||
      wide > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max());
  return result;
}

ALUResult alu_compute(
    ALUOperation operation,
    std::int32_t lhs,
    std::int32_t rhs,
    std::uint32_t shift_amount,
    AdderStyle adder_style) {
  ALUResult result;
  switch (operation) {
    case ALUOperation::Add: {
      const auto add = add_signed(lhs, rhs, adder_style);
      result.value = add.sum;
      result.carry_out = add.carry_out;
      result.overflow = add.overflow;
      break;
    }
    case ALUOperation::Subtract: {
      const auto sub = subtract_signed(lhs, rhs, adder_style);
      result.value = sub.sum;
      result.carry_out = sub.carry_out;
      result.overflow = sub.overflow;
      break;
    }
    case ALUOperation::And:
      result.value = lhs & rhs;
      break;
    case ALUOperation::Or:
      result.value = lhs | rhs;
      break;
    case ALUOperation::Xor:
      result.value = lhs ^ rhs;
      break;
    case ALUOperation::SetLessThanSigned:
      result.value = lhs < rhs ? 1 : 0;
      break;
    case ALUOperation::SetLessThanUnsigned:
      result.value = static_cast<std::uint32_t>(lhs) < static_cast<std::uint32_t>(rhs) ? 1 : 0;
      break;
    case ALUOperation::ShiftLeftLogical:
      result.value = static_cast<std::int32_t>(static_cast<std::uint32_t>(rhs) << shift_amount);
      break;
    case ALUOperation::LoadUpperImmediate:
      result.value = static_cast<std::int32_t>((static_cast<std::uint32_t>(rhs) & 0xffffU) << 16U);
      break;
    case ALUOperation::PassThrough:
      result.value = rhs;
      break;
  }

  result.zero = result.value == 0;
  result.negative = result.value < 0;
  return result;
}

MultiplyResult multiply_signed(std::int32_t lhs, std::int32_t rhs, MultiplierStyle style) {
  if (style == MultiplierStyle::Native) {
    return from_wide_product(
        static_cast<std::int64_t>(lhs) * static_cast<std::int64_t>(rhs));
  }

  const bool negative = (lhs < 0) ^ (rhs < 0);
  std::uint64_t multiplicand =
      static_cast<std::uint64_t>(lhs < 0 ? -static_cast<std::int64_t>(lhs) : lhs);
  std::uint64_t multiplier =
      static_cast<std::uint64_t>(rhs < 0 ? -static_cast<std::int64_t>(rhs) : rhs);
  std::uint64_t product = 0;

  for (std::uint32_t bit = 0; bit < 32; ++bit) {
    if ((multiplier & (1ULL << bit)) != 0U) {
      product += (multiplicand << bit);
    }
  }

  std::int64_t signed_product = static_cast<std::int64_t>(product);
  if (negative) {
    signed_product = -signed_product;
  }
  return from_wide_product(signed_product);
}

DivideResult divide_signed(std::int32_t dividend, std::int32_t divisor, DividerStyle style) {
  DivideResult result;
  if (divisor == 0) {
    result.divide_by_zero = true;
    return result;
  }

  if (style == DividerStyle::Native) {
    result.quotient = dividend / divisor;
    result.remainder = dividend % divisor;
    return result;
  }

  const bool quotient_negative = (dividend < 0) ^ (divisor < 0);
  const bool remainder_negative = dividend < 0;
  std::uint64_t dividend_abs =
      static_cast<std::uint64_t>(dividend < 0 ? -static_cast<std::int64_t>(dividend) : dividend);
  const std::uint64_t divisor_abs =
      static_cast<std::uint64_t>(divisor < 0 ? -static_cast<std::int64_t>(divisor) : divisor);

  std::uint64_t quotient = 0;
  std::uint64_t remainder = 0;
  for (int bit = 31; bit >= 0; --bit) {
    remainder = (remainder << 1U) | ((dividend_abs >> bit) & 1U);
    if (remainder >= divisor_abs) {
      remainder -= divisor_abs;
      quotient |= (1ULL << bit);
    }
  }

  result.quotient = static_cast<std::int32_t>(quotient_negative ? -static_cast<std::int64_t>(quotient)
                                                                : static_cast<std::int64_t>(quotient));
  result.remainder = static_cast<std::int32_t>(
      remainder_negative ? -static_cast<std::int64_t>(remainder)
                         : static_cast<std::int64_t>(remainder));
  return result;
}

}  // namespace nexus::common
