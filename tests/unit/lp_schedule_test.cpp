#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <numeric>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

#include "nexus/compiler/polyhedral/schedule.hpp"

namespace poly = nexus::compiler::polyhedral;
using poly::Fraction;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

poly::LoopNest load(const std::string& repo, const std::string& name) {
  std::ifstream input(repo + "/examples/loops/" + name);
  std::stringstream buffer;
  buffer << input.rdbuf();
  auto parsed = poly::parse_loop_nest(buffer.str());
  if (!parsed.nest) {
    std::cerr << name << ": " << parsed.error << '\n';
    std::exit(1);
  }
  return *parsed.nest;
}

void linear_programs() {
  // Textbook LP: max 3x + 5y, x <= 4, 2y <= 12, 3x + 2y <= 18 -> 36 at (2, 6).
  poly::LpProblem p;
  p.objective = {Fraction(3), Fraction(5)};
  p.a = {{Fraction(1), Fraction(0)}, {Fraction(0), Fraction(2)}, {Fraction(3), Fraction(2)}};
  p.b = {Fraction(4), Fraction(12), Fraction(18)};
  const auto s = poly::solve_lp(p);
  expect(s.status == poly::LpStatus::Optimal && s.value == Fraction(36) && s.x[0] == Fraction(2) && s.x[1] == Fraction(6),
         "textbook LP optimum 36 at (2,6)");
  // Fractional optimum: max x + y, 2x + 2y <= 3 -> 3/2.
  poly::LpProblem half;
  half.objective = {Fraction(1), Fraction(1)};
  half.a = {{Fraction(2), Fraction(2)}};
  half.b = {Fraction(3)};
  expect(poly::solve_lp(half).value == Fraction(3, 2), "fractional optimum 3/2");
  expect(poly::solve_ilp(half).value == Fraction(1), "its integer optimum is 1");
  // Infeasible: x >= 2 and x <= 1.
  poly::LpProblem bad;
  bad.objective = {Fraction(1)};
  bad.a = {{Fraction(-1)}, {Fraction(1)}};
  bad.b = {Fraction(-2), Fraction(1)};
  expect(poly::solve_lp(bad).status == poly::LpStatus::Infeasible, "infeasible LP detected");
  // Unbounded: max x with x >= 1.
  poly::LpProblem open;
  open.objective = {Fraction(1)};
  open.a = {{Fraction(-1)}};
  open.b = {Fraction(-1)};
  expect(poly::solve_lp(open).status == poly::LpStatus::Unbounded, "unbounded LP detected");

  // Random bounded integer programs against exhaustive search.
  std::mt19937 rng(3);
  for (int round = 0; round < 150; ++round) {
    const std::size_t n = 2 + rng() % 2;
    poly::LpProblem ip;
    for (std::size_t j = 0; j < n; ++j) ip.objective.emplace_back(static_cast<std::int64_t>(rng() % 9) - 3);
    const std::size_t m = 1 + rng() % 3;
    for (std::size_t i = 0; i < m; ++i) {
      std::vector<Fraction> row;
      for (std::size_t j = 0; j < n; ++j) row.emplace_back(static_cast<std::int64_t>(rng() % 7) - 2);
      ip.a.push_back(row);
      ip.b.emplace_back(static_cast<std::int64_t>(rng() % 15) - 3);
    }
    for (std::size_t j = 0; j < n; ++j) {
      std::vector<Fraction> row(n);
      row[j] = Fraction(1);
      ip.a.push_back(row);
      ip.b.emplace_back(5);
    }
    const auto solved = poly::solve_ilp(ip);
    bool feasible = false;
    std::int64_t best = 0;
    std::vector<std::int64_t> x(n, 0);
    std::function<void(std::size_t)> search = [&](std::size_t j) {
      if (j == n) {
        for (std::size_t i = 0; i < ip.a.size(); ++i) {
          std::int64_t lhs = 0;
          for (std::size_t k = 0; k < n; ++k) lhs += ip.a[i][k].num * x[k];
          if (lhs > ip.b[i].num) return;
        }
        std::int64_t value = 0;
        for (std::size_t k = 0; k < n; ++k) value += ip.objective[k].num * x[k];
        if (!feasible || value > best) best = value;
        feasible = true;
        return;
      }
      for (std::int64_t v = 0; v <= 5; ++v) {
        x[j] = v;
        search(j + 1);
      }
    };
    search(0);
    expect(feasible == (solved.status == poly::LpStatus::Optimal), "ILP feasibility agrees with exhaustive search");
    if (feasible && solved.status == poly::LpStatus::Optimal) {
      expect(solved.value == Fraction(best), "ILP optimum equals the exhaustive optimum");
    }
  }
}

