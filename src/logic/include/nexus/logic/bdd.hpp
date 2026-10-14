#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "nexus/logic/boolean.hpp"

// Reduced ordered binary decision diagrams (EY121 representations; NEY705 equivalence checking).
// Nodes are shared in one manager: two functions are equal exactly when their node ids are equal.
namespace nexus::logic {

class Bdd {
 public:
  using Node = std::uint32_t;
  static constexpr Node kFalse = 0;
  static constexpr Node kTrue = 1;

  // `order[k]` is the variable index tested at level k (default: 0, 1, 2, ...).
  explicit Bdd(std::size_t num_vars, std::vector<std::size_t> order = {});

  Node var(std::size_t index);
  Node not_(Node f);
  Node and_(Node f, Node g);
  Node or_(Node f, Node g);
  Node xor_(Node f, Node g);
  Node ite(Node f, Node g, Node h);
  Node from_expr(const Expr& e);

  bool evaluate(Node f, std::uint64_t minterm) const;  // minterm numbering as in boolean.hpp
  double sat_count(Node f) const;                       // number of satisfying assignments of all variables
  std::size_t size(Node f) const;                       // internal nodes reachable from f
  std::size_t total_nodes() const { return nodes_.size(); }
  std::string to_dot(Node f, const std::vector<std::string>& names) const;

 private:
  struct Entry {
    std::size_t level;
    Node lo;
    Node hi;
  };
  Node make(std::size_t level, Node lo, Node hi);
  std::size_t level_of(Node f) const { return nodes_[f].level; }

  std::size_t num_vars_;
  std::vector<std::size_t> order_;     // level -> variable
  std::vector<std::size_t> level_of_;  // variable -> level
  std::vector<Entry> nodes_;
  std::map<std::tuple<std::size_t, Node, Node>, Node> unique_;
  std::map<std::tuple<Node, Node, Node>, Node> computed_;
};

}  // namespace nexus::logic
