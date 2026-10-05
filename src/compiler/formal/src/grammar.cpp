#include "nexus/compiler/formal/grammar.hpp"

#include <algorithm>
#include <cctype>
#include <deque>
#include <functional>
#include <limits>
#include <sstream>
#include <tuple>

namespace nexus::compiler::formal {

namespace {

constexpr std::size_t kMaxSteps = 400;
constexpr std::uint64_t kSaturate = 1'000'000'000'000'000'000ULL;

bool is_integer(const std::string& text) {
  if (text.empty()) {
    return false;
  }
  const std::size_t begin = text[0] == '-' && text.size() > 1 ? 1U : 0U;
  return std::all_of(text.begin() + static_cast<std::ptrdiff_t>(begin), text.end(),
                     [](unsigned char ch) { return std::isdigit(ch) != 0; });
}

bool is_identifier(const std::string& text) {
  return !text.empty() && (std::isalpha(static_cast<unsigned char>(text[0])) != 0 || text[0] == '_') &&
         std::all_of(text.begin(), text.end(),
                     [](unsigned char ch) { return std::isalnum(ch) != 0 || ch == '_'; });
}

std::size_t add_symbol(Grammar& grammar, const std::string& name, bool terminal) {
  if (const auto found = grammar.find(name); found.has_value()) {
    return *found;
  }
  grammar.names.push_back(name);
  grammar.terminal.push_back(terminal);
  return grammar.names.size() - 1;
}

std::string fresh_name(const Grammar& grammar, const std::string& base) {
  std::string name = base + "'";
  while (grammar.find(name).has_value()) {
    name += "'";
  }
  return name;
}

std::uint64_t saturating_add(std::uint64_t lhs, std::uint64_t rhs) {
  return lhs > kSaturate - rhs ? kSaturate : lhs + rhs;
}

std::uint64_t saturating_mul(std::uint64_t lhs, std::uint64_t rhs) {
  if (lhs == 0 || rhs == 0) {
    return 0;
  }
  return lhs > kSaturate / rhs ? kSaturate : lhs * rhs;
}

// ---- action expressions: $$ = expr over $k, integers, + - * / % ( ) and unary minus ----------
class ActionEvaluator {
 public:
  ActionEvaluator(const std::string& text, const std::vector<std::int64_t>& values) : text_(text), values_(values) {}

  std::int64_t run() {
    skip();
    expect("$$");
    expect("=");
    const std::int64_t result = expression();
    skip();
    if (position_ != text_.size()) {
      throw std::string("trailing input in action '" + text_ + "'");
    }
    return result;
  }

 private:
  void skip() {
    while (position_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[position_])) != 0) {
      ++position_;
    }
  }

  void expect(const std::string& word) {
    skip();
    if (text_.compare(position_, word.size(), word) != 0) {
      throw std::string("expected '" + word + "' in action '" + text_ + "'");
    }
    position_ += word.size();
  }

  bool accept(char ch) {
    skip();
    if (position_ < text_.size() && text_[position_] == ch) {
      ++position_;
      return true;
    }
    return false;
  }

  std::int64_t expression() {
    std::int64_t value = term();
    while (true) {
      if (accept('+')) {
        value += term();
      } else if (accept('-')) {
        value -= term();
      } else {
        return value;
      }
    }
  }

  std::int64_t term() {
    std::int64_t value = factor();
    while (true) {
      if (accept('*')) {
        value *= factor();
      } else if (accept('/') || accept('%')) {
        const char op = text_[position_ - 1];
        const std::int64_t divisor = factor();
        if (divisor == 0) {
          throw std::string("division by zero in action");
        }
        value = op == '/' ? value / divisor : value % divisor;
      } else {
        return value;
      }
    }
  }

  std::int64_t factor() {
    if (accept('-')) {
      return -factor();
    }
    if (accept('(')) {
      const std::int64_t value = expression();
      if (!accept(')')) {
        throw std::string("missing ')' in action");
      }
      return value;
    }
    skip();
    if (accept('$')) {
      std::size_t index = 0;
      bool digits = false;
      while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_])) != 0) {
        index = index * 10 + static_cast<std::size_t>(text_[position_++] - '0');
        digits = true;
      }
      if (!digits || index == 0 || index > values_.size()) {
        throw std::string("invalid attribute reference in action '" + text_ + "'");
      }
      return values_[index - 1];
    }
    std::int64_t value = 0;
    bool digits = false;
    while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_])) != 0) {
      value = value * 10 + (text_[position_++] - '0');
      digits = true;
    }
    if (!digits) {
      throw std::string("expected operand in action '" + text_ + "'");
    }
    return value;
  }

  const std::string& text_;
  const std::vector<std::int64_t>& values_;
  std::size_t position_ = 0;
};

std::pair<std::set<std::size_t>, bool> first_of_sequence(
    const Grammar& grammar,
    const FirstFollow& sets,
    const std::vector<std::size_t>& sequence,
    std::size_t from) {
  std::set<std::size_t> result;
  for (std::size_t index = from; index < sequence.size(); ++index) {
    const std::size_t symbol = sequence[index];
    result.insert(sets.first[symbol].begin(), sets.first[symbol].end());
    if (grammar.terminal[symbol] || !sets.nullable[symbol]) {
      return {result, false};
    }
  }
  return {result, true};
}

