#include "nexus/compiler/polyhedral/schedule.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>

namespace nexus::compiler::polyhedral {

// ---- exact rationals -------------------------------------------------------------------------------

namespace {
std::int64_t narrow(__int128 v) {
  if (v > std::numeric_limits<std::int64_t>::max() || v < std::numeric_limits<std::int64_t>::min()) {
    throw std::overflow_error("rational overflow in the simplex");
  }
  return static_cast<std::int64_t>(v);
}
Fraction make(__int128 n, __int128 d) {
  if (d == 0) throw std::domain_error("division by zero");
  if (d < 0) {
    n = -n;
    d = -d;
  }
  __int128 a = n < 0 ? -n : n;
  __int128 b = d;
  while (b != 0) {
    const __int128 t = a % b;
    a = b;
    b = t;
  }
  if (a > 1) {
    n /= a;
    d /= a;
  }
  Fraction f;
  f.num = narrow(n);
  f.den = narrow(d);
  return f;
}
}  // namespace

Fraction::Fraction(std::int64_t n, std::int64_t d) { *this = make(n, d); }

std::string Fraction::text() const {
  return den == 1 ? std::to_string(num) : std::to_string(num) + "/" + std::to_string(den);
}

Fraction operator+(const Fraction& a, const Fraction& b) {
  return make(static_cast<__int128>(a.num) * b.den + static_cast<__int128>(b.num) * a.den, static_cast<__int128>(a.den) * b.den);
}
Fraction operator-(const Fraction& a, const Fraction& b) {
  return make(static_cast<__int128>(a.num) * b.den - static_cast<__int128>(b.num) * a.den, static_cast<__int128>(a.den) * b.den);
}
Fraction operator*(const Fraction& a, const Fraction& b) {
  return make(static_cast<__int128>(a.num) * b.num, static_cast<__int128>(a.den) * b.den);
}
Fraction operator/(const Fraction& a, const Fraction& b) {
  return make(static_cast<__int128>(a.num) * b.den, static_cast<__int128>(a.den) * b.num);
}
bool operator<(const Fraction& a, const Fraction& b) {
  return static_cast<__int128>(a.num) * b.den < static_cast<__int128>(b.num) * a.den;
}
bool operator==(const Fraction& a, const Fraction& b) { return a.num == b.num && a.den == b.den; }
Fraction floor_of(const Fraction& f) {
  std::int64_t q = f.num / f.den;
  if (f.num % f.den != 0 && f.num < 0) --q;
  return Fraction(q);
}

// ---- simplex -----------------------------------------------------------------------------------------

namespace {

struct Tableau {
  std::vector<std::vector<Fraction>> rows;  // each: columns..., rhs
  std::vector<std::size_t> basis;
  std::size_t columns = 0;
};

void pivot(Tableau& t, std::size_t row, std::size_t col) {
  const Fraction p = t.rows[row][col];
  for (auto& v : t.rows[row]) v = v / p;
  for (std::size_t r = 0; r < t.rows.size(); ++r) {
    if (r == row || t.rows[r][col].num == 0) continue;
    const Fraction factor = t.rows[r][col];
    for (std::size_t c = 0; c <= t.columns; ++c) {
      if (t.rows[row][c].num != 0) t.rows[r][c] = t.rows[r][c] - factor * t.rows[row][c];
    }
  }
  t.basis[row] = col;
}

// Maximises cost.x over the allowed columns with Bland's rule.
LpStatus optimise(Tableau& t, const std::vector<Fraction>& cost, const std::vector<bool>& allowed, std::size_t& pivots) {
  while (true) {
    std::optional<std::size_t> entering;
    for (std::size_t j = 0; j < t.columns && !entering; ++j) {
      if (!allowed[j] || std::find(t.basis.begin(), t.basis.end(), j) != t.basis.end()) continue;
      Fraction reduced = cost[j];
      for (std::size_t i = 0; i < t.rows.size(); ++i) {
        if (t.rows[i][j].num != 0) reduced = reduced - cost[t.basis[i]] * t.rows[i][j];
      }
      if (Fraction(0) < reduced) entering = j;
    }
    if (!entering) return LpStatus::Optimal;
    std::optional<std::size_t> leaving;
    Fraction best;
    for (std::size_t i = 0; i < t.rows.size(); ++i) {
      if (!(Fraction(0) < t.rows[i][*entering])) continue;
      const Fraction ratio = t.rows[i][t.columns] / t.rows[i][*entering];
      if (!leaving || ratio < best || (ratio == best && t.basis[i] < t.basis[*leaving])) {
        leaving = i;
        best = ratio;
      }
    }
    if (!leaving) return LpStatus::Unbounded;
    pivot(t, *leaving, *entering);
    ++pivots;
  }
}

}  // namespace

LpSolution solve_lp(const LpProblem& problem) {
  LpSolution solution;
  const std::size_t m = problem.a.size();
  const std::size_t n = problem.objective.size();
  std::vector<std::size_t> artificial_rows;
  for (std::size_t i = 0; i < m; ++i) {
    if (problem.b[i] < Fraction(0)) artificial_rows.push_back(i);
  }
  Tableau t;
  t.columns = n + m + artificial_rows.size();
  t.rows.assign(m, std::vector<Fraction>(t.columns + 1));
  t.basis.assign(m, 0);
  for (std::size_t i = 0, art = 0; i < m; ++i) {
    const bool negative = problem.b[i] < Fraction(0);
    const Fraction sign(negative ? -1 : 1);
    for (std::size_t j = 0; j < n; ++j) t.rows[i][j] = sign * problem.a[i][j];
    t.rows[i][n + i] = sign;
    t.rows[i][t.columns] = sign * problem.b[i];
    if (negative) {
      t.rows[i][n + m + art] = Fraction(1);
      t.basis[i] = n + m + art;
      ++art;
    } else {
      t.basis[i] = n + i;
    }
  }
  std::vector<bool> allowed(t.columns, true);
  if (!artificial_rows.empty()) {
    std::vector<Fraction> phase1(t.columns);
    for (std::size_t k = 0; k < artificial_rows.size(); ++k) phase1[n + m + k] = Fraction(-1);
    optimise(t, phase1, allowed, solution.pivots);
    Fraction value;
    for (std::size_t i = 0; i < m; ++i) value = value + phase1[t.basis[i]] * t.rows[i][t.columns];
    if (value < Fraction(0)) {
      solution.status = LpStatus::Infeasible;
      return solution;
    }
    for (std::size_t i = 0; i < m; ++i) {  // drive zero-valued artificials out of the basis
      if (t.basis[i] < n + m) continue;
      for (std::size_t j = 0; j < n + m; ++j) {
        if (t.rows[i][j].num != 0) {
          pivot(t, i, j);
          ++solution.pivots;
          break;
        }
      }
    }
    for (std::size_t k = 0; k < artificial_rows.size(); ++k) allowed[n + m + k] = false;
  }
  std::vector<Fraction> cost(t.columns);
  for (std::size_t j = 0; j < n; ++j) cost[j] = problem.objective[j];
  solution.status = optimise(t, cost, allowed, solution.pivots);
  if (solution.status != LpStatus::Optimal) return solution;
  solution.x.assign(n, Fraction(0));
  for (std::size_t i = 0; i < m; ++i) {
    if (t.basis[i] < n) solution.x[t.basis[i]] = t.rows[i][t.columns];
  }
  for (std::size_t j = 0; j < n; ++j) solution.value = solution.value + problem.objective[j] * solution.x[j];
  return solution;
}

IlpSolution solve_ilp(const LpProblem& problem, std::size_t max_nodes) {
  IlpSolution best;
  bool have = false;
  const std::size_t n = problem.objective.size();
  std::function<void(const LpProblem&, bool)> branch = [&](const LpProblem& node, bool root) {
    if (best.nodes >= max_nodes) return;
    ++best.nodes;
    const auto relaxed = solve_lp(node);
    best.pivots += relaxed.pivots;
    if (root) best.relaxation = relaxed.value;
    if (relaxed.status == LpStatus::Unbounded && root) {
      best.status = LpStatus::Unbounded;
      return;
    }
    if (relaxed.status != LpStatus::Optimal) return;
    if (have && !(best.value < relaxed.value)) return;  // bound
    for (std::size_t j = 0; j < n; ++j) {
      if (relaxed.x[j].integer()) continue;
      const Fraction down = floor_of(relaxed.x[j]);
      LpProblem left = node;
      std::vector<Fraction> row(n);
      row[j] = Fraction(1);
      left.a.push_back(row);
      left.b.push_back(down);
      branch(left, false);
      LpProblem right = node;
      row[j] = Fraction(-1);
      right.a.push_back(row);
      right.b.push_back(Fraction(0) - (down + Fraction(1)));
      branch(right, false);
      return;
    }
    have = true;
    best.status = LpStatus::Optimal;
    best.value = relaxed.value;
    best.x.assign(n, 0);
    for (std::size_t j = 0; j < n; ++j) best.x[j] = relaxed.x[j].num;
  };
  branch(problem, true);
  return best;
}

std::string lp_status_name(LpStatus status) {
  switch (status) {
    case LpStatus::Optimal: return "optimal";
    case LpStatus::Infeasible: return "infeasible";
    case LpStatus::Unbounded: return "unbounded";
  }
  return "?";
}

// ---- unimodular completion --------------------------------------------------------------------------

std::optional<Matrix> unimodular_completion(const std::vector<std::int64_t>& row) {
  const std::size_t d = row.size();
  if (d == 0) return std::nullopt;
  std::vector<std::int64_t> w = row;
  Matrix inverse(d, std::vector<std::int64_t>(d, 0));  // V^-1, kept in step with column operations on V
  for (std::size_t i = 0; i < d; ++i) inverse[i][i] = 1;
  while (true) {
    std::optional<std::size_t> pivot_index;
    std::size_t nonzero = 0;
    for (std::size_t i = 0; i < d; ++i) {
      if (w[i] == 0) continue;
      ++nonzero;
      if (!pivot_index || std::llabs(w[i]) < std::llabs(w[*pivot_index])) pivot_index = i;
    }
    if (!pivot_index) return std::nullopt;  // the zero vector
    if (nonzero == 1) {
      const std::size_t i = *pivot_index;
      if (std::llabs(w[i]) != 1) return std::nullopt;  // gcd > 1
      std::swap(w[0], w[i]);
      std::swap(inverse[0], inverse[i]);
      if (w[0] == -1) {
        for (auto& v : inverse[0]) v = -v;
      }
      return inverse;
    }
    const std::size_t i = *pivot_index;
    for (std::size_t j = 0; j < d; ++j) {
      if (j == i || w[j] == 0) continue;
      const std::int64_t q = w[j] / w[i];
      w[j] -= q * w[i];                       // column j of V -= q * column i
      for (std::size_t c = 0; c < d; ++c) {   // so row i of V^-1 += q * row j
        inverse[i][c] += q * inverse[j][c];
      }
    }
  }
}

std::int64_t determinant(const Matrix& m) {
  const std::size_t n = m.size();
  if (n == 1) return m[0][0];
  std::int64_t det = 0;
  for (std::size_t col = 0; col < n; ++col) {
    Matrix minor;
    for (std::size_t r = 1; r < n; ++r) {
      std::vector<std::int64_t> row;
      for (std::size_t c = 0; c < n; ++c) {
        if (c != col) row.push_back(m[r][c]);
      }
      minor.push_back(row);
    }
    det += (col % 2 == 0 ? 1 : -1) * m[0][col] * determinant(minor);
  }
  return det;
}

// ---- hyperplane schedule ----------------------------------------------------------------------------

namespace {
void enumerate(const LoopNest& nest, std::size_t level, std::vector<std::int64_t>& point,
               const std::function<void(const std::vector<std::int64_t>&)>& visit) {
  if (level == nest.indices.size()) {
    visit(point);
    return;
  }
  auto evaluate = [&](const Affine& bound) {
    std::int64_t v = bound.back();
    for (std::size_t k = 0; k < level; ++k) v += bound[k] * point[k];
    return v;
  };
  const auto lo = evaluate(nest.lower[level]);
  const auto hi = evaluate(nest.upper[level]);
  for (std::int64_t x = lo; x <= hi; ++x) {
    point[level] = x;
    enumerate(nest, level + 1, point, visit);
  }
}
}  // namespace

ScheduleResult find_hyperplane_schedule(const LoopNest& nest, const DependenceAnalysis& analysis, std::int64_t bound) {
  ScheduleResult result;
  const std::size_t d = nest.indices.size();
  std::set<std::vector<std::int64_t>> unique;
  for (const auto& dependence : analysis.dependences) {
    for (const auto& distance : dependence.distances) {
      if (std::any_of(distance.begin(), distance.end(), [](std::int64_t v) { return v != 0; })) unique.insert(distance);
    }
  }
  result.distances.assign(unique.begin(), unique.end());
  // Variables p_0..p_{d-1}, q_0..q_{d-1} >= 0 with pi = p - q.
  LpProblem problem;
  problem.objective.assign(2 * d, Fraction(-1));  // maximise -sum(p + q) = minimise sum |pi|
  for (const auto& distance : result.distances) {
    std::vector<Fraction> row(2 * d);
    for (std::size_t k = 0; k < d; ++k) {
      row[k] = Fraction(-distance[k]);
      row[d + k] = Fraction(distance[k]);
    }
    problem.a.push_back(row);  // -(pi . distance) <= -1
    problem.b.emplace_back(-1);
  }
  for (std::size_t k = 0; k < 2 * d; ++k) {
    std::vector<Fraction> row(2 * d);
    row[k] = Fraction(1);
    problem.a.push_back(row);
    problem.b.emplace_back(bound);
  }
  if (result.distances.empty()) {
    // No loop-carried dependence: the identity is already fully parallel.
    result.hyperplane.assign(d, 0);
    result.hyperplane[0] = 1;
  } else {
    result.ilp = solve_ilp(problem);
    if (result.ilp.status != LpStatus::Optimal) {
      result.reason = "no integer time vector with |pi_k| <= " + std::to_string(bound) + " carries every dependence (" +
                      lp_status_name(result.ilp.status) + ")";
      return result;
    }
    for (std::size_t k = 0; k < d; ++k) result.hyperplane.push_back(result.ilp.x[k] - result.ilp.x[d + k]);
  }
  const auto completion = unimodular_completion(result.hyperplane);
  if (!completion) {
    result.reason = "the time vector is not primitive";
    return result;
  }
  result.transform = *completion;
  std::set<std::int64_t> fronts;
  std::vector<std::int64_t> point(d, 0);
  enumerate(nest, 0, point, [&](const std::vector<std::int64_t>& p) {
    std::int64_t t = 0;
    for (std::size_t k = 0; k < d; ++k) t += result.hyperplane[k] * p[k];
    fronts.insert(t);
    ++result.instances;
  });
  result.wavefronts = static_cast<std::int64_t>(fronts.size());
  result.applied = apply_transformation(nest, analysis, result.transform);
  result.found = result.applied.legal;
  result.reason = result.applied.reason;
  return result;
}

std::string print_schedule(const LoopNest& nest, const ScheduleResult& result) {
  std::ostringstream out;
  auto vec = [](const std::vector<std::int64_t>& v) {
    std::string s = "(";
    for (std::size_t k = 0; k < v.size(); ++k) s += (k ? "," : "") + std::to_string(v[k]);
    return s + ")";
  };
  out << "hyperplane schedule by integer linear programming\n";
  out << "  constraints pi.d >= 1 for the loop-carried distances:";
  for (const auto& distance : result.distances) out << ' ' << vec(distance);
  out << '\n';
  if (!result.distances.empty()) {
    out << "  LP relaxation value " << result.ilp.relaxation.text() << ", integer optimum " << result.ilp.value.text()
        << " (" << result.ilp.nodes << " branch-and-bound nodes, " << result.ilp.pivots << " simplex pivots)\n";
  }
  if (result.hyperplane.empty()) {
    out << "  " << result.reason << '\n';
    return out.str();
  }
  out << "  time vector pi = " << vec(result.hyperplane) << ": " << result.wavefronts << " wavefronts for "
      << result.instances << " iterations\n";
  out << "  unimodular completion T =";
  for (const auto& row : result.transform) out << ' ' << vec(row);
  out << "  (det " << determinant(result.transform) << ")\n";
  out << "  " << result.reason << '\n';
  if (result.applied.legal) {
    out << "  parallel loops after the transformation:";
    for (std::size_t k = 0; k < result.applied.parallel_after.size(); ++k) {
      out << ' ' << (k + 1) << (result.applied.parallel_after[k] ? ":yes" : ":no");
    }
    out << "\n  verified by execution: " << (result.applied.verified ? "yes" : "NO") << '\n';
    out << result.applied.code;
  }
  (void)nest;
  return out.str();
}

}  // namespace nexus::compiler::polyhedral
