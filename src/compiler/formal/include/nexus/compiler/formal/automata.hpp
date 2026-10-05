#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace nexus::compiler::formal {

// Regular expressions -> Thompson NFA -> subset-construction DFA -> minimal DFA.
//
// Supported syntax: literals, escapes (\x), '.', character classes [abc] [a-z] [^0-9],
// grouping ( ), alternation |, and the postfix operators * + ?.
// '.' and negated classes range over printable ASCII (32..126).

struct NfaState {
  std::vector<std::size_t> epsilon;
  std::vector<std::pair<std::set<char>, std::size_t>> edges;
};

struct Nfa {
  std::vector<NfaState> states;
  std::size_t start = 0;
  std::size_t accept = 0;
};

struct Dfa {
  std::vector<std::map<char, std::size_t>> transitions;  // missing entry = dead state
  std::vector<bool> accepting;
  std::size_t start = 0;
  std::vector<char> alphabet;

  [[nodiscard]] bool matches(const std::string& text) const;
  [[nodiscard]] std::size_t size() const { return transitions.size(); }
};

struct RegexResult {
  std::optional<Nfa> nfa;
  std::string error;
};

RegexResult regex_to_nfa(const std::string& pattern);
bool nfa_matches(const Nfa& nfa, const std::string& text);
Dfa subset_construction(const Nfa& nfa);
// Moore partition refinement; unreachable states are dropped first.
Dfa minimize(const Dfa& dfa);

std::string print_nfa(const Nfa& nfa);
std::string print_dfa(const Dfa& dfa, const std::string& title);

}  // namespace nexus::compiler::formal