std::string symbols_text(const Grammar& grammar, const std::set<std::size_t>& symbols) {
  std::string text = "{";
  bool first = true;
  for (const std::size_t symbol : symbols) {
    text += (first ? "" : ", ") + grammar.names[symbol];
    first = false;
  }
  return text + "}";
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Grammar parsing and printing
// ---------------------------------------------------------------------------------------------
std::optional<std::size_t> Grammar::find(const std::string& name) const {
  for (std::size_t index = 0; index < names.size(); ++index) {
    if (names[index] == name) {
      return index;
    }
  }
  return std::nullopt;
}

std::string Grammar::production_text(std::size_t index) const {
  const auto& production = productions[index];
  std::string text = names[production.lhs] + " ->";
  if (production.rhs.empty()) {
    text += " %empty";
  }
  for (const std::size_t symbol : production.rhs) {
    text += " " + names[symbol];
  }
  return text;
}

GrammarResult parse_grammar(const std::string& text) {
  struct Word {
    std::string text;
    bool quoted = false;
    bool action = false;
  };
  std::vector<Word> words;
  for (std::size_t position = 0; position < text.size();) {
    const char ch = text[position];
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      ++position;
    } else if (ch == '#') {
      while (position < text.size() && text[position] != '\n') {
        ++position;
      }
    } else if (ch == '{') {
      const std::size_t close = text.find('}', position);
      if (close == std::string::npos) {
        return {std::nullopt, "unterminated action block"};
      }
      words.push_back({text.substr(position + 1, close - position - 1), false, true});
      position = close + 1;
    } else if (ch == '\'') {
      const std::size_t close = text.find('\'', position + 1);
      if (close == std::string::npos || close == position + 1) {
        return {std::nullopt, "bad quoted terminal"};
      }
      words.push_back({text.substr(position + 1, close - position - 1), true, false});
      position = close + 1;
    } else {
      std::size_t end = position;
      while (end < text.size() && std::isspace(static_cast<unsigned char>(text[end])) == 0 && text[end] != '{' &&
             text[end] != '#') {
        ++end;
      }
      words.push_back({text.substr(position, end - position), false, false});
      position = end;
    }
  }

  struct Alternative {
    std::vector<Word> symbols;
    std::string action;
  };
  struct Rule {
    std::string lhs;
    std::vector<Alternative> alternatives;
  };
  std::vector<Rule> rules;
  std::optional<std::string> start_name;
  for (std::size_t index = 0; index < words.size();) {
    if (!words[index].quoted && words[index].text == "%start") {
      if (index + 1 >= words.size()) {
        return {std::nullopt, "%start needs a symbol"};
      }
      start_name = words[index + 1].text;
      index += 2;
      continue;
    }
    if (index + 1 >= words.size() || words[index + 1].quoted || words[index + 1].text != "->") {
      return {std::nullopt, "expected 'X ->' near '" + words[index].text + "'"};
    }
    Rule rule{words[index].text, {Alternative{}}};
    index += 2;
    while (index < words.size()) {
      const Word& word = words[index++];
      if (!word.quoted && !word.action && word.text == ";") {
        break;
      }
      if (!word.quoted && !word.action && word.text == "|") {
        rule.alternatives.emplace_back();
        continue;
      }
      if (word.action) {
        rule.alternatives.back().action = word.text;
        continue;
      }
      if (!word.quoted && (word.text == "%empty" || word.text == "ε")) {
        continue;
      }
      rule.alternatives.back().symbols.push_back(word);
    }
    rules.push_back(std::move(rule));
  }
  if (rules.empty()) {
    return {std::nullopt, "grammar has no productions"};
  }

  Grammar grammar;
  grammar.end_marker = add_symbol(grammar, "$", true);
  std::set<std::string> nonterminals;
  for (const auto& rule : rules) {
    nonterminals.insert(rule.lhs);
  }
  for (const auto& rule : rules) {
    add_symbol(grammar, rule.lhs, false);
  }
  for (const auto& rule : rules) {
    const std::size_t lhs = *grammar.find(rule.lhs);
    for (const auto& alternative : rule.alternatives) {
      Production production{lhs, {}, alternative.action};
      for (const auto& word : alternative.symbols) {
        const bool terminal = word.quoted || !nonterminals.contains(word.text);
        if (word.quoted && nonterminals.contains(word.text)) {
          return {std::nullopt, "quoted terminal '" + word.text + "' clashes with a nonterminal"};
        }
        production.rhs.push_back(add_symbol(grammar, word.text, terminal));
      }
      grammar.productions.push_back(std::move(production));
    }
  }
  const std::string start = start_name.value_or(rules.front().lhs);
  const auto start_symbol = grammar.find(start);
  if (!start_symbol.has_value() || grammar.terminal[*start_symbol]) {
    return {std::nullopt, "start symbol '" + start + "' is not a nonterminal"};
  }
  grammar.start = *start_symbol;
  return {std::move(grammar), ""};
}

std::string print_grammar(const Grammar& grammar) {
  std::ostringstream out;
  out << "%start " << grammar.names[grammar.start] << '\n';
  for (std::size_t symbol = 0; symbol < grammar.names.size(); ++symbol) {
    if (grammar.terminal[symbol]) {
      continue;
    }
    bool first = true;
    for (const auto& production : grammar.productions) {
      if (production.lhs != symbol) {
        continue;
      }
      out << (first ? grammar.names[symbol] + " ->" : std::string(grammar.names[symbol].size(), ' ') + "  |");
      if (production.rhs.empty()) {
        out << " %empty";
      }
      for (const std::size_t rhs : production.rhs) {
        out << ' ' << grammar.names[rhs];
      }
      if (!production.action.empty()) {
        out << " {" << production.action << '}';
      }
      out << '\n';
      first = false;
    }
    if (!first) {
      out << std::string(grammar.names[symbol].size(), ' ') << "  ;\n";
    }
  }
  return out.str();
}

