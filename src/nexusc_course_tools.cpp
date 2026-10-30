// nexusc commands for course topics that are not part of the NexusLang compiler pipeline itself:
// attribute grammars with inherited attributes (NEY613).
#include <iostream>
#include <string>

#include "nexus/compiler/formal/attributes.hpp"

int run_attributes(int argc, char** argv) {
  namespace fm = nexus::compiler::formal;
  const std::string usage = "usage: nexusc attr knuth <binary number> | nexusc attr decl \"<declaration>\"\n";
  if (argc < 4) {
    std::cerr << usage;
    return 1;
  }
  const std::string which = argv[2];
  const bool knuth = which == "knuth";
  if (!knuth && which != "decl") {
    std::cerr << usage;
    return 1;
  }
  const auto grammar = knuth ? fm::knuth_binary_grammar() : fm::declaration_grammar();
  const auto tree = knuth ? fm::parse_binary(argv[3]) : fm::parse_declaration(argv[3]);
  if (!tree) {
    std::cerr << "error: the input is not in the language of the " << grammar.name << " grammar\n";
    return 1;
  }
  std::cout << "grammar: " << grammar.name << '\n';
  for (const auto& production : grammar.productions) {
    std::cout << "  " << production.lhs << " ->";
    for (const auto& symbol : production.rhs) {
      std::cout << ' ' << symbol;
    }
    std::cout << '\n';
    for (const auto& rule : production.rules) {
      std::cout << "      " << rule.text << '\n';
    }
  }
  const auto check = fm::check_l_attributed(grammar);
  std::cout << "L-attributed: " << (check.l_attributed ? "yes" : "no") << '\n';
  for (const auto& violation : check.violations) {
    std::cout << "  " << violation << '\n';
  }
  const auto graph = fm::evaluate_by_dependency_graph(grammar, *tree);
  if (!graph.ok) {
    std::cout << "dependency graph: " << graph.error << '\n';
    return 1;
  }
  std::cout << "dependency graph: " << graph.order.size() << " rule instances, " << graph.dependency_edges
            << " edges; evaluation in topological order:\n";
  for (const auto& step : graph.order) {
    std::cout << "  " << step << '\n';
  }
  std::cout << "annotated parse tree:\n" << fm::print_tree_attributes(*tree, graph);
  const auto one_pass = fm::evaluate_l_attributed(grammar, *tree);
  std::cout << "one left-to-right pass: "
            << (one_pass.ok ? (one_pass.values == graph.values ? "same values" : "DIFFERENT values") : one_pass.error)
            << '\n';
  if (knuth) {
    std::cout << "value: " << graph.values.at(fm::Instance{0, "val"}) << '\n';
  }
  return 0;
}
