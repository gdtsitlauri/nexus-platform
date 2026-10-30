#include <cmath>
#include <iostream>
#include <random>
#include <string>

#include "nexus/compiler/formal/attributes.hpp"

namespace fm = nexus::compiler::formal;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

double root_value(const fm::Evaluation& evaluation, const std::string& name) {
  const auto it = evaluation.values.find(fm::Instance{0, name});
  return it == evaluation.values.end() ? std::nan("") : it->second;
}

double direct_binary(const std::string& text) {
  double value = 0;
  const auto dot = text.find('.');
  const std::string whole = text.substr(0, dot);
  for (const char c : whole) value = value * 2 + (c - '0');
  if (dot != std::string::npos) {
    double weight = 0.5;
    for (std::size_t i = dot + 1; i < text.size(); ++i, weight /= 2) value += (text[i] - '0') * weight;
  }
  return value;
}

void knuth() {
  const auto grammar = fm::knuth_binary_grammar();
  const auto check = fm::check_l_attributed(grammar);
  expect(!check.l_attributed && check.violations.size() == 1 &&
             check.violations.front().find("L2.scale = -L2.len") != std::string::npos,
         "Knuth's grammar is not L-attributed: only L2.scale = -L2.len looks to the right");
  const auto tree = fm::parse_binary("1101.01");
  expect(tree.has_value(), "parse 1101.01");
  if (tree) {
    const auto graph = fm::evaluate_by_dependency_graph(grammar, *tree);
    expect(graph.ok && root_value(graph, "val") == 13.25, "1101.01 = 13.25 by the dependency graph");
    const auto one_pass = fm::evaluate_l_attributed(grammar, *tree);
    expect(!one_pass.ok && one_pass.error.find("scale") != std::string::npos,
           "one left-to-right pass cannot evaluate Knuth's grammar");
  }
  const auto integer = fm::parse_binary("101100");
  if (integer) {
    // Without a fraction every rule is L-attributed in effect: both evaluators agree.
    const auto graph = fm::evaluate_by_dependency_graph(grammar, *integer);
    const auto one_pass = fm::evaluate_l_attributed(grammar, *integer);
    expect(graph.ok && one_pass.ok && graph.values == one_pass.values && root_value(graph, "val") == 44,
           "101100 = 44, identical attribute values from both evaluators");
  }
  std::mt19937 rng(5);
  for (int round = 0; round < 500; ++round) {
    std::string text;
    const auto whole = 1 + rng() % 12;
    for (std::size_t i = 0; i < whole; ++i) text += static_cast<char>('0' + rng() % 2);
    if (rng() % 2) {
      text += '.';
      const auto fraction = 1 + rng() % 12;
      for (std::size_t i = 0; i < fraction; ++i) text += static_cast<char>('0' + rng() % 2);
    }
    const auto parsed = fm::parse_binary(text);
    const auto evaluation = fm::evaluate_by_dependency_graph(grammar, *parsed);
    expect(evaluation.ok && root_value(evaluation, "val") == direct_binary(text), "random binary number " + text);
  }
  expect(!fm::parse_binary("10.").has_value() && !fm::parse_binary("12").has_value(), "malformed numbers rejected");
}

void declarations() {
  const auto grammar = fm::declaration_grammar();
  expect(fm::check_l_attributed(grammar).l_attributed, "the declaration grammar is L-attributed");
  const auto tree = fm::parse_declaration("float a, b, c");
  expect(tree.has_value(), "parse float a, b, c");
  if (!tree) return;
  const auto graph = fm::evaluate_by_dependency_graph(grammar, *tree);
  const auto one_pass = fm::evaluate_l_attributed(grammar, *tree);
  expect(graph.ok && one_pass.ok && graph.values == one_pass.values, "both evaluators give the same attributes");
  std::size_t typed = 0;
  for (const auto& [instance, value] : one_pass.values) {
    if (instance.name == "type" && fm::type_name(value) == "float") ++typed;
  }
  expect(typed == 4, "T and the three identifiers all get type float");
  expect(!fm::parse_declaration("int a b").has_value() && !fm::parse_declaration("char a").has_value(),
         "malformed declarations rejected");
}

void circular() {
  // A grammar with a circular definition: S -> A, A.i = A.s, A.s = A.i (through A -> a).
  fm::AttributeGrammar g;
  g.attributes["A"] = {{"i", fm::AttributeKind::Inherited}, {"s", fm::AttributeKind::Synthesized}};
  g.productions.push_back({"S", {"A"}, {{{1, "i"}, {{1, "s"}}, [](const auto& a) { return a[0]; }, "A.i = A.s"}}});
  g.productions.push_back({"A", {"a"}, {{{0, "s"}, {{0, "i"}}, [](const auto& a) { return a[0]; }, "A.s = A.i"}}});
  fm::ParseNode leaf{.symbol = "a", .production = std::nullopt, .children = {}, .lexeme = "a"};
  fm::ParseNode a{.symbol = "A", .production = 1, .children = {leaf}, .lexeme = {}};
  fm::ParseNode s{.symbol = "S", .production = 0, .children = {a}, .lexeme = {}};
  const auto evaluation = fm::evaluate_by_dependency_graph(g, s);
  expect(!evaluation.ok && evaluation.error.find("circular") != std::string::npos, "cycle in the dependency graph found");
}

}  // namespace

int main() {
  knuth();
  declarations();
  circular();
  if (failures == 0) {
    std::cout << "attribute grammars: inherited attributes, dependency graphs and L-attributed evaluation OK\n";
  }
  return failures == 0 ? 0 : 1;
}
