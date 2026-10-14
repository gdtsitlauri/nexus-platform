#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "nexus/logic/minimize.hpp"

// Programmable combinational logic (EY121): multi-output minimisation for a PLA with shared product terms and
// optional output inversion (XOR at each output), the PAL alternative with a fixed OR array, and the ROM table.
namespace nexus::logic {

struct MultiOutput {
  std::vector<std::uint32_t> ones;
  std::vector<std::uint32_t> dont_cares;
};

struct PlaTerm {
  Cube cube;
  std::vector<bool> outputs;  // OR-array connections
};

struct Pla {
  std::size_t inputs = 0;
  std::vector<bool> complemented;  // output i realised as the complement (programmed XOR = 1)
  std::vector<PlaTerm> terms;      // AND-array rows
  std::size_t connections = 0;     // OR-array connections
  bool exact = true;               // false when the row search hit its node limit
};

// Fewest AND-array rows: candidate terms are the primes of every product of output subsets, the cover is found by
// branch and bound over (output, minterm) pairs. With `allow_complement`, every output polarity is tried.
Pla minimize_pla(std::size_t n, const std::vector<MultiOutput>& outputs, bool allow_complement = true);

// PAL: each output has its own product terms (fixed OR array); returns the minimal number of terms per output.
std::vector<std::size_t> pal_terms(std::size_t n, const std::vector<MultiOutput>& outputs);

bool pla_implements(const Pla& pla, const std::vector<MultiOutput>& outputs);

// Programming table in the textbook format: product term, input pattern (1, 0, -), output columns, T/C row.
std::string pla_table(const Pla& pla, const std::vector<std::string>& vars);
std::string rom_table(std::size_t n, const std::vector<MultiOutput>& outputs);

}  // namespace nexus::logic
