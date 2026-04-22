#include <climits>
#include <iostream>

#include "nexus/common/arithmetic.hpp"

int main() {
  using namespace nexus::common;

  const auto ripple_add = add_signed(10, 20, AdderStyle::RippleCarry);
  if (ripple_add.sum != 30 || ripple_add.carry_out || ripple_add.overflow) {
    std::cerr << "Ripple-carry add failed.\n";
    return 1;
  }

  const auto native_add = add_signed(INT_MAX, 1, AdderStyle::Native);
  if (!native_add.overflow || native_add.sum != INT_MIN) {
    std::cerr << "Native add overflow detection failed.\n";
    return 1;
  }

  const auto ripple_sub = subtract_signed(7, 10, AdderStyle::RippleCarry);
  if (ripple_sub.sum != -3 || ripple_sub.overflow) {
    std::cerr << "Ripple-carry subtract failed.\n";
    return 1;
  }

  const auto alu = alu_compute(ALUOperation::SetLessThanUnsigned, -1, 7);
  if (alu.value != 0 || !alu.zero) {
    std::cerr << "ALU unsigned compare failed.\n";
    return 1;
  }

  const auto shift_add_product = multiply_signed(7, -6, MultiplierStyle::ShiftAdd);
  if (shift_add_product.lo != -42 || shift_add_product.hi != -1) {
    std::cerr << "Shift-add multiplier failed.\n";
    return 1;
  }

  const auto native_product = multiply_signed(-8, -9, MultiplierStyle::Native);
  if (native_product.lo != 72 || native_product.hi != 0) {
    std::cerr << "Native multiplier failed.\n";
    return 1;
  }

  const auto restoring_div = divide_signed(43, 5, DividerStyle::Restoring);
  if (restoring_div.divide_by_zero || restoring_div.quotient != 8 || restoring_div.remainder != 3) {
    std::cerr << "Restoring divider failed.\n";
    return 1;
  }

  const auto signed_div = divide_signed(-43, 5, DividerStyle::Restoring);
  if (signed_div.quotient != -8 || signed_div.remainder != -3) {
    std::cerr << "Signed divide remainder convention failed.\n";
    return 1;
  }

  const auto div_zero = divide_signed(7, 0, DividerStyle::Native);
  if (!div_zero.divide_by_zero) {
    std::cerr << "Divide-by-zero flag not set.\n";
    return 1;
  }

  return 0;
}
