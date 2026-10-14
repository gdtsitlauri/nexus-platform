#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "nexus/logic/boolean.hpp"

// A conflict-driven clause-learning SAT solver (two watched literals, first-UIP learning, activity-based
// decisions with phase saving, geometric restarts) and the Tseitin encoding of Boolean expressions. Used for CNF
// (EY121) and for SAT-based equivalence checking of synthesised circuits (NEY705).
namespace nexus::logic {

using Clause = std::vector<int>;  // DIMACS literals: +v / -v for variable v >= 1

class SatSolver {
 public:
  enum class Result { Sat, Unsat };

  int new_var();
  int num_vars() const { return static_cast<int>(assign_.size()); }
  void add_clause(Clause clause);
  Result solve();
  bool value(int var) const;  // model after Sat (variable >= 1)

  std::size_t conflicts() const { return conflicts_; }
  std::size_t decisions() const { return decisions_; }
  std::size_t propagations() const { return propagations_; }
  std::size_t learnt_clauses() const { return learnt_count_; }

 private:
  using Lit = std::uint32_t;  // 2*var + negated
  static Lit to_lit(int dimacs);
  static Lit neg(Lit l) { return l ^ 1U; }
  static std::size_t var_of(Lit l) { return l >> 1U; }
  int lit_value(Lit l) const;  // 1 true, 0 false, -1 unassigned
  void ensure_var(std::size_t v);
  void enqueue(Lit l, int reason);
  int propagate();  // returns a conflicting clause index or -1
  void analyze(int conflict, std::vector<Lit>& learnt, std::size_t& back_level);
  void backtrack(std::size_t level);
  void bump(std::size_t v);
  std::size_t attach(std::vector<Lit> lits);

  std::vector<std::vector<Lit>> clauses_;
  std::vector<std::vector<std::size_t>> watches_;  // per literal: clauses watching it
  std::vector<int> assign_;                         // per variable
  std::vector<std::size_t> level_;
  std::vector<int> reason_;
  std::vector<bool> phase_;
  std::vector<double> activity_;
  std::vector<Lit> trail_;
  std::vector<std::size_t> trail_lim_;
  std::size_t qhead_ = 0;
  double bump_amount_ = 1.0;
  bool unsat_ = false;
  std::vector<Lit> pending_units_;
  std::size_t conflicts_ = 0;
  std::size_t decisions_ = 0;
  std::size_t propagations_ = 0;
  std::size_t learnt_count_ = 0;
};

// Tseitin encoding: one variable per expression variable (1..n, in order), fresh variables for the gates.
struct Tseitin {
  std::vector<Clause> clauses;
  int output = 0;     // literal equivalent to the expression
  int num_vars = 0;   // highest variable used
};
Tseitin tseitin(const Expr& e, std::size_t num_input_vars);

std::string to_dimacs(const std::vector<Clause>& clauses, int num_vars);
std::vector<Clause> parse_dimacs(const std::string& text, int& num_vars);

// Brute-force satisfiability for testing (num_vars <= 24).
bool brute_force_sat(const std::vector<Clause>& clauses, int num_vars);

struct EquivalenceResult {
  bool equivalent = false;
  std::optional<std::uint64_t> counterexample;  // a minterm on which the two differ
  std::size_t conflicts = 0;
};
// SAT miter: f XOR g is unsatisfiable exactly when f == g. Both expressions use the same n input variables.
EquivalenceResult sat_equivalent(const Expr& f, const Expr& g, std::size_t num_input_vars);

}  // namespace nexus::logic