// ---------------------------------------------------------------------------------------------
// FIRST / FOLLOW
// ---------------------------------------------------------------------------------------------
FirstFollow compute_first_follow(const Grammar& grammar) {
  const std::size_t count = grammar.names.size();
  FirstFollow sets;
  sets.nullable.assign(count, false);
  sets.first.assign(count, {});
  sets.follow.assign(count, {});
  for (std::size_t symbol = 0; symbol < count; ++symbol) {
    if (grammar.terminal[symbol]) {
      sets.first[symbol].insert(symbol);
    }
  }
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto& production : grammar.productions) {
      const auto [first, nullable] = first_of_sequence(grammar, sets, production.rhs, 0);
      const std::size_t before = sets.first[production.lhs].size();
      sets.first[production.lhs].insert(first.begin(), first.end());
      changed = changed || sets.first[production.lhs].size() != before;
      if (nullable && !sets.nullable[production.lhs]) {
        sets.nullable[production.lhs] = true;
        changed = true;
      }
    }
  }
  sets.follow[grammar.start].insert(grammar.end_marker);
  changed = true;
  while (changed) {
    changed = false;
    for (const auto& production : grammar.productions) {
      for (std::size_t index = 0; index < production.rhs.size(); ++index) {
        const std::size_t symbol = production.rhs[index];
        if (grammar.terminal[symbol]) {
          continue;
        }
        const auto [first, nullable] = first_of_sequence(grammar, sets, production.rhs, index + 1);
        const std::size_t before = sets.follow[symbol].size();
        sets.follow[symbol].insert(first.begin(), first.end());
        if (nullable) {
          sets.follow[symbol].insert(sets.follow[production.lhs].begin(), sets.follow[production.lhs].end());
        }
        changed = changed || sets.follow[symbol].size() != before;
      }
    }
  }
  return sets;
}

std::string print_first_follow(const Grammar& grammar, const FirstFollow& sets) {
  std::ostringstream out;
  out << "nonterminal   nullable  FIRST / FOLLOW\n";
  for (std::size_t symbol = 0; symbol < grammar.names.size(); ++symbol) {
    if (grammar.terminal[symbol]) {
      continue;
    }
    out << grammar.names[symbol] << std::string(grammar.names[symbol].size() < 14 ? 14 - grammar.names[symbol].size() : 1, ' ')
        << (sets.nullable[symbol] ? "yes       " : "no        ") << "FIRST = " << symbols_text(grammar, sets.first[symbol])
        << "  FOLLOW = " << symbols_text(grammar, sets.follow[symbol]) << '\n';
  }
  return out.str();
}

// ---------------------------------------------------------------------------------------------
// Transformations
// ---------------------------------------------------------------------------------------------
Grammar eliminate_left_recursion(const Grammar& input) {
  Grammar grammar = input;
  std::vector<std::size_t> order;
  for (std::size_t symbol = 0; symbol < input.names.size(); ++symbol) {
    if (!input.terminal[symbol]) {
      order.push_back(symbol);
    }
  }
  for (std::size_t i = 0; i < order.size(); ++i) {
    const std::size_t ai = order[i];
    // Substitute earlier nonterminals at the front of Ai's alternatives.
    for (std::size_t j = 0; j < i; ++j) {
      const std::size_t aj = order[j];
      std::vector<Production> rewritten;
      for (const auto& production : grammar.productions) {
        if (production.lhs == ai && !production.rhs.empty() && production.rhs.front() == aj) {
          for (const auto& expansion : grammar.productions) {
            if (expansion.lhs != aj) {
              continue;
            }
            Production replaced{ai, expansion.rhs, ""};
            replaced.rhs.insert(replaced.rhs.end(), production.rhs.begin() + 1, production.rhs.end());
            rewritten.push_back(std::move(replaced));
          }
        } else {
          rewritten.push_back(production);
        }
      }
      grammar.productions = std::move(rewritten);
    }
    // Remove immediate left recursion: A -> A a | b  ==>  A -> b A' ; A' -> a A' | %empty
    const bool recursive = std::any_of(grammar.productions.begin(), grammar.productions.end(), [&](const Production& p) {
      return p.lhs == ai && !p.rhs.empty() && p.rhs.front() == ai;
    });
    if (!recursive) {
      continue;
    }
    const std::size_t tail = add_symbol(grammar, fresh_name(grammar, grammar.names[ai]), false);
    std::vector<Production> rewritten;
    for (const auto& production : grammar.productions) {
      if (production.lhs != ai) {
        rewritten.push_back(production);
        continue;
      }
      if (!production.rhs.empty() && production.rhs.front() == ai) {
        Production loop{tail, std::vector<std::size_t>(production.rhs.begin() + 1, production.rhs.end()), ""};
        loop.rhs.push_back(tail);
        rewritten.push_back(std::move(loop));
      } else {
        Production base{ai, production.rhs, ""};
        base.rhs.push_back(tail);
        rewritten.push_back(std::move(base));
      }
    }
    rewritten.push_back(Production{tail, {}, ""});
    grammar.productions = std::move(rewritten);
  }
  return grammar;
}

