#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nexus::common {

// ---- IEEE-754 binary32 implemented in integer arithmetic ---------------------------------------
// Round-to-nearest-even, gradual underflow (subnormals), signed zeros, infinities and quiet NaNs.
// Results are bit-identical to hardware binary32 arithmetic (NaN payloads aside).
std::uint32_t soft_add(std::uint32_t a, std::uint32_t b);
std::uint32_t soft_sub(std::uint32_t a, std::uint32_t b);
std::uint32_t soft_mul(std::uint32_t a, std::uint32_t b);
std::uint32_t soft_div(std::uint32_t a, std::uint32_t b);
bool is_nan_bits(std::uint32_t bits);
std::string describe_float_bits(std::uint32_t bits);  // "s eeeeeeee fff... (class, value)"

// ---- integer representations -------------------------------------------------------------------
struct IntegerEncodings {
  unsigned bits = 8;
  std::optional<std::string> sign_magnitude;
  std::optional<std::string> ones_complement;
  std::optional<std::string> twos_complement;
  std::optional<std::string> excess;  // bias 2^(bits-1) - 1, as in IEEE exponents
  std::optional<std::string> bcd;     // packed BCD of |value| (non-negative values only)
};

IntegerEncodings encode_integer(std::int64_t value, unsigned bits);
std::string to_binary(std::uint64_t value, unsigned bits);

// ---- arithmetic algorithms with step traces -----------------------------------------------------
struct AlgorithmTrace {
  std::int64_t result = 0;
  std::int64_t remainder = 0;
  std::vector<std::string> steps;
};

// Booth radix-2 multiplication of two `bits`-wide two's-complement operands (registers A, Q, Q-1).
AlgorithmTrace booth_multiply(std::int64_t multiplicand, std::int64_t multiplier, unsigned bits);
// Non-restoring division of two `bits`-wide unsigned operands.
std::optional<AlgorithmTrace> nonrestoring_divide(std::uint64_t dividend, std::uint64_t divisor, unsigned bits);

// 32-bit carry-lookahead adder: 4-bit blocks with group propagate/generate and a second
// lookahead level.  Reports the sum, the carry out and the gate-delay depth versus ripple carry.
struct CarryLookaheadResult {
  std::uint32_t sum = 0;
  bool carry_out = false;
  std::uint32_t carries = 0;     // carry into each bit
  unsigned lookahead_delay = 0;  // gate levels to the slowest sum bit
  unsigned ripple_delay = 0;
};

CarryLookaheadResult carry_lookahead_add(std::uint32_t a, std::uint32_t b, bool carry_in = false);

// ---- character data ------------------------------------------------------------------------------
std::optional<std::vector<std::uint8_t>> utf8_encode(std::uint32_t code_point);
// Strict decoder: rejects overlong forms, surrogates, code points above U+10FFFF and truncation.
std::optional<std::vector<std::uint32_t>> utf8_decode(const std::string& bytes, std::string& error);

}  // namespace nexus::common
