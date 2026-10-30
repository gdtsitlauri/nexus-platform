#include <algorithm>
#include <functional>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "nexus/compiler/foundations/foundations.hpp"

namespace fd = nexus::compiler::foundations;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::vector<std::string> names(const fd::Graph& g, const std::vector<std::size_t>& ids) {
  std::vector<std::string> out;
  for (const auto id : ids) out.push_back(g.name[id]);
  return out;
}

void hash_table() {
  // Random operations against std::unordered_map.
  std::mt19937 rng(7);
  fd::HashTable table(2);
  std::unordered_map<std::string, std::int64_t> reference;
  for (int step = 0; step < 20000; ++step) {
    const std::string key = "k" + std::to_string(rng() % 3000);
    switch (rng() % 3) {
      case 0: {
        const auto value = static_cast<std::int64_t>(rng() % 100000);
        const bool fresh = table.insert(key, value);
        expect(fresh == (reference.count(key) == 0), "insert reports a new key correctly");
        reference[key] = value;
        break;
      }
      case 1: {
        const auto found = table.find(key);
        const auto it = reference.find(key);
        expect(found.has_value() == (it != reference.end()) && (!found || *found == it->second), "find agrees");
        break;
      }
      default:
        expect(table.erase(key) == (reference.erase(key) == 1), "erase agrees");
    }
  }
  expect(table.size() == reference.size(), "size agrees with std::unordered_map");
  expect(table.load_factor() <= 0.75 + 1e-9, "load factor kept below the threshold");
  expect(table.rehashes() > 0, "the table grew by rehashing");

  fd::ScopedSymbolTable scopes;
  expect(scopes.declare("x", 1), "declare x in the global scope");
  scopes.enter_scope();
  expect(scopes.declare("x", 2) && !scopes.declare("x", 3), "shadowing allowed, redeclaration rejected");
  expect(scopes.lookup("x")->first == 2 && scopes.lookup("x")->second == 1, "inner x found at depth 1");
  scopes.exit_scope();
  expect(scopes.lookup("x")->first == 1 && !scopes.lookup("y"), "outer x visible again, y undeclared");
}

void trees() {
  const auto tree = fd::parse_tree("+(a *(b c))");
  expect(tree.has_value(), "parse the expression tree");
  if (!tree) return;
  expect(fd::preorder(*tree) == std::vector<std::string>{"+", "a", "*", "b", "c"}, "preorder (prefix form)");
  expect(fd::inorder(*tree) == std::vector<std::string>{"a", "+", "b", "*", "c"}, "inorder (infix form)");
  expect(fd::postorder(*tree) == std::vector<std::string>{"a", "b", "c", "*", "+"}, "postorder (postfix form)");
  expect(fd::level_order(*tree) == std::vector<std::string>{"+", "a", "*", "b", "c"}, "level order");
  expect(fd::height(*tree) == 3, "height 3");
  expect(!fd::parse_tree("a(b").has_value() && !fd::parse_tree("a b").has_value(), "malformed trees rejected");
}

void graphs() {
  // A loop CFG: entry -> head -> body -> head, head -> exit.
  const auto cfg = fd::parse_graph("entry -> head\nhead -> body exit\nbody -> head\n");
  expect(cfg.has_value(), "parse the CFG");
  if (!cfg) return;
  const auto dfs = fd::depth_first(*cfg);
  expect(names(*cfg, dfs.preorder) == std::vector<std::string>{"entry", "head", "body", "exit"}, "DFS preorder");
  expect(!dfs.acyclic(), "the back edge body->head is found");
  const auto back = std::count_if(dfs.edges.begin(), dfs.edges.end(),
                                  [](const auto& e) { return e.second == fd::EdgeKind::Back; });
  expect(back == 1, "exactly one back edge (one natural loop)");
  expect(!fd::topological_order(*cfg).has_value(), "no topological order for a cyclic graph");
  const auto scc = fd::strongly_connected_components(*cfg);
  expect(std::count_if(scc.begin(), scc.end(), [](const auto& c) { return c.size() == 2; }) == 1,
         "head and body form one strongly connected component");

  // Random graphs: Warshall against DFS reachability; Tarjan against mutual reachability; Kahn on DAGs.
  std::mt19937 rng(11);
  for (int round = 0; round < 200; ++round) {
    fd::Graph g;
    const std::size_t n = 2 + rng() % 9;
    const bool dag = round % 2 == 0;
    for (std::size_t v = 0; v < n; ++v) g.node("n" + std::to_string(v));
    for (std::size_t a = 0; a < n; ++a) {
      for (std::size_t b = 0; b < n; ++b) {
        if ((!dag || a < b) && a != b && rng() % 4 == 0) g.edge(g.name[a], g.name[b]);
      }
    }
    const auto reach = fd::transitive_closure(g);
    for (std::size_t v = 0; v < n; ++v) {
      const auto from_v = fd::closure({v}, [&](std::size_t x) { return g.succ[x]; });
      for (std::size_t w = 0; w < n; ++w) {
        const bool by_closure = std::find(from_v.begin(), from_v.end(), w) != from_v.end();
        expect(reach[v][w] == by_closure, "Warshall agrees with the worklist closure");
      }
    }
    const auto components = fd::strongly_connected_components(g);
    std::vector<std::size_t> component_of(n);
    std::size_t covered = 0;
    for (std::size_t c = 0; c < components.size(); ++c) {
      for (const auto v : components[c]) component_of[v] = c;
      covered += components[c].size();
    }
    expect(covered == n, "SCCs partition the nodes");
    for (std::size_t a = 0; a < n; ++a) {
      for (std::size_t b = 0; b < n; ++b) {
        expect((component_of[a] == component_of[b]) == (reach[a][b] && reach[b][a]),
               "same SCC iff mutually reachable");
      }
    }
    const auto topo = fd::topological_order(g);
    expect(topo.has_value() == fd::depth_first(g).acyclic(), "Kahn succeeds iff DFS finds no back edge");
    if (topo) {
      std::vector<std::size_t> position(n);
      for (std::size_t k = 0; k < n; ++k) position[(*topo)[k]] = k;
      for (std::size_t a = 0; a < n; ++a) {
        for (const auto b : g.succ[a]) expect(position[a] < position[b], "every edge goes forward");
      }
    }
    expect(fd::breadth_first(g).size() == n && fd::depth_first(g).postorder.size() == n, "every node visited once");
  }
}

}  // namespace

int main() {
  hash_table();
  trees();
  graphs();
  if (failures == 0) {
    std::cout << "foundations: hash table, trees and graph algorithms OK\n";
  }
  return failures == 0 ? 0 : 1;
}
