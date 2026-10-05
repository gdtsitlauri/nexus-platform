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

// Executes the nest (deterministic initial array contents) and returns every written element.
std::map<std::string, std::map<std::vector<std::int64_t>, std::int64_t>> execute(const LoopNest& nest);

}  // namespace nexus::compiler::polyhedral
