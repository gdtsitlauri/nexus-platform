#include <fstream>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>
#include <string>

#include "nexus/compiler/formal/automata.hpp"
#include "nexus/compiler/formal/grammar.hpp"

namespace formal = nexus::compiler::formal;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

formal::Grammar load(const std::string& repo, const std::string& name) {
  std::ifstream input(repo + "/examples/grammars/" + name);
  std::stringstream buffer;
  buffer << input.rdbuf();
  auto parsed = formal::parse_grammar(buffer.str());
  if (!parsed.grammar.has_value()) {
    std::cerr << "cannot load " << name << ": " << parsed.error << '\n';
    std::exit(1);
  }
  return *parsed.grammar;
}

std::vector<formal::Token> tokens(const formal::Grammar& grammar, const std::string& text) {
  std::string error;
  auto result = formal::tokenize(grammar, text, error);
  expect(result.has_value(), "tokenize '" + text + "': " + error);
  return result.value_or(std::vector<formal::Token>{});
}

void regex_agrees_with_std_regex() {
  const std::vector<std::string> patterns = {
      "(a|b)*abb", "a(b|c)*d?", "(ab|a)(bc|c)", "[a-c]+b*", "((a|b)(a|b))*", "a?b?c?", "(a*b*)*c", "[^a]b|ab+",
  };
  std::mt19937 random(12345);
  const std::string alphabet = "abcd";
  for (const auto& pattern : patterns) {
    const auto nfa = formal::regex_to_nfa(pattern);
    expect(nfa.nfa.has_value(), "regex parse " + pattern);
    if (!nfa.nfa.has_value()) {
      continue;
    }
    const auto dfa = formal::subset_construction(*nfa.nfa);
    const auto minimal = formal::minimize(dfa);
    expect(minimal.size() <= dfa.size(), "minimisation must not grow the DFA for " + pattern);
    const std::regex reference(pattern);
    for (int sample = 0; sample < 400; ++sample) {
      std::string text;
      const int length = static_cast<int>(random() % 7U);
      for (int index = 0; index < length; ++index) {
        text += alphabet[random() % alphabet.size()];
      }
      const bool expected = std::regex_match(text, reference);
      if (formal::nfa_matches(*nfa.nfa, text) != expected || dfa.matches(text) != expected ||
          minimal.matches(text) != expected) {
        expect(false, "regex " + pattern + " disagrees with std::regex on '" + text + "'");
        break;
      }
    }
  }
  const auto classic = formal::regex_to_nfa("(a|b)*abb");
  const auto classic_dfa = formal::subset_construction(*classic.nfa);
  expect(classic_dfa.size() == 5 && formal::minimize(classic_dfa).size() == 4,
         "(a|b)*abb: subset construction gives 5 states and the minimal DFA 4 (Dragon book 3.36/3.40)");
  expect(!formal::regex_to_nfa("(ab").nfa.has_value(), "unbalanced parenthesis must be rejected");
}

