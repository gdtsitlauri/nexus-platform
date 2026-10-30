#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nexus::compiler::formal {

// Attribute grammars with synthesized AND inherited attributes (NEY613 (d): semantic analysis with
// attribute grammars).  The S-attributed actions of grammar.hpp cover the bottom-up case; this module adds
// inherited attributes, the dependency graph of a parse tree, evaluation in a topological order of that
// graph (with cycle detection), the L-attributed test and one-pass left-to-right evaluation for
// L-attributed grammars, so the two evaluators can be compared on the same tree.

enum class AttributeKind { Synthesized, Inherited };

struct AttributeRef {
  std::size_t position = 0;  // 0 = the production's left-hand side, k = the k-th right-hand-side symbol
  std::string name;
};

struct SemanticRule {
  AttributeRef target;
  std::vector<AttributeRef> arguments;
  std::function<double(const std::vector<double>&)> compute;
  std::string text;  // printable form, e.g. "L1.scale = L.scale + 1"
};

struct AttributedProduction {
  std::string lhs;
  std::vector<std::string> rhs;
  std::vector<SemanticRule> rules;
};

struct AttributeGrammar {
  std::string name;
  std::map<std::string, std::map<std::string, AttributeKind>> attributes;  // symbol -> attribute -> kind
  std::vector<AttributedProduction> productions;
};

struct ParseNode {
  std::string symbol;
  std::optional<std::size_t> production;  // none for terminals
  std::vector<ParseNode> children;
  std::string lexeme;
};

// An attribute instance in a tree: the node (preorder number) and the attribute name.
struct Instance {
  std::size_t node = 0;
  std::string name;
  bool operator==(const Instance& other) const = default;
  bool operator<(const Instance& other) const {
    return node != other.node ? node < other.node : name < other.name;
  }
};

struct Evaluation {
  bool ok = false;
  std::string error;
  std::map<Instance, double> values;
  std::vector<std::string> order;  // evaluation order, printable
  std::size_t dependency_edges = 0;
};

struct LAttributedCheck {
  bool l_attributed = true;
  std::vector<std::string> violations;  // rules that use information from the right or from the parent's synthesized side
};

LAttributedCheck check_l_attributed(const AttributeGrammar& grammar);
Evaluation evaluate_by_dependency_graph(const AttributeGrammar& grammar, const ParseNode& root);
Evaluation evaluate_l_attributed(const AttributeGrammar& grammar, const ParseNode& root);  // one depth-first pass
std::string print_tree_attributes(const ParseNode& root, const Evaluation& evaluation);

// Two classic examples with hand-written parsers.
// Knuth (1968) binary numbers with a fraction: N -> L . L | L, L -> L B | B, B -> 0 | 1; the value of each
// bit depends on an inherited scale, and the fraction's scale depends on its synthesized length, so the
// grammar is NOT L-attributed and needs the dependency graph.
AttributeGrammar knuth_binary_grammar();
std::optional<ParseNode> parse_binary(const std::string& text);
// Declarations (Aho et al., section 5.2): D -> T L, T -> int | float, L -> L , id | id; the type is handed down the
// list as the inherited attribute L.inh: L-attributed.
AttributeGrammar declaration_grammar();
std::optional<ParseNode> parse_declaration(const std::string& text);
std::string type_name(double code);

}  // namespace nexus::compiler::formal
