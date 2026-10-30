#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace nexus::compiler::foundations {

// The data structures and the classic traversal and closure algorithms that a compiler is built on
// (NEY613, part (a) of the outline): hash tables (the symbol table), trees (the syntax tree) and graphs
// (control flow, dependences, automata), with traversals and closures.

// ---- hash table ------------------------------------------------------------------------------------
// Separate chaining with a polynomial string hash; the table doubles when the load factor exceeds
// `max_load`.  Statistics expose what a symbol table pays for each lookup.
class HashTable {
 public:
  explicit HashTable(std::size_t buckets = 8, double max_load = 0.75);

  bool insert(const std::string& key, std::int64_t value);  // false when the key exists (value updated)
  [[nodiscard]] std::optional<std::int64_t> find(const std::string& key) const;
  bool erase(const std::string& key);

  [[nodiscard]] std::size_t size() const { return size_; }
  [[nodiscard]] std::size_t bucket_count() const { return buckets_.size(); }
  [[nodiscard]] double load_factor() const;
  [[nodiscard]] std::size_t longest_chain() const;
  [[nodiscard]] std::size_t rehashes() const { return rehashes_; }
  [[nodiscard]] std::size_t probes() const { return probes_; }  // key comparisons over all operations
  [[nodiscard]] std::vector<std::pair<std::string, std::int64_t>> items() const;

  static std::uint64_t hash(const std::string& key);

 private:
  void rehash(std::size_t buckets);
  std::vector<std::vector<std::pair<std::string, std::int64_t>>> buckets_;
  std::size_t size_ = 0;
  double max_load_;
  std::size_t rehashes_ = 0;
  mutable std::size_t probes_ = 0;
};

// Block-structured symbol table: a stack of hash tables, one per open scope.
class ScopedSymbolTable {
 public:
  ScopedSymbolTable();
  void enter_scope();
  void exit_scope();
  bool declare(const std::string& name, std::int64_t info);  // false on redeclaration in the same scope
  [[nodiscard]] std::optional<std::pair<std::int64_t, std::size_t>> lookup(const std::string& name) const;  // info, depth
  [[nodiscard]] std::size_t depth() const { return scopes_.size(); }

 private:
  std::vector<HashTable> scopes_;
};

// ---- trees -------------------------------------------------------------------------------------------
struct Tree {
  std::vector<std::string> label;
  std::vector<std::vector<std::size_t>> children;  // node 0 is the root
  std::size_t add(const std::string& text, std::optional<std::size_t> parent = std::nullopt);
};

// Parses "a(b(d e) c)" into a tree.
std::optional<Tree> parse_tree(const std::string& text);
std::vector<std::string> preorder(const Tree& tree);
std::vector<std::string> postorder(const Tree& tree);
std::vector<std::string> inorder(const Tree& tree);   // binary view: first child, node, remaining children
std::vector<std::string> level_order(const Tree& tree);
std::size_t height(const Tree& tree);

// ---- graphs -----------------------------------------------------------------------------------------
struct Graph {
  std::vector<std::string> name;
  std::vector<std::vector<std::size_t>> succ;
  std::size_t node(const std::string& text);  // finds or adds
  void edge(const std::string& from, const std::string& to);
};

// Parses lines "a -> b c" (a node list after the arrow); '#' starts a comment.
std::optional<Graph> parse_graph(const std::string& text, std::string* error = nullptr);

enum class EdgeKind { Tree, Back, Forward, Cross };
struct DepthFirst {
  std::vector<std::size_t> preorder;
  std::vector<std::size_t> postorder;
  std::vector<std::pair<std::pair<std::size_t, std::size_t>, EdgeKind>> edges;
  [[nodiscard]] bool acyclic() const;
};
DepthFirst depth_first(const Graph& graph, std::size_t root = 0);       // from root, then remaining nodes
std::vector<std::size_t> breadth_first(const Graph& graph, std::size_t root = 0);
std::optional<std::vector<std::size_t>> topological_order(const Graph& graph);  // Kahn; nullopt on a cycle
std::vector<std::vector<std::size_t>> strongly_connected_components(const Graph& graph);  // Tarjan
std::vector<std::vector<bool>> transitive_closure(const Graph& graph);   // Warshall (reflexive)

// Generic closure: the least set containing `seed` and closed under `step` (worklist algorithm),
// the shape of epsilon-closure, LR(0) item closure and reachability.
std::vector<std::size_t> closure(std::vector<std::size_t> seed,
                                 const std::function<std::vector<std::size_t>(std::size_t)>& step);

std::string edge_kind_name(EdgeKind kind);
std::string report(const Graph& graph);

}  // namespace nexus::compiler::foundations
