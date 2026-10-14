#include "nexus/logic/sat.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <stdexcept>

namespace nexus::logic {

SatSolver::Lit SatSolver::to_lit(int dimacs) {
  if (dimacs == 0) {
    throw std::runtime_error("sat: literal 0");
  }
  const auto v = static_cast<std::uint32_t>(std::abs(dimacs) - 1);
  return (v << 1U) | (dimacs < 0 ? 1U : 0U);
}

int SatSolver::new_var() {
  ensure_var(assign_.size());
  return static_cast<int>(assign_.size());
}

void SatSolver::ensure_var(std::size_t v) {
  while (assign_.size() <= v) {
    assign_.push_back(-1);
    level_.push_back(0);
    reason_.push_back(-1);
    phase_.push_back(false);
    activity_.push_back(0.0);
    watches_.emplace_back();
    watches_.emplace_back();
  }
}

int SatSolver::lit_value(Lit l) const {
  const int a = assign_[var_of(l)];
  if (a < 0) {
    return -1;
  }
  return (a == 1) != ((l & 1U) != 0) ? 1 : 0;
}

std::size_t SatSolver::attach(std::vector<Lit> lits) {
  const std::size_t idx = clauses_.size();
  watches_[lits[0]].push_back(idx);
  watches_[lits[1]].push_back(idx);
  clauses_.push_back(std::move(lits));
  return idx;
}

void SatSolver::add_clause(Clause clause) {
  std::vector<Lit> lits;
  for (const int d : clause) {
    const Lit l = to_lit(d);
    ensure_var(var_of(l));
    lits.push_back(l);
  }
  std::sort(lits.begin(), lits.end());
  lits.erase(std::unique(lits.begin(), lits.end()), lits.end());
  for (std::size_t i = 0; i + 1 < lits.size(); ++i) {
    if (lits[i] == neg(lits[i + 1])) {
      return;  // tautology
    }
  }
  if (lits.empty()) {
    unsat_ = true;
  } else if (lits.size() == 1) {
    pending_units_.push_back(lits[0]);
  } else {
    attach(std::move(lits));
  }
}

void SatSolver::enqueue(Lit l, int reason) {
  const std::size_t v = var_of(l);
  assign_[v] = (l & 1U) != 0 ? 0 : 1;
  level_[v] = trail_lim_.size();
  reason_[v] = reason;
  trail_.push_back(l);
}

int SatSolver::propagate() {
  while (qhead_ < trail_.size()) {
    const Lit p = trail_[qhead_++];
    const Lit false_lit = neg(p);
    auto& ws = watches_[false_lit];
    std::size_t keep = 0;
    for (std::size_t i = 0; i < ws.size(); ++i) {
      const std::size_t ci = ws[i];
      auto& c = clauses_[ci];
      if (c[0] == false_lit) {
        std::swap(c[0], c[1]);
      }
      if (lit_value(c[0]) == 1) {
        ws[keep++] = ci;
        continue;
      }
      bool moved = false;
      for (std::size_t k = 2; k < c.size(); ++k) {
        if (lit_value(c[k]) != 0) {
          std::swap(c[1], c[k]);
          watches_[c[1]].push_back(ci);
          moved = true;
          break;
        }
      }
      if (moved) {
        continue;
      }
      ws[keep++] = ci;
      if (lit_value(c[0]) == 0) {
        for (std::size_t j = i + 1; j < ws.size(); ++j) {
          ws[keep++] = ws[j];
        }
        ws.resize(keep);
        qhead_ = trail_.size();
        return static_cast<int>(ci);
      }
      ++propagations_;
      enqueue(c[0], static_cast<int>(ci));
    }
    ws.resize(keep);
  }
  return -1;
}

void SatSolver::bump(std::size_t v) {
  activity_[v] += bump_amount_;
  if (activity_[v] > 1e100) {
    for (auto& a : activity_) {
      a *= 1e-100;
    }
    bump_amount_ *= 1e-100;
  }
}

void SatSolver::analyze(int conflict, std::vector<Lit>& learnt, std::size_t& back_level) {
  learnt.clear();
  learnt.push_back(0);  // placeholder for the asserting literal
  std::vector<bool> seen(assign_.size(), false);
  std::size_t counter = 0;
  Lit p = 0;
  bool have_p = false;
  std::size_t index = trail_.size();
  int clause = conflict;
  const std::size_t current = trail_lim_.size();
  do {
    const auto& c = clauses_[static_cast<std::size_t>(clause)];
    for (std::size_t j = have_p ? 1 : 0; j < c.size(); ++j) {
      const Lit q = c[j];
      const std::size_t v = var_of(q);
      if (seen[v] || level_[v] == 0) {
        continue;
      }
      seen[v] = true;
      bump(v);
      if (level_[v] >= current) {
        ++counter;
      } else {
        learnt.push_back(q);
      }
    }
    // next literal of the current level on the trail
    do {
      --index;
    } while (!seen[var_of(trail_[index])]);
    p = trail_[index];
    have_p = true;
    clause = reason_[var_of(p)];
    seen[var_of(p)] = false;
    --counter;
    if (counter > 0 && clause >= 0) {
      // the reason clause has p as its first literal after propagation
      auto& rc = clauses_[static_cast<std::size_t>(clause)];
      const auto it = std::find(rc.begin(), rc.end(), p);
      std::iter_swap(rc.begin(), it);
    }
  } while (counter > 0);
  learnt[0] = neg(p);
  back_level = 0;
  std::size_t max_i = 1;
  for (std::size_t i = 1; i < learnt.size(); ++i) {
    if (level_[var_of(learnt[i])] > back_level) {
      back_level = level_[var_of(learnt[i])];
      max_i = i;
    }
  }
  if (learnt.size() > 1) {
    std::swap(learnt[1], learnt[max_i]);
  }
  bump_amount_ /= 0.95;
}

void SatSolver::backtrack(std::size_t level) {
  if (trail_lim_.size() <= level) {
    return;
  }
  for (std::size_t i = trail_.size(); i > trail_lim_[level]; --i) {
    const std::size_t v = var_of(trail_[i - 1]);
    phase_[v] = assign_[v] == 1;
    assign_[v] = -1;
    reason_[v] = -1;
  }
  trail_.resize(trail_lim_[level]);
  trail_lim_.resize(level);
  qhead_ = trail_.size();
}

SatSolver::Result SatSolver::solve() {
  if (unsat_) {
    return Result::Unsat;
  }
  backtrack(0);
  for (const Lit u : pending_units_) {
    const int val = lit_value(u);
    if (val == 0) {
      return Result::Unsat;
    }
    if (val < 0) {
      enqueue(u, -1);
    }
  }
  if (propagate() >= 0) {
    return Result::Unsat;
  }
  std::size_t restart_limit = 100;
  std::size_t since_restart = 0;
  std::vector<Lit> learnt;
  for (;;) {
    const int conflict = propagate();
    if (conflict >= 0) {
      ++conflicts_;
      ++since_restart;
      if (trail_lim_.empty()) {
        return Result::Unsat;
      }
      std::size_t back_level = 0;
      analyze(conflict, learnt, back_level);
      backtrack(back_level);
      if (learnt.size() == 1) {
        enqueue(learnt[0], -1);
      } else {
        const std::size_t ci = attach(learnt);
        ++learnt_count_;
        enqueue(learnt[0], static_cast<int>(ci));
      }
      continue;
    }
    if (since_restart >= restart_limit) {
      since_restart = 0;
      restart_limit += restart_limit / 2;
      backtrack(0);
      continue;
    }
    // decision: most active unassigned variable, saved phase
    std::size_t best = assign_.size();
    for (std::size_t v = 0; v < assign_.size(); ++v) {
      if (assign_[v] < 0 && (best == assign_.size() || activity_[v] > activity_[best])) {
        best = v;
      }
    }
    if (best == assign_.size()) {
      return Result::Sat;
    }
    ++decisions_;
    trail_lim_.push_back(trail_.size());
    const Lit l = static_cast<Lit>((best << 1U) | (phase_[best] ? 0U : 1U));
    enqueue(l, -1);
  }
}

bool SatSolver::value(int var) const {
  const auto v = static_cast<std::size_t>(var - 1);
  return v < assign_.size() && assign_[v] == 1;
}

Tseitin tseitin(const Expr& e, std::size_t num_input_vars) {
  Tseitin t;
  t.num_vars = static_cast<int>(num_input_vars);
  auto fresh = [&]() { return ++t.num_vars; };
  std::function<int(const Expr&)> enc = [&](const Expr& x) -> int {
    switch (x.kind) {
      case Expr::Kind::Var:
        return static_cast<int>(x.var) + 1;
      case Expr::Kind::Const: {
        const int c = fresh();
        t.clauses.push_back({x.value ? c : -c});
        return c;
      }
      case Expr::Kind::Not:
        return -enc(*x.kids[0]);
      case Expr::Kind::And: {
        const int a = enc(*x.kids[0]);
        const int b = enc(*x.kids[1]);
        const int g = fresh();
        t.clauses.push_back({-g, a});
        t.clauses.push_back({-g, b});
        t.clauses.push_back({g, -a, -b});
        return g;
      }
      case Expr::Kind::Or: {
        const int a = enc(*x.kids[0]);
        const int b = enc(*x.kids[1]);
        const int g = fresh();
        t.clauses.push_back({g, -a});
        t.clauses.push_back({g, -b});
        t.clauses.push_back({-g, a, b});
        return g;
      }
      case Expr::Kind::Xor: {
        const int a = enc(*x.kids[0]);
        const int b = enc(*x.kids[1]);
        const int g = fresh();
        t.clauses.push_back({-g, a, b});
        t.clauses.push_back({-g, -a, -b});
        t.clauses.push_back({g, -a, b});
        t.clauses.push_back({g, a, -b});
        return g;
      }
    }
    return 0;
  };
  t.output = enc(e);
  return t;
}

std::string to_dimacs(const std::vector<Clause>& clauses, int num_vars) {
  std::ostringstream out;
  out << "p cnf " << num_vars << ' ' << clauses.size() << '\n';
  for (const auto& c : clauses) {
    for (const int l : c) {
      out << l << ' ';
    }
    out << "0\n";
  }
  return out.str();
}

std::vector<Clause> parse_dimacs(const std::string& text, int& num_vars) {
  std::istringstream in(text);
  std::string token;
  std::vector<Clause> clauses;
  Clause current;
  num_vars = 0;
  while (in >> token) {
    if (token == "c") {
      std::string rest;
      std::getline(in, rest);
      continue;
    }
    if (token == "p") {
      std::string fmt;
      std::size_t count = 0;
      in >> fmt >> num_vars >> count;
      continue;
    }
    const int l = std::stoi(token);
    if (l == 0) {
      clauses.push_back(current);
      current.clear();
    } else {
      current.push_back(l);
      num_vars = std::max(num_vars, std::abs(l));
    }
  }
  return clauses;
}

bool brute_force_sat(const std::vector<Clause>& clauses, int num_vars) {
  if (num_vars > 24) {
    throw std::runtime_error("brute_force_sat: too many variables");
  }
  for (std::uint64_t a = 0; a < (std::uint64_t{1} << num_vars); ++a) {
    bool ok = true;
    for (const auto& c : clauses) {
      bool sat = false;
      for (const int l : c) {
        const bool v = ((a >> (std::abs(l) - 1)) & 1U) != 0;
        if ((l > 0) == v) {
          sat = true;
          break;
        }
      }
      if (!sat) {
        ok = false;
        break;
      }
    }
    if (ok) {
      return true;
    }
  }
  return false;
}

EquivalenceResult sat_equivalent(const Expr& f, const Expr& g, std::size_t num_input_vars) {
  const ExprPtr fp = std::make_shared<Expr>(f);
  const ExprPtr gp = std::make_shared<Expr>(g);
  const ExprPtr miter = make_xor(fp, gp);
  const Tseitin t = tseitin(*miter, num_input_vars);
  SatSolver solver;
  for (int v = 0; v < t.num_vars; ++v) {
    solver.new_var();
  }
  for (const auto& c : t.clauses) {
    solver.add_clause(c);
  }
  solver.add_clause({t.output});
  EquivalenceResult r;
  const auto result = solver.solve();
  r.conflicts = solver.conflicts();
  r.equivalent = result == SatSolver::Result::Unsat;
  if (!r.equivalent) {
    std::uint64_t m = 0;
    for (std::size_t i = 0; i < num_input_vars; ++i) {
      if (solver.value(static_cast<int>(i) + 1)) {
        m |= std::uint64_t{1} << (num_input_vars - 1 - i);
      }
    }
    r.counterexample = m;
  }
  return r;
}

}  // namespace nexus::logic