void grammar_classes(const std::string& repo) {
  const auto expr = load(repo, "expr.g");
  const auto sets = formal::compute_first_follow(expr);
  expect(formal::build_ll1_table(expr, sets).conflicts > 0, "left-recursive expr.g is not LL(1)");
  expect(!formal::build_lr_table(expr, formal::LrKind::Lr0).conflict_free(), "expr.g is not LR(0)");
  for (const auto kind : {formal::LrKind::Slr1, formal::LrKind::Lalr1, formal::LrKind::Lr1}) {
    const auto table = formal::build_lr_table(expr, kind);
    expect(table.conflict_free(), std::string("expr.g must be ") + std::string(formal::lr_kind_name(kind)));
    const auto outcome = formal::lr_parse(table, tokens(expr, "2 + 3 * ( 4 - 1 ) / - 3"));
    expect(outcome.accepted && outcome.value == -1, "LR parse evaluates 2 + 3*(4-1)/-3 = -1");
  }
  const auto transformed = formal::left_factor(formal::eliminate_left_recursion(expr));
  expect(formal::build_ll1_table(transformed, formal::compute_first_follow(transformed)).conflicts == 0,
         "expr.g becomes LL(1) after left-recursion elimination");

  const auto ll1 = load(repo, "expr_ll1.g");
  const auto ll1_sets = formal::compute_first_follow(ll1);
  const auto ll1_table = formal::build_ll1_table(ll1, ll1_sets);
  expect(ll1_table.conflicts == 0, "expr_ll1.g is LL(1)");
  const auto ll1_outcome = formal::ll1_parse(ll1, ll1_table, tokens(ll1, "2 + 3 * 4 + ( 1 + 1 ) * 5"));
  expect(ll1_outcome.accepted && ll1_outcome.value == 24, "LL(1) parse evaluates 2+3*4+(1+1)*5 = 24");
  expect(!formal::ll1_parse(ll1, ll1_table, tokens(ll1, "2 + * 3")).accepted, "LL(1) rejects '2 + * 3'");
  expect(ll1_sets.nullable[*ll1.find("E2")] && !ll1_sets.nullable[*ll1.find("E")], "nullable sets");

  const auto lvalue = load(repo, "lvalue.g");
  const auto slr = formal::build_lr_table(lvalue, formal::LrKind::Slr1);
  const auto lalr = formal::build_lr_table(lvalue, formal::LrKind::Lalr1);
  const auto lr1 = formal::build_lr_table(lvalue, formal::LrKind::Lr1);
  expect(slr.shift_reduce_conflicts == 1, "grammar 4.49 has one SLR(1) shift/reduce conflict");
  expect(lalr.conflict_free() && lr1.conflict_free(), "grammar 4.49 is LALR(1) and LR(1)");
  expect(lalr.states() == 10 && lr1.states() == 14, "grammar 4.49: 10 LALR(1) states vs 14 LR(1) states");
  expect(formal::lr_parse(lalr, tokens(lvalue, "* id = id")).accepted, "LALR(1) accepts '* id = id'");

  const auto ambiguous = load(repo, "ambiguous.g");
  expect(!formal::build_lr_table(ambiguous, formal::LrKind::Lr1).conflict_free(), "an ambiguous grammar is not LR(1)");
  const auto two = formal::earley_parse(ambiguous, tokens(ambiguous, "1 + 2 * 3"));
  expect(two.accepted && two.parse_trees == 2U, "Earley: '1 + 2 * 3' has 2 parse trees");
  const auto catalan = formal::earley_parse(ambiguous, tokens(ambiguous, "1 + 2 + 3 + 4 + 5 + 6"));
  expect(catalan.accepted && catalan.parse_trees == 42U, "Earley: 6 operands give Catalan(5) = 42 trees");
  expect(!formal::earley_parse(ambiguous, tokens(ambiguous, "1 + + 2")).accepted, "Earley rejects '1 + + 2'");

  const auto dangling = load(repo, "dangling_else.g");
  const auto dangling_table = formal::build_lr_table(dangling, formal::LrKind::Lalr1);
  expect(dangling_table.shift_reduce_conflicts == 1, "dangling else: exactly one shift/reduce conflict");
  expect(formal::lr_parse(dangling_table, tokens(dangling, "if c then if c then other else other")).accepted,
         "shift-preferred resolution parses the dangling else");
  expect(formal::earley_parse(dangling, tokens(dangling, "if c then if c then other else other")).parse_trees == 2U,
         "the dangling-else sentence has exactly 2 parse trees");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: formal_test <repo-root>\n";
    return 1;
  }
  regex_agrees_with_std_regex();
  grammar_classes(argv[1]);
  if (failures == 0) {
    std::cout << "formal_test: all checks passed\n";
  }
  return failures == 0 ? 0 : 1;
}