void completion() {
  std::mt19937 rng(9);
  for (int round = 0; round < 300; ++round) {
    const std::size_t d = 2 + rng() % 3;
    std::vector<std::int64_t> row(d);
    for (auto& v : row) v = static_cast<std::int64_t>(rng() % 13) - 6;
    std::int64_t g = 0;
    for (const auto v : row) g = std::gcd(g, std::llabs(v));
    const auto t = poly::unimodular_completion(row);
    expect(t.has_value() == (g == 1), "completion exists exactly for primitive rows");
    if (t) {
      expect((*t)[0] == row, "first row is the given vector");
      expect(std::llabs(poly::determinant(*t)) == 1, "the completion is unimodular");
    }
  }
}

void schedules(const std::string& repo) {
  const auto wavefront = load(repo, "wavefront.loop");
  const auto wave = poly::find_hyperplane_schedule(wavefront, poly::analyze_dependences(wavefront));
  expect(wave.found && wave.hyperplane == std::vector<std::int64_t>{1, 1}, "wavefront: pi = (1,1)");
  expect(wave.applied.verified && !wave.applied.parallel_after[0] && wave.applied.parallel_after[1],
         "wavefront: inner loop parallel after the LP schedule, verified by execution");
  expect(wave.wavefronts == 6 + 7 - 1, "wavefront: N + M - 1 wavefronts");

  const auto matmul = load(repo, "matmul.loop");
  const auto mm = poly::find_hyperplane_schedule(matmul, poly::analyze_dependences(matmul));
  expect(mm.found && mm.hyperplane == std::vector<std::int64_t>{0, 0, 1}, "matmul: pi = (0,0,1), k outermost");
  expect(mm.applied.verified && mm.applied.parallel_after[1] && mm.applied.parallel_after[2],
         "matmul: i and j parallel inside");

  for (const auto* name : {"interchange_illegal.loop", "triangular.loop"}) {
    const auto nest = load(repo, name);
    const auto s = poly::find_hyperplane_schedule(nest, poly::analyze_dependences(nest));
    expect(s.found && s.applied.verified, std::string(name) + ": legal schedule found and verified");
    for (std::size_t k = 1; k < s.applied.parallel_after.size(); ++k) {
      expect(s.applied.parallel_after[k], std::string(name) + ": every inner loop parallel");
    }
  }

  // ILP optimum against brute force over small integer vectors, for random distance sets.
  std::mt19937 rng(21);
  for (int round = 0; round < 120; ++round) {
    const std::size_t d = 2 + rng() % 2;
    std::vector<std::vector<std::int64_t>> distances;
    const std::size_t count = 1 + rng() % 3;
    while (distances.size() < count) {
      std::vector<std::int64_t> v(d);
      for (auto& x : v) x = static_cast<std::int64_t>(rng() % 5) - 2;
      // lexicographically positive, as dependence distances are
      const auto first = std::find_if(v.begin(), v.end(), [](std::int64_t x) { return x != 0; });
      if (first != v.end() && *first > 0) distances.push_back(v);
    }
    poly::LpProblem problem;
    problem.objective.assign(2 * d, Fraction(-1));
    for (const auto& distance : distances) {
      std::vector<Fraction> row(2 * d);
      for (std::size_t k = 0; k < d; ++k) {
        row[k] = Fraction(-distance[k]);
        row[d + k] = Fraction(distance[k]);
      }
      problem.a.push_back(row);
      problem.b.emplace_back(-1);
    }
    for (std::size_t k = 0; k < 2 * d; ++k) {
      std::vector<Fraction> row(2 * d);
      row[k] = Fraction(1);
      problem.a.push_back(row);
      problem.b.emplace_back(4);
    }
    const auto ilp = poly::solve_ilp(problem);
    std::int64_t best = -1;
    std::vector<std::int64_t> pi(d);
    std::function<void(std::size_t)> search = [&](std::size_t k) {
      if (k == d) {
        for (const auto& distance : distances) {
          std::int64_t dot = 0;
          for (std::size_t j = 0; j < d; ++j) dot += pi[j] * distance[j];
          if (dot < 1) return;
        }
        std::int64_t norm = 0;
        for (const auto v : pi) norm += std::llabs(v);
        if (best < 0 || norm < best) best = norm;
        return;
      }
      for (std::int64_t v = -4; v <= 4; ++v) {
        pi[k] = v;
        search(k + 1);
      }
    };
    search(0);
    expect(best >= 0 && ilp.status == poly::LpStatus::Optimal && ilp.value == Fraction(-best),
           "minimal sum |pi| from the ILP equals brute force");
  }
}

}  // namespace

int main(int argc, char** argv) {
  const std::string repo = argc > 1 ? argv[1] : ".";
  linear_programs();
  completion();
  schedules(repo);
  if (failures == 0) {
    std::cout << "linear programming: simplex, branch and bound and hyperplane schedules OK\n";
  }
  return failures == 0 ? 0 : 1;
}