Grammar left_factor(const Grammar& input) {
  Grammar grammar = input;
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t symbol = 0; symbol < grammar.names.size() && !changed; ++symbol) {
      if (grammar.terminal[symbol]) {
        continue;
      }
      std::vector<std::size_t> alternatives;
      for (std::size_t index = 0; index < grammar.productions.size(); ++index) {
        if (grammar.productions[index].lhs == symbol) {
          alternatives.push_back(index);
        }
      }
      for (std::size_t a = 0; a < alternatives.size() && !changed; ++a) {
        const auto& first = grammar.productions[alternatives[a]].rhs;
        if (first.empty()) {
          continue;
        }
        std::vector<std::size_t> group;
        for (const std::size_t other : alternatives) {
          const auto& rhs = grammar.productions[other].rhs;
          if (!rhs.empty() && rhs.front() == first.front()) {
            group.push_back(other);
          }
        }
        if (group.size() < 2) {
          continue;
        }
        std::size_t prefix = first.size();
        for (const std::size_t other : group) {
          const auto& rhs = grammar.productions[other].rhs;
          std::size_t common = 0;
          while (common < prefix && common < rhs.size() && rhs[common] == first[common]) {
            ++common;
          }
          prefix = common;
        }
        const std::vector<std::size_t> shared(first.begin(), first.begin() + static_cast<std::ptrdiff_t>(prefix));
        const std::size_t tail = add_symbol(grammar, fresh_name(grammar, grammar.names[symbol]), false);
        std::vector<Production> rewritten;
        bool emitted = false;
        for (std::size_t index = 0; index < grammar.productions.size(); ++index) {
          if (std::find(group.begin(), group.end(), index) == group.end()) {
            rewritten.push_back(grammar.productions[index]);
            continue;
          }
          if (!emitted) {
            Production head{symbol, shared, ""};
            head.rhs.push_back(tail);
            rewritten.push_back(std::move(head));
            emitted = true;
          }
        }
        for (const std::size_t index : group) {
          const auto& rhs = grammar.productions[index].rhs;
          rewritten.push_back(Production{tail, std::vector<std::size_t>(rhs.begin() + static_cast<std::ptrdiff_t>(prefix), rhs.end()), ""});
        }
        grammar.productions = std::move(rewritten);
        changed = true;
      }
    }
  }
  return grammar;
}

// ---------------------------------------------------------------------------------------------
// Tokens, trees and attributes
// ---------------------------------------------------------------------------------------------
std::optional<std::vector<Token>> tokenize(const Grammar& grammar, const std::string& input, std::string& error) {
  std::vector<Token> tokens;
  std::istringstream stream(input);
  std::string word;
  const auto num = grammar.find("num");
  const auto id = grammar.find("id");
  while (stream >> word) {
    const auto exact = grammar.find(word);
    if (exact.has_value() && grammar.terminal[*exact] && *exact != grammar.end_marker) {
      tokens.push_back({*exact, word, is_integer(word) ? std::stoll(word) : 0});
    } else if (is_integer(word) && num.has_value() && grammar.terminal[*num]) {
      tokens.push_back({*num, word, std::stoll(word)});
    } else if (is_identifier(word) && id.has_value() && grammar.terminal[*id]) {
      tokens.push_back({*id, word, 0});
    } else {
      error = "token '" + word + "' is not a terminal of the grammar";
      return std::nullopt;
    }
  }
  return tokens;
}

bool evaluate_attributes(const Grammar& grammar, ParseTree& tree, std::string& error) {
  if (!tree.production.has_value()) {
    return true;  // terminal leaf: value set by the scanner
  }
  std::vector<std::int64_t> values;
  for (auto& child : tree.children) {
    if (!evaluate_attributes(grammar, *child, error)) {
      return false;
    }
    values.push_back(child->value);
  }
  const auto& action = grammar.productions[*tree.production].action;
  if (action.empty()) {
    tree.value = values.empty() ? 0 : values.front();
    return true;
  }
  try {
    tree.value = ActionEvaluator(action, values).run();
  } catch (const std::string& message) {
    error = message;
    return false;
  }
  return true;
}

std::string print_tree(const Grammar& grammar, const ParseTree& tree) {
  std::ostringstream out;
  std::function<void(const ParseTree&, const std::string&, bool)> walk = [&](const ParseTree& node,
                                                                             const std::string& prefix, bool last) {
    out << prefix << (prefix.empty() ? "" : (last ? "`-- " : "|-- ")) << grammar.names[node.symbol];
    if (!node.production.has_value() && !node.text.empty()) {
      out << " '" << node.text << "'";
    }
    if (node.production.has_value()) {
      out << "  [val=" << node.value << "]";
    }
    out << '\n';
    const std::string child_prefix = prefix + (prefix.empty() ? "" : (last ? "    " : "|   "));
    for (std::size_t index = 0; index < node.children.size(); ++index) {
      walk(*node.children[index], child_prefix.empty() ? " " : child_prefix, index + 1 == node.children.size());
    }
  };
  walk(tree, "", true);
  return out.str();
}

// ---------------------------------------------------------------------------------------------
// LL(1)
// ---------------------------------------------------------------------------------------------
LL1Table build_ll1_table(const Grammar& grammar, const FirstFollow& sets) {
  LL1Table table;
  for (std::size_t index = 0; index < grammar.productions.size(); ++index) {
    const auto& production = grammar.productions[index];
    const auto [first, nullable] = first_of_sequence(grammar, sets, production.rhs, 0);
    std::set<std::size_t> lookaheads = first;
    if (nullable) {
      lookaheads.insert(sets.follow[production.lhs].begin(), sets.follow[production.lhs].end());
    }
    for (const std::size_t terminal : lookaheads) {
      table.cells[{production.lhs, terminal}].push_back(index);
    }
  }
  for (const auto& [cell, productions] : table.cells) {
    table.conflicts += productions.size() > 1 ? 1U : 0U;
  }
  return table;
}

