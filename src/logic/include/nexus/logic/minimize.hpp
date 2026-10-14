#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Two-level minimisation (EY121): Quine-McCluskey prime implicants, essential primes, Petrick's method and an
// exact branch-and-bound cover; Karnaugh maps for 2-4 variables.
namespace nexus::logic {

// A product term over n variables: bit (n-1-i) of `care` set means variable i appears, with polarity given by the
// same bit of `value`. Bits outside `care` in `value` are zero.
struct Cube {
  std::uint32_t value = 0;
  std::uint32_t care = 0;
  bool operator==(const Cube&) const = default;
  bool covers(std::uint32_t minterm) const { return (minterm & care) == value; }
};

std::size_t literal_count(const Cube& c);
std::string cube_to_string(const Cube& c, const std::vector<std::string>& vars);
std::string cube_to_pattern(const Cube& c, std::size_t n);  // e.g. "1-0"
std::string sop_to_string(const std::vector<Cube>& cover, const std::vector<std::string>& vars);
std::string pos_to_string(const std::vector<Cube>& cover_of_zeros, const std::vector<std::string>& vars);

struct QmColumn {
  std::vector<std::vector<Cube>> groups;  // cubes of this column grouped by the number of 1s
};

struct QmResult {
  std::vector<Cube> primes;
  std::vector<QmColumn> columns;  // the tabulation, for display
};

// Prime implicants of the function that is 1 on `ones` and unspecified on `dont_cares` (n <= 16).
QmResult quine_mccluskey(std::size_t n, const std::vector<std::uint32_t>& ones,
                         const std::vector<std::uint32_t>& dont_cares = {});

enum class CoverMethod { Petrick, BranchAndBound };

struct Cover {
  std::vector<Cube> cubes;
  std::vector<Cube> essential;
  std::size_t literals = 0;
  CoverMethod method = CoverMethod::BranchAndBound;
};

// Minimum-cost cover of `ones` by `primes`: fewest cubes, then fewest literals. Essential primes are taken first.
// Petrick's method expands the product of sums exactly (used when at most `petrick_limit` primes remain); the
// branch-and-bound search is exact as well and used otherwise.
Cover minimum_cover(std::size_t n, const std::vector<Cube>& primes, const std::vector<std::uint32_t>& ones,
                    std::size_t petrick_limit = 14);
Cover petrick(std::size_t n, const std::vector<Cube>& primes, const std::vector<std::uint32_t>& ones);
Cover branch_and_bound(std::size_t n, const std::vector<Cube>& primes, const std::vector<std::uint32_t>& ones);

struct MinimizeResult {
  QmResult qm;
  Cover cover;
};

MinimizeResult minimize_sop(std::size_t n, const std::vector<std::uint32_t>& ones,
                            const std::vector<std::uint32_t>& dont_cares = {});
// Minimal product of sums: minimises the zeros; each returned cube is the complement of one sum term.
MinimizeResult minimize_pos(std::size_t n, const std::vector<std::uint32_t>& ones,
                            const std::vector<std::uint32_t>& dont_cares = {});

bool cover_implements(std::size_t n, const std::vector<Cube>& cover, const std::vector<std::uint32_t>& ones,
                      const std::vector<std::uint32_t>& dont_cares);

// Karnaugh map of an n-variable function (2 <= n <= 4) in Gray-code order. `values`: 0, 1 or 2 (don't care) per
// minterm. The cover's cubes are listed with the cells each one groups.
std::string karnaugh_map(const std::vector<std::string>& vars, const std::vector<std::uint8_t>& values,
                         const std::vector<Cube>& cover);

}  // namespace nexus::logic
