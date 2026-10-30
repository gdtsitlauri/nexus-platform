#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nexus::compiler::polyhedral {

// A small polyhedral loop-nest framework.
//
// Input (a static control part over a perfect loop nest):
//   params N = 6, M = 5
//   for i = 0 to N - 1 {
//     for j = 1 to M - 1 {
//       A[i][j] = A[i][j - 1] + B[j][i] * 2
//     }
//   }
// Bounds are inclusive affine expressions of outer indices and parameters; subscripts are affine.
//
// Analysis: iteration domains as integer polyhedra, exact dependence testing by Fourier-Motzkin
// elimination with gcd tightening (cross-checked by enumerating the concrete domain), direction
// and distance vectors, loop-carried levels and parallel loops.
// Transformation: unimodular matrices (interchange, reversal, skewing) with a legality check
// (every transformed distance vector must stay lexicographically positive), loop-bound
// generation by Fourier-Motzkin projection, and verification by executing both nests.

using Affine = std::vector<std::int64_t>;  // coefficients over [loop indices..., 1]

struct Access {
  std::string array;
  std::vector<Affine> subscripts;
  bool write = false;
};

struct Expression;  // statement right-hand side (opaque here)

struct Statement {
  std::string text;
  Access target;
  std::vector<Access> reads;
  std::shared_ptr<Expression> rhs;
};

struct LoopNest {
  std::vector<std::string> indices;
  std::map<std::string, std::int64_t> params;
  std::vector<Affine> lower;  // per loop, in terms of outer indices (params substituted)
  std::vector<Affine> upper;
  std::vector<Statement> statements;
};

struct NestResult {
  std::optional<LoopNest> nest;
  std::string error;
};

NestResult parse_loop_nest(const std::string& text);

enum class DependenceKind { Flow, Anti, Output };

struct Dependence {
  DependenceKind kind = DependenceKind::Flow;
  std::size_t source_statement = 0;
  std::size_t target_statement = 0;
  std::string array;
  std::size_t level = 0;              // carrying loop (1-based); depth + 1 = loop-independent
  std::string direction;              // e.g. "(=,<)"
  std::optional<std::vector<std::int64_t>> distance;  // when uniform
  std::vector<std::vector<std::int64_t>> distances;   // all distinct distances (enumeration)
  bool fourier_motzkin_feasible = true;
};

struct DependenceAnalysis {
  std::vector<Dependence> dependences;
  std::vector<bool> parallel;         // per loop: carries no dependence
  std::size_t fm_tests = 0;
  std::size_t fm_false_positives = 0;  // FM feasible but no integer instance (real-shadow imprecision)
  std::size_t fm_misses = 0;           // FM infeasible but enumeration found an instance (must stay 0)
};

DependenceAnalysis analyze_dependences(const LoopNest& nest);
std::string print_dependences(const LoopNest& nest, const DependenceAnalysis& analysis);

using Matrix = std::vector<std::vector<std::int64_t>>;

std::optional<Matrix> interchange_matrix(std::size_t depth, std::size_t a, std::size_t b);
std::optional<Matrix> reversal_matrix(std::size_t depth, std::size_t loop);
std::optional<Matrix> skew_matrix(std::size_t depth, std::size_t target, std::size_t source, std::int64_t factor);
Matrix multiply(const Matrix& lhs, const Matrix& rhs);

struct TransformResult {
  bool legal = false;
  std::string reason;
  std::string code;                   // generated loop nest
  bool verified = false;              // executing both nests gives identical arrays
  std::size_t original_instances = 0;
  std::size_t transformed_instances = 0;
  std::vector<bool> parallel_after;   // per new loop: carries no transformed dependence
};

TransformResult apply_transformation(const LoopNest& nest, const DependenceAnalysis& analysis, const Matrix& transform);

// ---- Loop tiling for locality (NEY709 (h)) ------------------------------------------------------
//
// Rectangular tiling of the band y = T x: tile loops t_k step s_k over the whole range of y_k, then point
// loops y_k = max(lower_k, t_k) to min(upper_k, t_k + s_k - 1).  Tiling the band is legal when it is fully
// permutable (Irigoin-Triolet, Wolf-Lam): no dependence instance has a negative component in T d.  The test
// uses the exact distances when the domain was enumerated and Fourier-Motzkin otherwise (sound: it may
// refuse a legal tiling, never accept an illegal one).  The effect on locality is measured by running the
// address trace of both nests through a set-associative LRU cache (row-major arrays, one element per word).

struct CacheConfig {
  std::size_t line_elements = 8;  // 64-byte lines of 8-byte elements
  std::size_t sets = 16;
  std::size_t ways = 4;           // 16 x 4 x 64 B = 4 KiB
};

struct CacheStats {
  std::size_t accesses = 0;
  std::size_t misses = 0;
  [[nodiscard]] double miss_rate() const {
    return accesses == 0 ? 0.0 : static_cast<double>(misses) / static_cast<double>(accesses);
  }
};

struct TileResult {
  bool legal = false;
  std::string reason;
  std::vector<std::int64_t> tile_sizes;  // 0 = loop not tiled
  std::string code;
  bool verified = false;                 // every instance once, arrays identical to the original nest
  std::size_t original_instances = 0;
  std::size_t tiled_instances = 0;
  std::size_t tiles = 0;                 // non-empty tiles
  CacheStats original_cache;
  CacheStats tiled_cache;
};

// True when every dependence instance has T d >= 0 componentwise; otherwise `reason` names one that does not.
bool fully_permutable(const LoopNest& nest, const DependenceAnalysis& analysis, const Matrix& transform,
                      std::string* reason = nullptr);
// The lower-triangular unimodular skew S (unit diagonal, factors 0..max_factor, smallest sum first) that makes
// S T fully permutable; nullopt when none exists in that range.
std::optional<Matrix> find_tiling_skew(const LoopNest& nest, const DependenceAnalysis& analysis, const Matrix& transform,
                                       std::int64_t max_factor = 3);
TileResult apply_tiling(const LoopNest& nest, const DependenceAnalysis& analysis, const Matrix& transform,
                        const std::vector<std::int64_t>& tile_sizes, const CacheConfig& cache = {});
// Misses of the nest executed in its own order.
CacheStats simulate_cache(const LoopNest& nest, const CacheConfig& cache);

// Executes the nest (deterministic initial array contents) and returns every written element.
std::map<std::string, std::map<std::vector<std::int64_t>, std::int64_t>> execute(const LoopNest& nest);

}  // namespace nexus::compiler::polyhedral