std::string print_ll1_table(const Grammar& grammar, const LL1Table& table) {
  std::ostringstream out;
  out << "LL(1) table (" << (table.conflicts == 0 ? "conflict-free: the grammar is LL(1)"
                                                    : std::to_string(table.conflicts) + " conflicting cells: NOT LL(1)")
      << ")\n";
  for (const auto& [cell, productions] : table.cells) {
    out << "  M[" << grammar.names[cell.first] << ", " << grammar.names[cell.second] << "] =";
    for (const std::size_t production : productions) {
      out << "  " << grammar.production_text(production);
    }
    out << (productions.size() > 1 ? "   <-- conflict" : "") << '\n';
  }
  return out.str();
}

ParseOutcome ll1_parse(const Grammar& grammar, const LL1Table& table, const std::vector<Token>& tokens) {
  ParseOutcome outcome;
  outcome.tree = std::make_unique<ParseTree>();
  outcome.tree->symbol = grammar.start;
  std::vector<std::pair<std::size_t, ParseTree*>> stack{{grammar.end_marker, nullptr}, {grammar.start, outcome.tree.get()}};
  std::size_t position = 0;
  auto lookahead = [&]() { return position < tokens.size() ? tokens[position].symbol : grammar.end_marker; };
  auto snapshot = [&](const std::string& action) {
    if (outcome.steps.size() >= kMaxSteps) {
      return;
    }
    std::string stack_text;
    for (const auto& entry : stack) {
      stack_text += grammar.names[entry.first] + " ";
    }
    std::string input_text;
    for (std::size_t index = position; index < tokens.size(); ++index) {
      input_text += tokens[index].text + " ";
    }
    outcome.steps.push_back(stack_text + "| " + input_text + "$ | " + action);
  };
  while (true) {
    const auto [symbol, node] = stack.back();
    const std::size_t next = lookahead();
    if (symbol == grammar.end_marker) {
      if (next == grammar.end_marker) {
        snapshot("accept");
        outcome.accepted = true;
        break;
      }
      outcome.error = "unexpected '" + tokens[position].text + "' after a complete sentence";
      break;
    }
    if (grammar.terminal[symbol]) {
      if (symbol != next) {
        outcome.error = "expected '" + grammar.names[symbol] + "' but found '" +
                        (position < tokens.size() ? tokens[position].text : std::string("$")) + "'";
        break;
      }
      snapshot("match " + grammar.names[symbol]);
      node->text = tokens[position].text;
      node->value = tokens[position].value;
      stack.pop_back();
      ++position;
      continue;
    }
    const auto cell = table.cells.find({symbol, next});
    if (cell == table.cells.end()) {
      outcome.error = "no LL(1) rule for " + grammar.names[symbol] + " on '" + grammar.names[next] + "'";
      break;
    }
    const std::size_t production = cell->second.front();
    snapshot("expand " + grammar.production_text(production));
    node->production = production;
    stack.pop_back();
    for (const std::size_t rhs : grammar.productions[production].rhs) {
      auto child = std::make_unique<ParseTree>();
      child->symbol = rhs;
      node->children.push_back(std::move(child));
    }
    for (std::size_t index = node->children.size(); index-- > 0;) {
      stack.push_back({node->children[index]->symbol, node->children[index].get()});
    }
  }
  if (outcome.accepted) {
    std::string error;
    if (evaluate_attributes(grammar, *outcome.tree, error)) {
      outcome.value = outcome.tree->value;
    } else {
      outcome.error = error;
    }
  }
  return outcome;
}

