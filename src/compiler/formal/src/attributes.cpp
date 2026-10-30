#include "nexus/compiler/formal/attributes.hpp"

#include <cctype>
#include <cmath>
#include <deque>
#include <set>
#include <sstream>

namespace nexus::compiler::formal {

namespace {

struct Indexed {
  std::vector<const ParseNode*> nodes;  // preorder
  std::vector<std::vector<std::size_t>> child_ids;
};

void index_tree(const ParseNode& node, Indexed& index) {
  const std::size_t id = index.nodes.size();
  index.nodes.push_back(&node);
  index.child_ids.emplace_back();
  for (const auto& child : node.children) {
    index.child_ids[id].push_back(index.nodes.size());
    index_tree(child, index);
  }
}

struct Bound {
  const SemanticRule* rule = nullptr;
  Instance target;
  std::vector<Instance> arguments;
};

Instance resolve(const Indexed& index, std::size_t node, const AttributeRef& ref) {
  return Instance{ref.position == 0 ? node : index.child_ids[node][ref.position - 1], ref.name};
}

std::string instance_text(const Indexed& index, const Instance& instance) {
  return index.nodes[instance.node]->symbol + "#" + std::to_string(instance.node) + "." + instance.name;
}

std::string bound_text(const Indexed& index, const Bound& bound, double value) {
  std::ostringstream out;
  out << instance_text(index, bound.target) << " = " << value << "   [" << bound.rule->text << "]";
  return out.str();
}

// Collects every semantic rule instance of the tree; returns an error for malformed trees.
std::string bind_rules(const AttributeGrammar& grammar, const Indexed& index, std::vector<Bound>& bound) {
  for (std::size_t node = 0; node < index.nodes.size(); ++node) {
    const auto& tree_node = *index.nodes[node];
    if (!tree_node.production) {
      continue;
    }
    if (*tree_node.production >= grammar.productions.size()) {
      return "unknown production at node " + std::to_string(node);
    }
    const auto& production = grammar.productions[*tree_node.production];
    if (production.rhs.size() != tree_node.children.size()) {
      return "node " + std::to_string(node) + " does not match production " + production.lhs;
    }
    for (const auto& rule : production.rules) {
      Bound b{.rule = &rule, .target = resolve(index, node, rule.target), .arguments = {}};
      for (const auto& argument : rule.arguments) {
        b.arguments.push_back(resolve(index, node, argument));
      }
      bound.push_back(std::move(b));
    }
  }
  return {};
}

double apply_rule(const Bound& bound, const std::map<Instance, double>& values) {
  std::vector<double> args;
  for (const auto& argument : bound.arguments) {
    args.push_back(values.at(argument));
  }
  return bound.rule->compute(args);
}

AttributeKind kind_of(const AttributeGrammar& grammar, const std::string& symbol, const std::string& name) {
  const auto symbol_it = grammar.attributes.find(symbol);
  if (symbol_it != grammar.attributes.end()) {
    const auto attribute = symbol_it->second.find(name);
    if (attribute != symbol_it->second.end()) {
      return attribute->second;
    }
  }
  return AttributeKind::Synthesized;
}

const std::string& symbol_at(const AttributedProduction& production, std::size_t position) {
  return position == 0 ? production.lhs : production.rhs[position - 1];
}

void evaluate_node(const AttributeGrammar& grammar, const Indexed& index, std::size_t node,
                   const std::map<std::pair<std::size_t, std::size_t>, std::vector<const Bound*>>& by_target_position,
                   Evaluation& evaluation) {
  if (!evaluation.error.empty()) {
    return;
  }
  auto run = [&](std::size_t position) {
    const auto it = by_target_position.find({node, position});
    if (it == by_target_position.end()) {
      return;
    }
    for (const auto* bound : it->second) {
      for (const auto& argument : bound->arguments) {
        if (!evaluation.values.count(argument)) {
          evaluation.error = "cannot compute " + instance_text(index, bound->target) +
                             ": " + instance_text(index, argument) + " is not available yet";
          return;
        }
      }
      const double value = apply_rule(*bound, evaluation.values);
      evaluation.values[bound->target] = value;
      evaluation.order.push_back(bound_text(index, *bound, value));
    }
  };
  const auto& children = index.child_ids[node];
  for (std::size_t k = 0; k < children.size(); ++k) {
    run(k + 1);  // inherited attributes of child k+1 (rules of this node's production)
    if (!evaluation.error.empty()) {
      return;
    }
    evaluate_node(grammar, index, children[k], by_target_position, evaluation);
    if (!evaluation.error.empty()) {
      return;
    }
  }
  run(0);  // synthesized attributes of this node
}

}  // namespace

LAttributedCheck check_l_attributed(const AttributeGrammar& grammar) {
  LAttributedCheck check;
  for (const auto& production : grammar.productions) {
    for (const auto& rule : production.rules) {
      const auto target_kind = kind_of(grammar, symbol_at(production, rule.target.position), rule.target.name);
      if (rule.target.position == 0) {
        if (target_kind != AttributeKind::Synthesized) {
          check.l_attributed = false;
          check.violations.push_back(rule.text + "  (defines an inherited attribute of the left-hand side)");
        }
        continue;
      }
      if (target_kind != AttributeKind::Inherited) {
        check.l_attributed = false;
        check.violations.push_back(rule.text + "  (defines a synthesized attribute of a right-hand-side symbol)");
        continue;
      }
      for (const auto& argument : rule.arguments) {
        const bool from_parent = argument.position == 0 &&
            kind_of(grammar, production.lhs, argument.name) == AttributeKind::Inherited;
        const bool from_left = argument.position >= 1 && argument.position < rule.target.position;
        if (!from_parent && !from_left) {
          check.l_attributed = false;
          check.violations.push_back(rule.text + "  (uses " + symbol_at(production, argument.position) + "." +
                                     argument.name + ", which is not to the left)");
        }
      }
    }
  }
  return check;
}

Evaluation evaluate_by_dependency_graph(const AttributeGrammar& grammar, const ParseNode& root) {
  Evaluation evaluation;
  Indexed index;
  index_tree(root, index);
  std::vector<Bound> bound;
  if (const auto error = bind_rules(grammar, index, bound); !error.empty()) {
    evaluation.error = error;
    return evaluation;
  }
  std::map<Instance, std::size_t> defining;
  for (std::size_t r = 0; r < bound.size(); ++r) {
    if (!defining.emplace(bound[r].target, r).second) {
      evaluation.error = "two rules define " + instance_text(index, bound[r].target);
      return evaluation;
    }
  }
  // Dependency graph over rule instances: an edge from the rule defining an argument to the rule using it.
  std::vector<std::vector<std::size_t>> users(bound.size());
  std::vector<std::size_t> pending(bound.size(), 0);
  for (std::size_t r = 0; r < bound.size(); ++r) {
    for (const auto& argument : bound[r].arguments) {
      const auto it = defining.find(argument);
      if (it == defining.end()) {
        evaluation.error = "no rule defines " + instance_text(index, argument);
        return evaluation;
      }
      users[it->second].push_back(r);
      ++pending[r];
      ++evaluation.dependency_edges;
    }
  }
  std::deque<std::size_t> ready;
  for (std::size_t r = 0; r < bound.size(); ++r) {
    if (pending[r] == 0) ready.push_back(r);
  }
  std::size_t done = 0;
  while (!ready.empty()) {
    const auto r = ready.front();
    ready.pop_front();
    const double value = apply_rule(bound[r], evaluation.values);
    evaluation.values[bound[r].target] = value;
    evaluation.order.push_back(bound_text(index, bound[r], value));
    ++done;
    for (const auto user : users[r]) {
      if (--pending[user] == 0) ready.push_back(user);
    }
  }
  if (done != bound.size()) {
    std::ostringstream out;
    out << "circular dependencies:";
    for (std::size_t r = 0; r < bound.size(); ++r) {
      if (pending[r] != 0) out << ' ' << instance_text(index, bound[r].target);
    }
    evaluation.error = out.str();
    return evaluation;
  }
  evaluation.ok = true;
  return evaluation;
}

Evaluation evaluate_l_attributed(const AttributeGrammar& grammar, const ParseNode& root) {
  Evaluation evaluation;
  Indexed index;
  index_tree(root, index);
  std::vector<Bound> bound;
  if (const auto error = bind_rules(grammar, index, bound); !error.empty()) {
    evaluation.error = error;
    return evaluation;
  }
  // Group rule instances by (node whose production owns the rule, target position).
  std::map<std::pair<std::size_t, std::size_t>, std::vector<const Bound*>> by_target_position;
  for (std::size_t node = 0, b = 0; node < index.nodes.size(); ++node) {
    const auto& tree_node = *index.nodes[node];
    if (!tree_node.production) continue;
    for (const auto& rule : grammar.productions[*tree_node.production].rules) {
      by_target_position[{node, rule.target.position}].push_back(&bound[b++]);
    }
  }
  evaluate_node(grammar, index, 0, by_target_position, evaluation);
  evaluation.ok = evaluation.error.empty();
  return evaluation;
}

std::string print_tree_attributes(const ParseNode& root, const Evaluation& evaluation) {
  std::ostringstream out;
  std::size_t counter = 0;
  std::function<void(const ParseNode&, std::size_t)> print = [&](const ParseNode& node, std::size_t depth) {
    const std::size_t id = counter++;
    out << std::string(depth * 2, ' ') << node.symbol;
    if (!node.lexeme.empty() && node.lexeme != node.symbol) out << " '" << node.lexeme << "'";
    for (const auto& [instance, value] : evaluation.values) {
      if (instance.node == id) out << "  " << instance.name << "=" << value;
    }
    out << '\n';
    for (const auto& child : node.children) print(child, depth + 1);
  };
  print(root, 0);
  return out.str();
}

// ---- examples ----------------------------------------------------------------------------------------

namespace {

SemanticRule rule(AttributeRef target, std::vector<AttributeRef> arguments,
                  std::function<double(const std::vector<double>&)> compute, std::string text) {
  return SemanticRule{std::move(target), std::move(arguments), std::move(compute), std::move(text)};
}

double first(const std::vector<double>& a) { return a[0]; }

ParseNode terminal(const std::string& symbol, const std::string& lexeme) {
  return ParseNode{.symbol = symbol, .production = std::nullopt, .children = {}, .lexeme = lexeme};
}

}  // namespace

AttributeGrammar knuth_binary_grammar() {
  AttributeGrammar g;
  g.name = "Knuth binary numbers";
  g.attributes["N"] = {{"val", AttributeKind::Synthesized}};
  g.attributes["L"] = {{"scale", AttributeKind::Inherited}, {"val", AttributeKind::Synthesized},
                       {"len", AttributeKind::Synthesized}};
  g.attributes["B"] = {{"scale", AttributeKind::Inherited}, {"val", AttributeKind::Synthesized}};
  // 0: N -> L . L
  g.productions.push_back({"N", {"L", ".", "L"}, {
      rule({1, "scale"}, {}, [](const auto&) { return 0.0; }, "L1.scale = 0"),
      rule({3, "scale"}, {{3, "len"}}, [](const auto& a) { return -a[0]; }, "L2.scale = -L2.len"),
      rule({0, "val"}, {{1, "val"}, {3, "val"}}, [](const auto& a) { return a[0] + a[1]; }, "N.val = L1.val + L2.val")}});
  // 1: N -> L
  g.productions.push_back({"N", {"L"}, {
      rule({1, "scale"}, {}, [](const auto&) { return 0.0; }, "L.scale = 0"),
      rule({0, "val"}, {{1, "val"}}, first, "N.val = L.val")}});
  // 2: L -> L B
  g.productions.push_back({"L", {"L", "B"}, {
      rule({1, "scale"}, {{0, "scale"}}, [](const auto& a) { return a[0] + 1; }, "L1.scale = L.scale + 1"),
      rule({2, "scale"}, {{0, "scale"}}, first, "B.scale = L.scale"),
      rule({0, "val"}, {{1, "val"}, {2, "val"}}, [](const auto& a) { return a[0] + a[1]; }, "L.val = L1.val + B.val"),
      rule({0, "len"}, {{1, "len"}}, [](const auto& a) { return a[0] + 1; }, "L.len = L1.len + 1")}});
  // 3: L -> B
  g.productions.push_back({"L", {"B"}, {
      rule({1, "scale"}, {{0, "scale"}}, first, "B.scale = L.scale"),
      rule({0, "val"}, {{1, "val"}}, first, "L.val = B.val"),
      rule({0, "len"}, {}, [](const auto&) { return 1.0; }, "L.len = 1")}});
  // 4: B -> 0, 5: B -> 1
  g.productions.push_back({"B", {"0"}, {rule({0, "val"}, {}, [](const auto&) { return 0.0; }, "B.val = 0")}});
  g.productions.push_back({"B", {"1"}, {
      rule({0, "val"}, {{0, "scale"}}, [](const auto& a) { return std::ldexp(1.0, static_cast<int>(a[0])); },
           "B.val = 2^B.scale")}});
  return g;
}

namespace {
ParseNode bit_list(const std::string& bits) {
  // Left-recursive L -> L B | B.
  ParseNode list{.symbol = "L", .production = 3, .children = {}, .lexeme = {}};
  for (std::size_t i = 0; i < bits.size(); ++i) {
    ParseNode bit{.symbol = "B", .production = bits[i] == '1' ? 5U : 4U,
                  .children = {terminal(std::string(1, bits[i]), std::string(1, bits[i]))}, .lexeme = {}};
    if (i == 0) {
      list.children.push_back(std::move(bit));
    } else {
      list = ParseNode{.symbol = "L", .production = 2, .children = {std::move(list), std::move(bit)}, .lexeme = {}};
    }
  }
  return list;
}
}  // namespace

std::optional<ParseNode> parse_binary(const std::string& text) {
  const auto dot = text.find('.');
  const std::string whole = text.substr(0, dot);
  const std::string fraction = dot == std::string::npos ? std::string{} : text.substr(dot + 1);
  auto bits = [](const std::string& s) { return !s.empty() && s.find_first_not_of("01") == std::string::npos; };
  if (!bits(whole) || (dot != std::string::npos && !bits(fraction))) {
    return std::nullopt;
  }
  if (dot == std::string::npos) {
    return ParseNode{.symbol = "N", .production = 1, .children = {bit_list(whole)}, .lexeme = {}};
  }
  return ParseNode{.symbol = "N", .production = 0,
                   .children = {bit_list(whole), terminal(".", "."), bit_list(fraction)}, .lexeme = {}};
}

AttributeGrammar declaration_grammar() {
  AttributeGrammar g;
  g.name = "declarations";
  g.attributes["T"] = {{"type", AttributeKind::Synthesized}};
  g.attributes["L"] = {{"inh", AttributeKind::Inherited}};
  g.attributes["id"] = {{"type", AttributeKind::Inherited}};
  // 0: D -> T L
  g.productions.push_back({"D", {"T", "L"}, {rule({2, "inh"}, {{1, "type"}}, first, "L.inh = T.type")}});
  // 1: T -> int, 2: T -> float
  g.productions.push_back({"T", {"int"}, {rule({0, "type"}, {}, [](const auto&) { return 1.0; }, "T.type = int")}});
  g.productions.push_back({"T", {"float"}, {rule({0, "type"}, {}, [](const auto&) { return 2.0; }, "T.type = float")}});
  // 3: L -> L , id
  g.productions.push_back({"L", {"L", ",", "id"}, {
      rule({1, "inh"}, {{0, "inh"}}, first, "L1.inh = L.inh"),
      rule({3, "type"}, {{0, "inh"}}, first, "addtype(id.entry, L.inh)")}});
  // 4: L -> id
  g.productions.push_back({"L", {"id"}, {rule({1, "type"}, {{0, "inh"}}, first, "addtype(id.entry, L.inh)")}});
  return g;
}

std::optional<ParseNode> parse_declaration(const std::string& text) {
  std::istringstream words(text);
  std::vector<std::string> tokens;
  for (std::string word; words >> word;) {
    // split "a,b" into a , b
    std::string current;
    for (const char c : word) {
      if (c == ',') {
        if (!current.empty()) tokens.push_back(current);
        tokens.emplace_back(",");
        current.clear();
      } else {
        current += c;
      }
    }
    if (!current.empty()) tokens.push_back(current);
  }
  if (tokens.size() < 2 || (tokens[0] != "int" && tokens[0] != "float") || tokens.size() % 2 != 0) {
    return std::nullopt;
  }
  ParseNode type{.symbol = "T", .production = tokens[0] == "int" ? 1U : 2U,
                 .children = {terminal(tokens[0], tokens[0])}, .lexeme = {}};
  auto identifier = [](const std::string& t) {
    return !t.empty() && (std::isalpha(static_cast<unsigned char>(t[0])) || t[0] == '_') && t != "int" && t != "float";
  };
  if (!identifier(tokens[1])) return std::nullopt;
  ParseNode list{.symbol = "L", .production = 4, .children = {terminal("id", tokens[1])}, .lexeme = {}};
  for (std::size_t i = 2; i + 1 < tokens.size(); i += 2) {
    if (tokens[i] != "," || !identifier(tokens[i + 1])) return std::nullopt;
    list = ParseNode{.symbol = "L", .production = 3,
                     .children = {std::move(list), terminal(",", ","), terminal("id", tokens[i + 1])}, .lexeme = {}};
  }
  return ParseNode{.symbol = "D", .production = 0, .children = {std::move(type), std::move(list)}, .lexeme = {}};
}

std::string type_name(double code) {
  return code == 1.0 ? "int" : code == 2.0 ? "float" : "?";
}

}  // namespace nexus::compiler::formal
