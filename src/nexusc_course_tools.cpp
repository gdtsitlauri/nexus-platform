// nexusc commands for course topics that are not part of the NexusLang compiler pipeline itself:
// attribute grammars with inherited attributes (NEY613) and software pipelining (NEY606).
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "nexus/compiler/formal/attributes.hpp"
#include "nexus/compiler/passes/modulo_schedule.hpp"

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

// Software pipelining of a loop's dependence graph (NEY606).
int run_modulo_schedule(int argc, char** argv) {
  namespace ms = nexus::compiler::passes;
  if (argc < 3) {
    std::cerr << "usage: nexusc modsched <file.ddg>\n";
    return 1;
  }
  std::ifstream in(argv[2]);
  if (!in) {
    std::cerr << "error: cannot open " << argv[2] << '\n';
    return 1;
  }
  std::stringstream text;
  text << in.rdbuf();
  const auto parsed = ms::parse_ddg(text.str());
  if (!parsed.ddg) {
    std::cerr << "error: " << parsed.error << '\n';
    return 1;
  }
  const auto schedule = ms::modulo_schedule(*parsed.ddg);
  std::cout << ms::print_schedule(*parsed.ddg, schedule);
  const auto problem = schedule.success ? ms::verify_schedule(*parsed.ddg, schedule) : schedule.error;
  std::cout << "verification: " << (problem.empty() ? "every dependence and resource limit holds" : problem) << '\n';
  return problem.empty() ? 0 : 1;
}
