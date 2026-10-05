#include "nexus/common/number_systems.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <sstream>

namespace nexus::common {

namespace {

constexpr std::uint32_t kSignBit = 0x80000000U;
constexpr std::uint32_t kExponentMask = 0x7f800000U;
constexpr std::uint32_t kFractionMask = 0x007fffffU;
constexpr std::uint32_t kQuietNaN = 0x7fc00000U;
constexpr std::uint32_t kInfinity = 0x7f800000U;

struct Unpacked {
  bool sign = false;
  int exponent = 0;          // exponent of the least significant bit of `significand`
  std::uint64_t significand = 0;
  bool zero = false;
  bool infinity = false;
  bool nan = false;
};

Unpacked unpack(std::uint32_t bits) {
  Unpacked value;
  value.sign = (bits & kSignBit) != 0;
  const std::uint32_t exponent = (bits & kExponentMask) >> 23U;
  const std::uint32_t fraction = bits & kFractionMask;
  if (exponent == 0xff) {
    value.nan = fraction != 0;
    value.infinity = fraction == 0;
    return value;
  }
  if (exponent == 0) {
    value.zero = fraction == 0;
    value.significand = fraction;
    value.exponent = -149;  // subnormal: fraction x 2^-149
    return value;
  }
  value.significand = fraction | 0x800000U;
  value.exponent = static_cast<int>(exponent) - 150;
  return value;
}

// Rounds sign x significand x 2^exponent to binary32 (round to nearest, ties to even).
std::uint32_t round_pack(bool sign, std::uint64_t significand, int exponent) {
  const std::uint32_t sign_bits = sign ? kSignBit : 0U;
  if (significand == 0) {
    return sign_bits;
  }
  const int msb = 63 - std::countl_zero(significand);
  const int unit = std::max(msb + exponent - 23, -149);  // exponent of the result's LSB
  const int shift = unit - exponent;
  std::uint64_t q = 0;
  if (shift <= 0) {
    q = significand << static_cast<unsigned>(-shift);
  } else if (shift >= 64) {
    q = (shift == 64 && significand > (1ULL << 63U)) ? 1U : 0U;
  } else {
    q = significand >> static_cast<unsigned>(shift);
    const std::uint64_t remainder = significand & ((1ULL << static_cast<unsigned>(shift)) - 1U);
    const std::uint64_t half = 1ULL << static_cast<unsigned>(shift - 1);
    if (remainder > half || (remainder == half && (q & 1U) != 0)) {
      ++q;
    }
  }
  int lsb_exponent = unit;
  if (q == (1ULL << 24U)) {
    q >>= 1U;
    ++lsb_exponent;
  }
  if (q >= (1ULL << 23U)) {
    const int biased = lsb_exponent + 150;
    if (biased >= 0xff) {
      return sign_bits | kInfinity;
    }
    return sign_bits | (static_cast<std::uint32_t>(biased) << 23U) | (static_cast<std::uint32_t>(q) & kFractionMask);
  }
  return sign_bits | static_cast<std::uint32_t>(q);  // subnormal (or zero after rounding)
}

// Shifts a subnormal significand up so its leading one sits at bit 23.
void normalize(Unpacked& value) {
  while (value.significand != 0 && value.significand < 0x800000U) {
    value.significand <<= 1U;
    --value.exponent;
  }
}

std::string bits_text(std::uint32_t bits) {
  std::string text;
  for (int bit = 31; bit >= 0; --bit) {
    text += ((bits >> static_cast<unsigned>(bit)) & 1U) != 0 ? '1' : '0';
    if (bit == 31 || bit == 23) {
      text += ' ';
    }
  }
  return text;
}

}  // namespace

bool is_nan_bits(std::uint32_t bits) {
  return (bits & kExponentMask) == kExponentMask && (bits & kFractionMask) != 0;
}

std::uint32_t soft_add(std::uint32_t a, std::uint32_t b) {
  const Unpacked x = unpack(a);
  const Unpacked y = unpack(b);
  if (x.nan || y.nan) {
    return kQuietNaN;
  }
  if (x.infinity || y.infinity) {
    if (x.infinity && y.infinity && x.sign != y.sign) {
      return kQuietNaN;
    }
    return x.infinity ? a : b;
  }
  if (x.zero && y.zero) {
    return (x.sign && y.sign) ? kSignBit : 0U;
  }
  if (x.zero) {
    return b;
  }
  if (y.zero) {
    return a;
  }
  // Align on the smaller exponent; keep at most 38 bits of shift and fold the rest into a sticky bit.
  const Unpacked& big = x.exponent >= y.exponent ? x : y;
  const Unpacked& small = x.exponent >= y.exponent ? y : x;
  const int difference = big.exponent - small.exponent;
  std::uint64_t big_sig = 0;
  std::uint64_t small_sig = small.significand;
  int exponent = small.exponent;
  if (difference <= 38) {
    big_sig = big.significand << static_cast<unsigned>(difference);
  } else {
    big_sig = big.significand << 38U;
    const int drop = difference - 38;
    const bool sticky = drop >= 64 ? small_sig != 0 : (small_sig & ((1ULL << static_cast<unsigned>(drop)) - 1U)) != 0;
    small_sig = drop >= 64 ? 0 : small_sig >> static_cast<unsigned>(drop);
    small_sig |= sticky ? 1U : 0U;
    exponent = small.exponent + drop;
  }
  if (big.sign == small.sign) {
    return round_pack(big.sign, big_sig + small_sig, exponent);
  }
  if (big_sig == small_sig) {
    return 0U;  // exact cancellation is +0 in round-to-nearest
  }
  const bool sign = big_sig > small_sig ? big.sign : small.sign;
  return round_pack(sign, big_sig > small_sig ? big_sig - small_sig : small_sig - big_sig, exponent);
}

std::uint32_t soft_sub(std::uint32_t a, std::uint32_t b) {
  return soft_add(a, is_nan_bits(b) ? b : b ^ kSignBit);
}

std::uint32_t soft_mul(std::uint32_t a, std::uint32_t b) {
  const Unpacked x = unpack(a);
  const Unpacked y = unpack(b);
  const bool sign = x.sign != y.sign;
  if (x.nan || y.nan) {
    return kQuietNaN;
  }
  if (x.infinity || y.infinity) {
    if (x.zero || y.zero) {
      return kQuietNaN;
    }
    return (sign ? kSignBit : 0U) | kInfinity;
  }
  if (x.zero || y.zero) {
    return sign ? kSignBit : 0U;
  }
  return round_pack(sign, x.significand * y.significand, x.exponent + y.exponent);
}

std::uint32_t soft_div(std::uint32_t a, std::uint32_t b) {
  Unpacked x = unpack(a);
  Unpacked y = unpack(b);
  const bool sign = x.sign != y.sign;
  const std::uint32_t sign_bits = sign ? kSignBit : 0U;
  if (x.nan || y.nan || (x.infinity && y.infinity) || (x.zero && y.zero)) {
    return kQuietNaN;
  }
  if (x.infinity || y.zero) {
    return sign_bits | kInfinity;
  }
  if (y.infinity || x.zero) {
    return sign_bits;
  }
  normalize(x);
  normalize(y);
  const std::uint64_t numerator = x.significand << 40U;
  std::uint64_t quotient = numerator / y.significand;
  const bool sticky = numerator % y.significand != 0;
  quotient = (quotient << 1U) | (sticky ? 1U : 0U);
  return round_pack(sign, quotient, x.exponent - 40 - y.exponent - 1);
}

std::string describe_float_bits(std::uint32_t bits) {
  float value = 0.0F;
  std::memcpy(&value, &bits, sizeof(value));
  const std::uint32_t exponent = (bits & kExponentMask) >> 23U;
  const char* kind = is_nan_bits(bits)                                  ? "NaN"
                     : exponent == 0xff                                 ? "infinity"
                     : exponent == 0 && (bits & kFractionMask) == 0      ? "zero"
                     : exponent == 0                                    ? "subnormal"
                                                                        : "normal";
  std::ostringstream out;
  out.precision(9);
  out << bits_text(bits) << "  (" << kind << ", " << value << ")";
  return out.str();
}

std::string to_binary(std::uint64_t value, unsigned bits) {
  std::string text;
  for (unsigned bit = bits; bit-- > 0;) {
    text += ((value >> bit) & 1U) != 0 ? '1' : '0';
  }
  return text;
}

IntegerEncodings encode_integer(std::int64_t value, unsigned bits) {
  IntegerEncodings encodings;
  encodings.bits = bits;
  const std::int64_t magnitude_limit = (std::int64_t{1} << (bits - 1)) - 1;
  const std::uint64_t mask = bits >= 64 ? ~0ULL : (1ULL << bits) - 1U;
  const std::uint64_t magnitude = static_cast<std::uint64_t>(value < 0 ? -value : value);
  if (value >= -magnitude_limit && value <= magnitude_limit) {
    encodings.sign_magnitude = to_binary((value < 0 ? (1ULL << (bits - 1)) : 0U) | magnitude, bits);
    encodings.ones_complement = to_binary(value < 0 ? (~magnitude & mask) : magnitude, bits);
  }
  if (value >= -magnitude_limit - 1 && value <= magnitude_limit) {
    encodings.twos_complement = to_binary(static_cast<std::uint64_t>(value) & mask, bits);
  }
  if (value >= -magnitude_limit && value <= magnitude_limit + 1) {
    encodings.excess = to_binary(static_cast<std::uint64_t>(value + magnitude_limit) & mask, bits);
  }
  if (value >= 0) {
    std::string bcd;
    std::uint64_t remaining = magnitude;
    do {
      bcd.insert(0, to_binary(remaining % 10U, 4) + (bcd.empty() ? "" : " "));
      remaining /= 10U;
    } while (remaining != 0);
    encodings.bcd = bcd;
  }
  return encodings;
}

AlgorithmTrace booth_multiply(std::int64_t multiplicand, std::int64_t multiplier, unsigned bits) {
  AlgorithmTrace trace;
  const std::uint64_t mask = (1ULL << bits) - 1U;
  const std::uint64_t m = static_cast<std::uint64_t>(multiplicand) & mask;
  const std::uint64_t negative_m = (~m + 1U) & mask;
  std::uint64_t a = 0;
  std::uint64_t q = static_cast<std::uint64_t>(multiplier) & mask;
  unsigned q_minus = 0;
  auto snapshot = [&](const std::string& action) {
    trace.steps.push_back("A=" + to_binary(a, bits) + " Q=" + to_binary(q, bits) + " Q-1=" + std::to_string(q_minus) +
                          "  " + action);
  };
  snapshot("init");
  for (unsigned step = 0; step < bits; ++step) {
    const unsigned q0 = static_cast<unsigned>(q & 1U);
    std::string action;
    if (q0 == 1 && q_minus == 0) {
      a = (a + negative_m) & mask;
      action = "10: A -= M, ";
    } else if (q0 == 0 && q_minus == 1) {
      a = (a + m) & mask;
      action = "01: A += M, ";
    } else {
      action = std::to_string(q0) + std::to_string(q_minus) + ": no op, ";
    }
    // arithmetic shift right of (A, Q, Q-1)
    q_minus = q0;
    q = ((q >> 1U) | ((a & 1U) << (bits - 1))) & mask;
    const std::uint64_t sign = a & (1ULL << (bits - 1));
    a = ((a >> 1U) | sign) & mask;
    snapshot(action + "shift");
  }
  const std::uint64_t combined = (a << bits) | q;
  const unsigned width = 2 * bits;
  std::int64_t product = static_cast<std::int64_t>(combined);
  if (width < 64 && (combined & (1ULL << (width - 1))) != 0) {
    product -= static_cast<std::int64_t>(1ULL << width);
  }
  trace.result = product;
  return trace;
}

std::optional<AlgorithmTrace> nonrestoring_divide(std::uint64_t dividend, std::uint64_t divisor, unsigned bits) {
  if (divisor == 0) {
    return std::nullopt;
  }
  AlgorithmTrace trace;
  const std::uint64_t mask = (1ULL << bits) - 1U;
  std::int64_t a = 0;  // remainder register, may go negative
  std::uint64_t q = dividend & mask;
  const auto m = static_cast<std::int64_t>(divisor & mask);
  for (unsigned step = 0; step < bits; ++step) {
    const std::uint64_t top = (q >> (bits - 1)) & 1U;
    a = a * 2 + static_cast<std::int64_t>(top);
    q = (q << 1U) & mask;
    const bool was_negative = a < 0;
    a = was_negative ? a + m : a - m;
    if (a >= 0) {
      q |= 1U;
    }
    trace.steps.push_back("step " + std::to_string(step + 1) + ": " + (was_negative ? "A += M" : "A -= M") +
                          " -> A=" + std::to_string(a) + " Q=" + to_binary(q, bits));
  }
  if (a < 0) {
    a += m;
    trace.steps.push_back("final restore: A += M -> A=" + std::to_string(a));
  }
  trace.result = static_cast<std::int64_t>(q);
  trace.remainder = a;
  return trace;
}

CarryLookaheadResult carry_lookahead_add(std::uint32_t a, std::uint32_t b, bool carry_in) {
  CarryLookaheadResult result;
  const std::uint32_t generate = a & b;
  const std::uint32_t propagate = a ^ b;
  // Group (4-bit) generate/propagate.
  std::uint32_t group_g = 0;
  std::uint32_t group_p = 0;
  for (unsigned group = 0; group < 8; ++group) {
    const unsigned base = group * 4;
    auto g = [&](unsigned bit) { return (generate >> (base + bit)) & 1U; };
    auto p = [&](unsigned bit) { return (propagate >> (base + bit)) & 1U; };
    const std::uint32_t gg = g(3) | (p(3) & g(2)) | (p(3) & p(2) & g(1)) | (p(3) & p(2) & p(1) & g(0));
    const std::uint32_t gp = p(3) & p(2) & p(1) & p(0);
    group_g |= gg << group;
    group_p |= gp << group;
  }
  // Second-level lookahead: carries into each group.
  std::uint32_t group_carry = carry_in ? 1U : 0U;
  std::uint32_t carries = 0;
  for (unsigned group = 0; group < 8; ++group) {
    std::uint32_t carry = group_carry;
    for (unsigned bit = 0; bit < 4; ++bit) {
      const unsigned position = group * 4 + bit;
      carries |= carry << position;
      carry = ((generate >> position) & 1U) | (((propagate >> position) & 1U) & carry);
    }
    group_carry = ((group_g >> group) & 1U) | (((group_p >> group) & 1U) & group_carry);
  }
  result.carries = carries;
  result.sum = propagate ^ carries;
  result.carry_out = group_carry != 0;
  // Gate depth: 1 (g,p) + 2 (group g,p) + 2 per lookahead level (2 levels) + 2 (in-group carry) + 1 (sum xor).
  result.lookahead_delay = 1 + 2 + 2 * 2 + 2 + 1;
  result.ripple_delay = 2 * 32 + 1;
  return result;
}

std::optional<std::vector<std::uint8_t>> utf8_encode(std::uint32_t code_point) {
  if (code_point > 0x10FFFFU || (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
    return std::nullopt;
  }
  if (code_point < 0x80U) {
    return std::vector<std::uint8_t>{static_cast<std::uint8_t>(code_point)};
  }
  if (code_point < 0x800U) {
    return std::vector<std::uint8_t>{static_cast<std::uint8_t>(0xC0U | (code_point >> 6U)),
                                     static_cast<std::uint8_t>(0x80U | (code_point & 0x3FU))};
  }
  if (code_point < 0x10000U) {
    return std::vector<std::uint8_t>{static_cast<std::uint8_t>(0xE0U | (code_point >> 12U)),
                                     static_cast<std::uint8_t>(0x80U | ((code_point >> 6U) & 0x3FU)),
                                     static_cast<std::uint8_t>(0x80U | (code_point & 0x3FU))};
  }
  return std::vector<std::uint8_t>{static_cast<std::uint8_t>(0xF0U | (code_point >> 18U)),
                                   static_cast<std::uint8_t>(0x80U | ((code_point >> 12U) & 0x3FU)),
                                   static_cast<std::uint8_t>(0x80U | ((code_point >> 6U) & 0x3FU)),
                                   static_cast<std::uint8_t>(0x80U | (code_point & 0x3FU))};
}

std::optional<std::vector<std::uint32_t>> utf8_decode(const std::string& text, std::string& error) {
  std::vector<std::uint32_t> code_points;
  for (std::size_t index = 0; index < text.size();) {
    const auto lead = static_cast<std::uint8_t>(text[index]);
    std::size_t length = 0;
    std::uint32_t code_point = 0;
    std::uint32_t minimum = 0;
    if (lead < 0x80U) {
      length = 1;
      code_point = lead;
    } else if ((lead & 0xE0U) == 0xC0U) {
      length = 2;
      code_point = lead & 0x1FU;
      minimum = 0x80U;
    } else if ((lead & 0xF0U) == 0xE0U) {
      length = 3;
      code_point = lead & 0x0FU;
      minimum = 0x800U;
    } else if ((lead & 0xF8U) == 0xF0U) {
      length = 4;
      code_point = lead & 0x07U;
      minimum = 0x10000U;
    } else {
      error = "invalid lead byte at offset " + std::to_string(index);
      return std::nullopt;
    }
    if (index + length > text.size()) {
      error = "truncated sequence at offset " + std::to_string(index);
      return std::nullopt;
    }
    for (std::size_t k = 1; k < length; ++k) {
      const auto byte = static_cast<std::uint8_t>(text[index + k]);
      if ((byte & 0xC0U) != 0x80U) {
        error = "missing continuation byte at offset " + std::to_string(index + k);
        return std::nullopt;
      }
      code_point = (code_point << 6U) | (byte & 0x3FU);
    }
    if (code_point < minimum) {
      error = "overlong encoding at offset " + std::to_string(index);
      return std::nullopt;
    }
    if (code_point > 0x10FFFFU || (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
      error = "invalid code point at offset " + std::to_string(index);
      return std::nullopt;
    }
    code_points.push_back(code_point);
    index += length;
  }
  return code_points;
}

}  // namespace nexus::common