// ---------------------------------------------------------------------------------------------
// LR(0), SLR(1), canonical LR(1), LALR(1)
// ---------------------------------------------------------------------------------------------
namespace {

using Core = std::pair<std::size_t, std::size_t>;          // (production, dot)
using ItemSet = std::map<Core, std::set<std::size_t>>;     // core -> lookaheads (empty for LR(0))

Grammar augment(const Grammar& grammar) {
  Grammar augmented = grammar;
  const std::size_t start = add_symbol(augmented, fresh_name(augmented, grammar.names[grammar.start]), false);
  augmented.productions.insert(augmented.productions.begin(), Production{start, {grammar.start}, "$$ = $1"});
  augmented.start = start;
  return augmented;
}

ItemSet closure(const Grammar& grammar, const FirstFollow& sets, ItemSet items, bool with_lookahead) {
  std::deque<Core> work;
  for (const auto& [core, lookaheads] : items) {
    work.push_back(core);
  }
  while (!work.empty()) {
    const Core core = work.front();
    work.pop_front();
    const auto& production = grammar.productions[core.first];
    if (core.second >= production.rhs.size()) {
      continue;
    }
    const std::size_t next = production.rhs[core.second];
    if (grammar.terminal[next]) {
      continue;
    }
    std::set<std::size_t> lookaheads;
    if (with_lookahead) {
      const auto [first, nullable] = first_of_sequence(grammar, sets, production.rhs, core.second + 1);
      lookaheads = first;
      if (nullable) {
        lookaheads.insert(items[core].begin(), items[core].end());
      }
    }
    for (std::size_t index = 0; index < grammar.productions.size(); ++index) {
      if (grammar.productions[index].lhs != next) {
        continue;
      }
      const Core added{index, 0};
      auto [entry, inserted] = items.try_emplace(added);
      const std::size_t before = entry->second.size();
      entry->second.insert(lookaheads.begin(), lookaheads.end());
      if (inserted || entry->second.size() != before) {
        work.push_back(added);
      }
    }
  }
  return items;
}

ItemSet go_to_set(const Grammar& grammar, const FirstFollow& sets, const ItemSet& items, std::size_t symbol, bool with_lookahead) {
  ItemSet kernel;
  for (const auto& [core, lookaheads] : items) {
    const auto& rhs = grammar.productions[core.first].rhs;
    if (core.second < rhs.size() && rhs[core.second] == symbol) {
      kernel[{core.first, core.second + 1}] = lookaheads;
    }
  }
  return kernel.empty() ? kernel : closure(grammar, sets, std::move(kernel), with_lookahead);
}

struct Collection {
  std::vector<ItemSet> states;
  std::map<std::pair<std::size_t, std::size_t>, std::size_t> transitions;
};

Collection canonical_collection(const Grammar& grammar, const FirstFollow& sets, bool with_lookahead) {
  Collection collection;
  ItemSet start;
  start[{0, 0}] = with_lookahead ? std::set<std::size_t>{grammar.end_marker} : std::set<std::size_t>{};
  collection.states.push_back(closure(grammar, sets, std::move(start), with_lookahead));
  std::map<ItemSet, std::size_t> index{{collection.states.front(), 0}};
  for (std::size_t state = 0; state < collection.states.size(); ++state) {
    std::set<std::size_t> symbols;
    for (const auto& [core, lookaheads] : collection.states[state]) {
      const auto& rhs = grammar.productions[core.first].rhs;
      if (core.second < rhs.size()) {
        symbols.insert(rhs[core.second]);
      }
    }
    for (const std::size_t symbol : symbols) {
      ItemSet next = go_to_set(grammar, sets, collection.states[state], symbol, with_lookahead);
      auto [found, inserted] = index.try_emplace(next, collection.states.size());
      if (inserted) {
        collection.states.push_back(std::move(next));
      }
      collection.transitions[{state, symbol}] = found->second;
    }
  }
  return collection;
}

// LALR(1): merge canonical LR(1) states that share the same LR(0) core.
Collection merge_cores(const Collection& lr1) {
  Collection merged;
  std::map<std::set<Core>, std::size_t> by_core;
  std::vector<std::size_t> mapping(lr1.states.size());
  for (std::size_t state = 0; state < lr1.states.size(); ++state) {
    std::set<Core> cores;
    for (const auto& [core, lookaheads] : lr1.states[state]) {
      cores.insert(core);
    }
    auto [found, inserted] = by_core.try_emplace(cores, merged.states.size());
    if (inserted) {
      merged.states.push_back(lr1.states[state]);
    } else {
      for (const auto& [core, lookaheads] : lr1.states[state]) {
        merged.states[found->second][core].insert(lookaheads.begin(), lookaheads.end());
      }
    }
    mapping[state] = found->second;
  }
  for (const auto& [edge, target] : lr1.transitions) {
    merged.transitions[{mapping[edge.first], edge.second}] = mapping[target];
  }
  return merged;
}

std::string item_text(const Grammar& grammar, const Core& core, const std::set<std::size_t>& lookaheads) {
  const auto& production = grammar.productions[core.first];
  std::string text = grammar.names[production.lhs] + " ->";
  for (std::size_t index = 0; index <= production.rhs.size(); ++index) {
    if (index == core.second) {
      text += " .";
    }
    if (index < production.rhs.size()) {
      text += " " + grammar.names[production.rhs[index]];
    }
  }
  if (!lookaheads.empty()) {
    text += " , ";
    bool first = true;
    for (const std::size_t lookahead : lookaheads) {
      text += (first ? "" : "/") + grammar.names[lookahead];
      first = false;
    }
  }
  return text;
}

}  // namespace

std::string_view lr_kind_name(LrKind kind) {
  switch (kind) {
    case LrKind::Lr0:
      return "LR(0)";
    case LrKind::Slr1:
      return "SLR(1)";
    case LrKind::Lr1:
      return "LR(1)";
    case LrKind::Lalr1:
      return "LALR(1)";
  }
  return "LR";
}

LrTable build_lr_table(const Grammar& grammar, LrKind kind) {
  LrTable table;
  table.kind = kind;
  table.augmented = augment(grammar);
  const Grammar& g = table.augmented;
  const FirstFollow sets = compute_first_follow(g);
  const bool lookahead = kind == LrKind::Lr1 || kind == LrKind::Lalr1;
  Collection collection = canonical_collection(g, sets, lookahead);
  if (kind == LrKind::Lalr1) {
    collection = merge_cores(collection);
  }

  for (std::size_t state = 0; state < collection.states.size(); ++state) {
    std::vector<std::string> items;
    for (const auto& [core, lookaheads] : collection.states[state]) {
      items.push_back(item_text(g, core, lookaheads));
      const auto& production = g.productions[core.first];
      if (core.second < production.rhs.size()) {
        const std::size_t symbol = production.rhs[core.second];
        if (g.terminal[symbol]) {
          table.action[{state, symbol}].insert({ActionKind::Shift, collection.transitions.at({state, symbol})});
        }
        continue;
      }
      if (core.first == 0) {
        table.action[{state, g.end_marker}].insert({ActionKind::Accept, 0});
        continue;
      }
      std::set<std::size_t> reduce_on;
      switch (kind) {
        case LrKind::Lr0:
          for (std::size_t symbol = 0; symbol < g.names.size(); ++symbol) {
            if (g.terminal[symbol]) {
              reduce_on.insert(symbol);
            }
          }
          break;
        case LrKind::Slr1:
          reduce_on = sets.follow[production.lhs];
          break;
        case LrKind::Lr1:
        case LrKind::Lalr1:
          reduce_on = lookaheads;
          break;
      }
      for (const std::size_t symbol : reduce_on) {
        table.action[{state, symbol}].insert({ActionKind::Reduce, core.first});
      }
    }
    table.state_items.push_back(std::move(items));
  }
  for (const auto& [edge, target] : collection.transitions) {
    if (!g.terminal[edge.second]) {
      table.go_to[edge] = target;
    }
  }
  for (const auto& [cell, actions] : table.action) {
    std::size_t shifts = 0;
    std::size_t reduces = 0;
    for (const auto& action : actions) {
      shifts += action.kind == ActionKind::Shift ? 1U : 0U;
      reduces += action.kind == ActionKind::Reduce ? 1U : 0U;
    }
    table.shift_reduce_conflicts += shifts > 0 && reduces > 0 ? 1U : 0U;
    table.reduce_reduce_conflicts += reduces > 1 ? 1U : 0U;
  }
  return table;
}

