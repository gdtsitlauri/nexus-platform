#include <cstring>
#include <iostream>
#include <random>
#include <string>

#include "nexus/common/number_systems.hpp"

namespace common = nexus::common;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::uint32_t bits_of(float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

float float_of(std::uint32_t bits) {
  float value = 0.0F;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

bool same(std::uint32_t soft, float hardware) {
  const std::uint32_t hard = bits_of(hardware);
  return common::is_nan_bits(soft) ? common::is_nan_bits(hard) : soft == hard;
}

void check_pair(std::uint32_t a, std::uint32_t b, std::size_t& mismatches) {
  const float x = float_of(a);
  const float y = float_of(b);
  const bool ok = same(common::soft_add(a, b), x + y) && same(common::soft_sub(a, b), x - y) &&
                  same(common::soft_mul(a, b), x * y) && same(common::soft_div(a, b), x / y);
  if (!ok && mismatches++ < 5) {
    std::cerr << "mismatch for " << std::hex << a << ", " << b << std::dec << '\n';
  }
}

void soft_float() {
  const std::uint32_t specials[] = {0x00000000U, 0x80000000U, 0x00000001U, 0x80000001U, 0x007fffffU, 0x00800000U,
                                    0x3f800000U, 0xbf800000U, 0x7f7fffffU, 0xff7fffffU, 0x7f800000U, 0xff800000U,
                                    0x7fc00000U, 0x3f800001U, 0x33800000U, 0x4b000000U, 0x3eaaaaabU, 0x00400000U};
  std::size_t mismatches = 0;
  for (const auto a : specials) {
    for (const auto b : specials) {
      check_pair(a, b, mismatches);
    }
  }
  std::mt19937 random(754);
  std::uniform_int_distribution<std::uint32_t> any;
  for (int sample = 0; sample < 1'000'000; ++sample) {
    check_pair(any(random), any(random), mismatches);
  }
  // Operands with nearby exponents exercise cancellation and rounding ties.
  for (int sample = 0; sample < 1'000'000; ++sample) {
    const std::uint32_t a = any(random);
    const std::uint32_t b = (a & 0xff800000U) ^ (any(random) & 0x807fffffU) ^ ((any(random) & 3U) << 23U);
    check_pair(a, b, mismatches);
  }
  expect(mismatches == 0, std::to_string(mismatches) + " soft-float results differ from hardware binary32");
}

void representations() {
  const auto minus_five = common::encode_integer(-5, 8);
  expect(minus_five.sign_magnitude == "10000101", "sign-magnitude of -5");
  expect(minus_five.ones_complement == "11111010", "one's complement of -5");
  expect(minus_five.twos_complement == "11111011", "two's complement of -5");
  expect(minus_five.excess == "01111010", "excess-127 of -5");
  expect(!minus_five.bcd.has_value(), "BCD is defined for non-negative values only");
  const auto minus_128 = common::encode_integer(-128, 8);
  expect(minus_128.twos_complement == "10000000" && !minus_128.sign_magnitude.has_value(),
         "-128 fits two's complement but not sign-magnitude in 8 bits");
  expect(common::encode_integer(905, 16).bcd == "1001 0000 0101", "packed BCD of 905");
}

void algorithms() {
  std::mt19937 random(42);
  for (int sample = 0; sample < 20000; ++sample) {
    const auto a = static_cast<std::int32_t>(random());
    const auto b = static_cast<std::int32_t>(random());
    const auto product = common::booth_multiply(a, b, 32);
    if (product.result != static_cast<std::int64_t>(a) * b) {
      expect(false, "Booth multiplication " + std::to_string(a) + " * " + std::to_string(b));
      break;
    }
    const auto ua = static_cast<std::uint32_t>(random());
    const auto ub = static_cast<std::uint32_t>(random() >> (random() % 31U)) | 1U;
    const auto quotient = common::nonrestoring_divide(ua, ub, 32);
    if (!quotient || quotient->result != static_cast<std::int64_t>(ua / ub) ||
        quotient->remainder != static_cast<std::int64_t>(ua % ub)) {
      expect(false, "non-restoring division " + std::to_string(ua) + " / " + std::to_string(ub));
      break;
    }
    const bool carry_in = (random() & 1U) != 0;
    const auto sum = common::carry_lookahead_add(ua, ub, carry_in);
    const std::uint64_t wide = std::uint64_t{ua} + ub + (carry_in ? 1U : 0U);
    if (sum.sum != static_cast<std::uint32_t>(wide) || sum.carry_out != ((wide >> 32U) != 0)) {
      expect(false, "carry-lookahead addition");
      break;
    }
  }
  expect(common::booth_multiply(-3, 5, 4).result == -15, "Booth on 4-bit operands: -3 * 5 = -15");
  expect(!common::nonrestoring_divide(1, 0, 8).has_value(), "division by zero is rejected");
  expect(common::carry_lookahead_add(1, 1).lookahead_delay < common::carry_lookahead_add(1, 1).ripple_delay,
         "lookahead is shallower than ripple carry");
}

void unicode() {
  std::string error;
  for (std::uint32_t code_point = 0; code_point <= 0x10FFFFU; code_point += (code_point < 0x3000U ? 1U : 97U)) {
    const auto bytes = common::utf8_encode(code_point);
    if (code_point >= 0xD800U && code_point <= 0xDFFFU) {
      expect(!bytes.has_value(), "surrogates have no UTF-8 encoding");
      continue;
    }
    const std::string text(bytes->begin(), bytes->end());
    const auto decoded = common::utf8_decode(text, error);
    if (!decoded || decoded->size() != 1 || decoded->front() != code_point) {
      expect(false, "UTF-8 round trip of U+" + std::to_string(code_point));
      break;
    }
  }
  expect(common::utf8_encode(0x20AC) == std::vector<std::uint8_t>({0xE2, 0x82, 0xAC}), "euro sign is E2 82 AC");
  expect(!common::utf8_decode(std::string("\xC0\xAF"), error).has_value(), "overlong '/' is rejected");
  expect(!common::utf8_decode(std::string("\xE2\x82"), error).has_value(), "truncated sequence is rejected");
  expect(!common::utf8_decode(std::string("\xED\xA0\x80"), error).has_value(), "encoded surrogate is rejected");
  const auto greek = common::utf8_decode("\xCE\x9D\xCE\xB5\xCE\xBE", error);  // "Νεξ"
  expect(greek && greek->size() == 3 && greek->front() == 0x039D, "Greek text decodes to code points");
}

}  // namespace

int main() {
  soft_float();
  representations();
  algorithms();
  unicode();
  if (failures == 0) {
    std::cout << "number_systems_test: all checks passed\n";
  }
  return failures == 0 ? 0 : 1;
}
