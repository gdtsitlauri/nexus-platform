#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace nexus::compiler::formal {

// Context-free grammars and the parsing algorithms of a compilers course.
//
// Grammar file format:
//   # comment
//   %start E                       (optional; defaults to the first left-hand side)
//   E -> E + T { $$ = $1 + $3 }    (an optional S-attributed action per alternative)
//      | T
//      ;
//   F -> ( E ) { $$ = $2 } | num | %empty ;
// Symbols that never appear on a left-hand side are terminals.  'quoted' symbols are always
// terminals (use them for '|', ';', '->', ...).  Actions compute a synthesized integer attribute
// from $1..$n with + - * / % and parentheses; the default action is $$ = $1.
// Input tokens are whitespace separated; an integer token matches the terminal `num` and an
// identifier token that is not itself a terminal matches `id`.

struct Production {
  std::size_t lhs = 0;
  std::vector<std::size_t> rhs;
  std::string action;
};

struct Grammar {
  std::vector<std::string> names;
  std::vector<bool> terminal;
  std::vector<Production> productions;
  std::size_t start = 0;
  std::size_t end_marker = 0;  // the terminal "$"

  [[nodiscard]] std::optional<std::size_t> find(const std::string& name) const;
  [[nodiscard]] std::string production_text(std::size_t index) const;
};

struct GrammarResult {
  std::optional<Grammar> grammar;
  std::string error;
};

GrammarResult parse_grammar(const std::string& text);
std::string print_grammar(const Grammar& grammar);

// ---- FIRST / FOLLOW -----------------------------------------------------------------------
struct FirstFollow {
  std::vector<bool> nullable;
  std::vector<std::set<std::size_t>> first;
  std::vector<std::set<std::size_t>> follow;
};

FirstFollow compute_first_follow(const Grammar& grammar);
std::string print_first_follow(const Grammar& grammar, const FirstFollow& sets);

// ---- grammar transformations --------------------------------------------------------------
Grammar eliminate_left_recursion(const Grammar& grammar);
Grammar left_factor(const Grammar& grammar);

// ---- parse trees, tokens and attribute evaluation ------------------------------------------
struct Token {
  std::size_t symbol = 0;
  std::string text;
  std::int64_t value = 0;
};

struct ParseTree {
  std::size_t symbol = 0;
  std::optional<std::size_t> production;
  std::string text;
  std::int64_t value = 0;
  std::vector<std::unique_ptr<ParseTree>> children;
};

struct ParseOutcome {
  bool accepted = false;
  std::string error;
  std::vector<std::string> steps;
  std::unique_ptr<ParseTree> tree;
  std::optional<std::int64_t> value;  // synthesized attribute of the start symbol
};

std::optional<std::vector<Token>> tokenize(const Grammar& grammar, const std::string& input, std::string& error);
// Evaluates the S-attributed actions bottom-up; returns false on a malformed action.
bool evaluate_attributes(const Grammar& grammar, ParseTree& tree, std::string& error);
std::string print_tree(const Grammar& grammar, const ParseTree& tree);

// ---- LL(1) ---------------------------------------------------------------------------------
struct LL1Table {
  std::map<std::pair<std::size_t, std::size_t>, std::vector<std::size_t>> cells;  // (A, a) -> productions
  std::size_t conflicts = 0;
};

LL1Table build_ll1_table(const Grammar& grammar, const FirstFollow& sets);
std::string print_ll1_table(const Grammar& grammar, const LL1Table& table);
ParseOutcome ll1_parse(const Grammar& grammar, const LL1Table& table, const std::vector<Token>& tokens);

// ---- LR family -----------------------------------------------------------------------------
enum class LrKind {
  Lr0,
  Slr1,
  Lr1,
  Lalr1,
};

enum class ActionKind {
  Shift,
  Reduce,
  Accept,
};

struct LrAction {
  ActionKind kind = ActionKind::Shift;
  std::size_t target = 0;  // state for shift, production for reduce

  bool operator<(const LrAction& other) const {
    return kind != other.kind ? kind < other.kind : target < other.target;
  }
};

struct LrTable {
  LrKind kind = LrKind::Slr1;
  // Production 0 is the augmented S' -> S (the parser adds it).
  Grammar augmented;
  std::vector<std::vector<std::string>> state_items;
  std::map<std::pair<std::size_t, std::size_t>, std::set<LrAction>> action;  // (state, terminal)
  std::map<std::pair<std::size_t, std::size_t>, std::size_t> go_to;          // (state, nonterminal)
  std::size_t shift_reduce_conflicts = 0;
  std::size_t reduce_reduce_conflicts = 0;

  [[nodiscard]] std::size_t states() const { return state_items.size(); }
  [[nodiscard]] bool conflict_free() const { return shift_reduce_conflicts == 0 && reduce_reduce_conflicts == 0; }
};

std::string_view lr_kind_name(LrKind kind);
LrTable build_lr_table(const Grammar& grammar, LrKind kind);
std::string print_lr_table(const LrTable& table, bool with_items);
// Conflicts are resolved like yacc: shift over reduce, earlier production over later.
ParseOutcome lr_parse(const LrTable& table, const std::vector<Token>& tokens);

// ---- Earley (generalized parsing, handles ambiguity) ----------------------------------------
struct EarleyOutcome {
  bool accepted = false;
  std::size_t chart_items = 0;
  // Number of distinct parse trees (saturates at 10^18); nullopt when a derivation cycle makes it infinite.
  std::optional<std::uint64_t> parse_trees;
  std::vector<std::string> sets;
};

EarleyOutcome earley_parse(const Grammar& grammar, const std::vector<Token>& tokens);

}  // namespace nexus::compiler::formal