std::string print_lr_table(const LrTable& table, bool with_items) {
  const Grammar& g = table.augmented;
  std::ostringstream out;
  out << lr_kind_name(table.kind) << " automaton: " << table.states() << " states, " << table.shift_reduce_conflicts
      << " shift/reduce and " << table.reduce_reduce_conflicts << " reduce/reduce conflicts"
      << (table.conflict_free() ? " -> the grammar is " + std::string(lr_kind_name(table.kind)) : "") << '\n';
  for (std::size_t index = 0; index < g.productions.size(); ++index) {
    out << "  (" << index << ") " << g.production_text(index) << '\n';
  }
  for (std::size_t state = 0; state < table.states(); ++state) {
    out << "state " << state << ':';
    for (const auto& [cell, actions] : table.action) {
      if (cell.first != state) {
        continue;
      }
      out << "  " << g.names[cell.second] << ':';
      bool first = true;
      for (const auto& action : actions) {
        out << (first ? "" : "/");
        first = false;
        switch (action.kind) {
          case ActionKind::Shift:
            out << 's' << action.target;
            break;
          case ActionKind::Reduce:
            out << 'r' << action.target;
            break;
          case ActionKind::Accept:
            out << "acc";
            break;
        }
      }
      if (actions.size() > 1) {
        out << '!';
      }
    }
    for (const auto& [edge, target] : table.go_to) {
      if (edge.first == state) {
        out << "  " << g.names[edge.second] << "=>" << target;
      }
    }
    out << '\n';
    if (with_items) {
      for (const auto& item : table.state_items[state]) {
        out << "      " << item << '\n';
      }
    }
  }
  return out.str();
}

ParseOutcome lr_parse(const LrTable& table, const std::vector<Token>& tokens) {
  const Grammar& g = table.augmented;
  ParseOutcome outcome;
  std::vector<std::size_t> states{0};
  std::vector<std::unique_ptr<ParseTree>> nodes;
  std::size_t position = 0;
  auto snapshot = [&](const std::string& action) {
    if (outcome.steps.size() >= kMaxSteps) {
      return;
    }
    std::string stack_text;
    for (std::size_t index = 0; index < states.size(); ++index) {
      if (index > 0) {
        stack_text += g.names[nodes[index - 1]->symbol] + " ";
      }
      stack_text += std::to_string(states[index]) + " ";
    }
    std::string input_text;
    for (std::size_t index = position; index < tokens.size(); ++index) {
      input_text += tokens[index].text + " ";
    }
    outcome.steps.push_back(stack_text + "| " + input_text + "$ | " + action);
  };
  while (true) {
    const std::size_t next = position < tokens.size() ? tokens[position].symbol : g.end_marker;
    const auto cell = table.action.find({states.back(), next});
    if (cell == table.action.end()) {
      outcome.error = "syntax error at '" + (position < tokens.size() ? tokens[position].text : std::string("$")) +
                      "' in state " + std::to_string(states.back());
      break;
    }
    // yacc-style resolution: shift wins, then the earliest production
    LrAction action = *cell->second.begin();
    for (const auto& candidate : cell->second) {
      if (candidate.kind == ActionKind::Shift) {
        action = candidate;
      }
    }
    if (action.kind == ActionKind::Accept) {
      snapshot("accept");
      outcome.accepted = true;
      break;
    }
    if (action.kind == ActionKind::Shift) {
      snapshot("shift " + std::to_string(action.target));
      auto leaf = std::make_unique<ParseTree>();
      leaf->symbol = next;
      leaf->text = tokens[position].text;
      leaf->value = tokens[position].value;
      nodes.push_back(std::move(leaf));
      states.push_back(action.target);
      ++position;
      continue;
    }
    const auto& production = g.productions[action.target];
    snapshot("reduce " + g.production_text(action.target));
    auto node = std::make_unique<ParseTree>();
    node->symbol = production.lhs;
    node->production = action.target;
    const std::size_t length = production.rhs.size();
    for (std::size_t index = nodes.size() - length; index < nodes.size(); ++index) {
      node->children.push_back(std::move(nodes[index]));
    }
    nodes.resize(nodes.size() - length);
    states.resize(states.size() - length);
    const auto target = table.go_to.find({states.back(), production.lhs});
    if (target == table.go_to.end()) {
      outcome.error = "missing goto entry (internal error)";
      break;
    }
    nodes.push_back(std::move(node));
    states.push_back(target->second);
  }
  if (outcome.accepted && nodes.size() == 1) {
    outcome.tree = std::move(nodes.front());
    std::string error;
    if (evaluate_attributes(g, *outcome.tree, error)) {
      outcome.value = outcome.tree->value;
    } else {
      outcome.error = error;
    }
  }
  return outcome;
}

