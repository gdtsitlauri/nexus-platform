#include "nexus/logic/codes.hpp"

#include <bit>
#include <stdexcept>

namespace nexus::logic {

std::uint32_t gray_encode(std::uint32_t binary) { return binary ^ (binary >> 1U); }

std::uint32_t gray_decode(std::uint32_t gray) {
  std::uint32_t b = gray;
  for (std::uint32_t shift = 1; shift < 32; shift <<= 1U) {
    b ^= b >> shift;
  }
  return b;
}

bool even_parity_bit(std::uint32_t data) { return (std::popcount(data) & 1) != 0; }

std::size_t hamming_parity_bits(std::size_t data_bits) {
  std::size_t r = 0;
  while ((std::size_t{1} << r) < data_bits + r + 1) {
    ++r;
  }
  return r;
}

namespace {
bool is_power_of_two(std::size_t x) { return x != 0 && (x & (x - 1)) == 0; }
}  // namespace

std::vector<std::uint8_t> hamming_encode(const std::vector<std::uint8_t>& data) {
  const std::size_t r = hamming_parity_bits(data.size());
  const std::size_t n = data.size() + r;
  std::vector<std::uint8_t> code(n + 1, 0);  // index 0 unused: positions are 1-based
  std::size_t d = 0;
  for (std::size_t pos = 1; pos <= n; ++pos) {
    if (!is_power_of_two(pos)) {
      code[pos] = data[d++] != 0 ? 1 : 0;
    }
  }
  for (std::size_t p = 1; p <= n; p <<= 1U) {
    std::uint8_t parity = 0;
    for (std::size_t pos = 1; pos <= n; ++pos) {
      if ((pos & p) != 0 && pos != p) {
        parity ^= code[pos];
      }
    }
    code[p] = parity;
  }
  return {code.begin() + 1, code.end()};
}

HammingDecode hamming_decode(std::vector<std::uint8_t> codeword, std::size_t data_bits) {
  const std::size_t n = codeword.size();
  if (n != data_bits + hamming_parity_bits(data_bits)) {
    throw std::runtime_error("hamming_decode: codeword length does not match");
  }
  HammingDecode out;
  for (std::size_t p = 1; p <= n; p <<= 1U) {
    std::uint8_t parity = 0;
    for (std::size_t pos = 1; pos <= n; ++pos) {
      if ((pos & p) != 0) {
        parity ^= codeword[pos - 1];
      }
    }
    if (parity != 0) {
      out.syndrome |= p;
    }
  }
  if (out.syndrome != 0 && out.syndrome <= n) {
    codeword[out.syndrome - 1] ^= 1U;
    out.corrected = true;
  }
  for (std::size_t pos = 1; pos <= n; ++pos) {
    if (!is_power_of_two(pos)) {
      out.data.push_back(codeword[pos - 1]);
    }
  }
  return out;
}

}  // namespace nexus::logic
