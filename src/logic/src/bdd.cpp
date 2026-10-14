#include "nexus/logic/bdd.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <sstream>
#include <stdexcept>

namespace nexus::logic {

Bdd::Bdd(std::size_t num_vars, std::vector<std::size_t> order) : num_vars_(num_vars), order_(std::move(order)) {
  if (order_.empty()) {
    for (std::size_t i = 0; i < num_vars_; ++i) {
      order_.push_back(i);
    }
  }
  if (order_.size() != num_vars_) {
    throw std::runtime_error("bdd: order must list every variable once");
  }
  level_of_.assign(num_vars_, 0);
  std::vector<bool> seen(num_vars_, false);
  for (std::size_t level = 0; level < order_.size(); ++level) {
    if (order_[level] >= num_vars_ || seen[order_[level]]) {
      throw std::runtime_error("bdd: invalid order");
    }
    seen[order_[level]] = true;
    level_of_[order_[level]] = level;
  }
  // terminals sit below every variable level
  nodes_.push_back(Entry{num_vars_, kFalse, kFalse});
  nodes_.push_back(Entry{num_vars_, kTrue, kTrue});
}

Bdd::Node Bdd::make(std::size_t level, Node lo, Node hi) {
  if (lo == hi) {
    return lo;  // reduction rule 1: redundant test
  }
  const auto key = std::make_tuple(level, lo, hi);
  const auto it = unique_.find(key);
  if (it != unique_.end()) {
    return it->second;  // reduction rule 2: share isomorphic subgraphs
  }
  const auto id = static_cast<Node>(nodes_.size());
  nodes_.push_back(Entry{level, lo, hi});
  unique_.emplace(key, id);
  return id;
}

Bdd::Node Bdd::var(std::size_t index) {
  if (index >= num_vars_) {
    throw std::runtime_error("bdd: variable out of range");
  }
  return make(level_of_[index], kFalse, kTrue);
}

Bdd::Node Bdd::ite(Node f, Node g, Node h) {
  if (f == kTrue) {
    return g;
  }
  if (f == kFalse) {
    return h;
  }
  if (g == h) {
    return g;
  }
  if (g == kTrue && h == kFalse) {
    return f;
  }
  const auto key = std::make_tuple(f, g, h);
  const auto it = computed_.find(key);
  if (it != computed_.end()) {
    return it->second;
  }
  const std::size_t top = std::min({level_of(f), level_of(g), level_of(h)});
  auto cofactor = [&](Node x, bool high) -> Node {
    if (level_of(x) != top) {
      return x;
    }
    return high ? nodes_[x].hi : nodes_[x].lo;
  };
  const Node hi = ite(cofactor(f, true), cofactor(g, true), cofactor(h, true));
  const Node lo = ite(cofactor(f, false), cofactor(g, false), cofactor(h, false));
  const Node result = make(top, lo, hi);
  computed_.emplace(key, result);
  return result;
}

Bdd::Node Bdd::not_(Node f) { return ite(f, kFalse, kTrue); }
Bdd::Node Bdd::and_(Node f, Node g) { return ite(f, g, kFalse); }
Bdd::Node Bdd::or_(Node f, Node g) { return ite(f, kTrue, g); }
Bdd::Node Bdd::xor_(Node f, Node g) { return ite(f, not_(g), g); }

Bdd::Node Bdd::from_expr(const Expr& e) {
  switch (e.kind) {
    case Expr::Kind::Const:
      return e.value ? kTrue : kFalse;
    case Expr::Kind::Var:
      return var(e.var);
    case Expr::Kind::Not:
      return not_(from_expr(*e.kids[0]));
    case Expr::Kind::And:
      return and_(from_expr(*e.kids[0]), from_expr(*e.kids[1]));
    case Expr::Kind::Or:
      return or_(from_expr(*e.kids[0]), from_expr(*e.kids[1]));
    case Expr::Kind::Xor:
      return xor_(from_expr(*e.kids[0]), from_expr(*e.kids[1]));
  }
  return kFalse;
}

bool Bdd::evaluate(Node f, std::uint64_t minterm) const {
  while (f > kTrue) {
    const std::size_t v = order_[nodes_[f].level];
    const bool bit = ((minterm >> (num_vars_ - 1 - v)) & 1U) != 0;
    f = bit ? nodes_[f].hi : nodes_[f].lo;
  }
  return f == kTrue;
}

double Bdd::sat_count(Node f) const {
  std::map<Node, double> memo;
  // fraction of assignments that satisfy the sub-function; multiply by 2^n at the end
  std::function<double(Node)> frac = [&](Node x) -> double {
    if (x == kFalse) {
      return 0.0;
    }
    if (x == kTrue) {
      return 1.0;
    }
    const auto it = memo.find(x);
    if (it != memo.end()) {
      return it->second;
    }
    const double v = 0.5 * frac(nodes_[x].lo) + 0.5 * frac(nodes_[x].hi);
    memo.emplace(x, v);
    return v;
  };
  return frac(f) * std::ldexp(1.0, static_cast<int>(num_vars_));
}

std::size_t Bdd::size(Node f) const {
  std::set<Node> seen;
  std::function<void(Node)> walk = [&](Node x) {
    if (x <= kTrue || !seen.insert(x).second) {
      return;
    }
    walk(nodes_[x].lo);
    walk(nodes_[x].hi);
  };
  walk(f);
  return seen.size();
}

std::string Bdd::to_dot(Node f, const std::vector<std::string>& names) const {
  std::ostringstream out;
  out << "digraph bdd {\n  0 [shape=box,label=\"0\"];\n  1 [shape=box,label=\"1\"];\n";
  std::set<Node> seen;
  std::function<void(Node)> walk = [&](Node x) {
    if (x <= kTrue || !seen.insert(x).second) {
      return;
    }
    const std::size_t v = order_[nodes_[x].level];
    out << "  " << x << " [label=\"" << (v < names.size() ? names[v] : "x" + std::to_string(v)) << "\"];\n";
    out << "  " << x << " -> " << nodes_[x].lo << " [style=dashed];\n";
    out << "  " << x << " -> " << nodes_[x].hi << ";\n";
    walk(nodes_[x].lo);
    walk(nodes_[x].hi);
  };
  walk(f);
  out << "}\n";
  return out.str();
}

}  // namespace nexus::logic