// ---------------------------------------------------------------------------------------------
// Earley
// ---------------------------------------------------------------------------------------------
EarleyOutcome earley_parse(const Grammar& grammar, const std::vector<Token>& tokens) {
  using Item = std::tuple<std::size_t, std::size_t, std::size_t>;  // production, dot, origin
  const FirstFollow sets = compute_first_follow(grammar);
  const std::size_t n = tokens.size();
  std::vector<std::vector<Item>> chart(n + 1);
  std::vector<std::set<Item>> seen(n + 1);
  auto add = [&](std::size_t set, const Item& item) {
    if (seen[set].insert(item).second) {
      chart[set].push_back(item);
    }
  };
  for (std::size_t index = 0; index < grammar.productions.size(); ++index) {
    if (grammar.productions[index].lhs == grammar.start) {
      add(0, {index, 0, 0});
    }
  }
  for (std::size_t k = 0; k <= n; ++k) {
    for (std::size_t cursor = 0; cursor < chart[k].size(); ++cursor) {
      const auto [production, dot, origin] = chart[k][cursor];
      const auto& rhs = grammar.productions[production].rhs;
      if (dot == rhs.size()) {  // complete
        const std::size_t lhs = grammar.productions[production].lhs;
        for (std::size_t index = 0; index < chart[origin].size(); ++index) {
          const auto [p, d, o] = chart[origin][index];
          const auto& prhs = grammar.productions[p].rhs;
          if (d < prhs.size() && prhs[d] == lhs) {
            add(k, {p, d + 1, o});
          }
        }
        continue;
      }
      const std::size_t next = rhs[dot];
      if (grammar.terminal[next]) {  // scan
        if (k < n && tokens[k].symbol == next) {
          add(k + 1, {production, dot + 1, origin});
        }
        continue;
      }
      for (std::size_t index = 0; index < grammar.productions.size(); ++index) {  // predict
        if (grammar.productions[index].lhs == next) {
          add(k, {index, 0, k});
        }
      }
      if (sets.nullable[next]) {  // Aycock-Horspool: nullable symbols complete immediately
        add(k, {production, dot + 1, origin});
      }
    }
  }

  EarleyOutcome outcome;
  for (std::size_t k = 0; k <= n; ++k) {
    outcome.chart_items += chart[k].size();
    std::ostringstream line;
    line << "S" << k << ":";
    for (const auto& [production, dot, origin] : chart[k]) {
      std::string text = grammar.names[grammar.productions[production].lhs] + "->";
      const auto& rhs = grammar.productions[production].rhs;
      for (std::size_t index = 0; index <= rhs.size(); ++index) {
        text += index == dot ? "." : "";
        text += index < rhs.size() ? grammar.names[rhs[index]] + (index + 1 < rhs.size() && index + 1 != dot ? " " : "") : "";
      }
      line << "  [" << text << "," << origin << "]";
    }
    outcome.sets.push_back(line.str());
  }
  for (const auto& [production, dot, origin] : chart[n]) {
    if (origin == 0 && grammar.productions[production].lhs == grammar.start &&
        dot == grammar.productions[production].rhs.size()) {
      outcome.accepted = true;
    }
  }
  if (!outcome.accepted) {
    outcome.parse_trees = 0;
    return outcome;
  }

  // Count derivations with memoised recursion over the chart.
  bool infinite = false;
  std::map<std::tuple<std::size_t, std::size_t, std::size_t, std::size_t>, std::uint64_t> sequence_memo;
  std::map<std::tuple<std::size_t, std::size_t, std::size_t>, std::uint64_t> symbol_memo;
  std::set<std::tuple<std::size_t, std::size_t, std::size_t>> active;
  std::function<std::uint64_t(std::size_t, std::size_t, std::size_t)> count_symbol;
  std::function<std::uint64_t(std::size_t, std::size_t, std::size_t, std::size_t)> count_sequence;
  count_sequence = [&](std::size_t production, std::size_t length, std::size_t from, std::size_t to) -> std::uint64_t {
    if (length == 0) {
      return from == to ? 1U : 0U;
    }
    if (!seen[to].contains({production, length, from})) {
      return 0;
    }
    const auto key = std::make_tuple(production, length, from, to);
    if (const auto found = sequence_memo.find(key); found != sequence_memo.end()) {
      return found->second;
    }
    const std::size_t symbol = grammar.productions[production].rhs[length - 1];
    std::uint64_t total = 0;
    for (std::size_t middle = from; middle <= to; ++middle) {
      if (length - 1 > 0 && !seen[middle].contains({production, length - 1, from})) {
        continue;
      }
      if (length - 1 == 0 && middle != from) {
        continue;
      }
      std::uint64_t ways = 0;
      if (grammar.terminal[symbol]) {
        ways = (to == middle + 1 && tokens[middle].symbol == symbol) ? 1U : 0U;
      } else {
        ways = count_symbol(symbol, middle, to);
      }
      total = saturating_add(total, saturating_mul(count_sequence(production, length - 1, from, middle), ways));
    }
    sequence_memo[key] = total;
    return total;
  };
  count_symbol = [&](std::size_t symbol, std::size_t from, std::size_t to) -> std::uint64_t {
    const auto key = std::make_tuple(symbol, from, to);
    if (const auto found = symbol_memo.find(key); found != symbol_memo.end()) {
      return found->second;
    }
    if (active.contains(key)) {
      infinite = true;
      return 0;
    }
    active.insert(key);
    std::uint64_t total = 0;
    for (std::size_t production = 0; production < grammar.productions.size(); ++production) {
      if (grammar.productions[production].lhs != symbol) {
        continue;
      }
      const std::size_t length = grammar.productions[production].rhs.size();
      if (length == 0) {
        total = saturating_add(total, from == to ? 1U : 0U);
        continue;
      }
      total = saturating_add(total, count_sequence(production, length, from, to));
    }
    active.erase(key);
    symbol_memo[key] = total;
    return total;
  };
  const std::uint64_t trees = count_symbol(grammar.start, 0, n);
  if (!infinite) {
    outcome.parse_trees = trees;
  }
  return outcome;
}

}  // namespace nexus::compiler::formal
