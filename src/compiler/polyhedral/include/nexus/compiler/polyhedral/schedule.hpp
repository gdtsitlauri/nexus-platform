#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "nexus/compiler/polyhedral/polyhedral.hpp"

namespace nexus::compiler::polyhedral {

// Polyhedral transformations based on linear programming (NEY709 (h)).
//
// solve_lp: exact rational two-phase simplex with Bland's rule for
//     maximise c.x  subject to  A x <= b,  x >= 0.
// solve_ilp: branch and bound on top of solve_lp for integer x.
// find_hyperplane_schedule: Lamport's hyperplane method posed as an integer program.  It looks for an integer
//     time vector pi with pi.d >= 1 for every dependence distance d (so every dependence is carried by the
//     new outer loop and all inner loops become parallel), minimising sum |pi_k|; pi is completed to a
//     unimodular matrix whose first row is pi, and the transformation is applied, checked for legality and
//     verified by executing both nests.

struct Fraction {
  std::int64_t num = 0;
  std::int64_t den = 1;
  Fraction() = default;
  Fraction(std::int64_t n, std::int64_t d = 1);  // normalises (den > 0, gcd 1)
  [[nodiscard]] double value() const { return static_cast<double>(num) / static_cast<double>(den); }
  [[nodiscard]] bool integer() const { return den == 1; }
  [[nodiscard]] std::string text() const;
};
Fraction operator+(const Fraction& a, const Fraction& b);
Fraction operator-(const Fraction& a, const Fraction& b);
Fraction operator*(const Fraction& a, const Fraction& b);
Fraction operator/(const Fraction& a, const Fraction& b);
bool operator<(const Fraction& a, const Fraction& b);
bool operator==(const Fraction& a, const Fraction& b);
Fraction floor_of(const Fraction& f);

struct LpProblem {
  std::vector<std::vector<Fraction>> a;  // constraint rows over the variables
  std::vector<Fraction> b;
  std::vector<Fraction> objective;       // maximise
};

enum class LpStatus { Optimal, Infeasible, Unbounded };

struct LpSolution {
  LpStatus status = LpStatus::Infeasible;
  std::vector<Fraction> x;
  Fraction value;
  std::size_t pivots = 0;
};

struct IlpSolution {
  LpStatus status = LpStatus::Infeasible;
  std::vector<std::int64_t> x;
  Fraction value;
  Fraction relaxation;    // value of the LP relaxation at the root
  std::size_t nodes = 0;  // branch-and-bound nodes
  std::size_t pivots = 0;
};

LpSolution solve_lp(const LpProblem& problem);
IlpSolution solve_ilp(const LpProblem& problem, std::size_t max_nodes = 200000);
std::string lp_status_name(LpStatus status);

// A unimodular integer matrix whose first row is `row` (gcd of the entries must be 1).
std::optional<Matrix> unimodular_completion(const std::vector<std::int64_t>& row);
std::int64_t determinant(const Matrix& m);

struct ScheduleResult {
  bool found = false;
  std::string reason;
  std::vector<std::vector<std::int64_t>> distances;  // the constraints pi.d >= 1
  std::vector<std::int64_t> hyperplane;              // pi
  Matrix transform;
  IlpSolution ilp;
  std::int64_t wavefronts = 0;    // number of distinct values of pi.i over the domain (sequential steps)
  std::size_t instances = 0;
  TransformResult applied;
};

ScheduleResult find_hyperplane_schedule(const LoopNest& nest, const DependenceAnalysis& analysis,
                                        std::int64_t bound = 8);
std::string print_schedule(const LoopNest& nest, const ScheduleResult& result);

}  // namespace nexus::compiler::polyhedral
