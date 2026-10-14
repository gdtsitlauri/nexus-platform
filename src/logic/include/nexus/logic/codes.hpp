#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Codes (EY121 "number systems and codes"): Gray code, parity and Hamming single-error-correcting codes.
namespace nexus::logic {

std::uint32_t gray_encode(std::uint32_t binary);
std::uint32_t gray_decode(std::uint32_t gray);

bool even_parity_bit(std::uint32_t data);  // the bit that makes the total number of 1s even

// Hamming code with parity bits at positions 1, 2, 4, 8, ... (1-based, LSB-first codeword vector).
std::size_t hamming_parity_bits(std::size_t data_bits);  // smallest r with 2^r >= data_bits + r + 1
std::vector<std::uint8_t> hamming_encode(const std::vector<std::uint8_t>& data);

struct HammingDecode {
  std::vector<std::uint8_t> data;
  std::size_t syndrome = 0;  // 0 = no error, otherwise the 1-based position that was flipped
  bool corrected = false;
};
HammingDecode hamming_decode(std::vector<std::uint8_t> codeword, std::size_t data_bits);

}  // namespace nexus::logic
