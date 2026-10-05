#include "nexus/compiler/formal/automata.hpp"

#include <algorithm>
#include <deque>
#include <sstream>

namespace nexus::compiler::formal {

namespace {

constexpr char kFirstPrintable = 32;
constexpr char kLastPrintable = 126;

std::set<char> printable() {
  std::set<char> chars;
  for (char ch = kFirstPrintable; ch <= kLastPrintable; ++ch) {
    chars.insert(ch);
    if (ch == kLastPrintable) {
      break;
    }
  }
  return chars;
}

struct Fragment {
  std::size_t start = 0;
  std::size_t accept = 0;
};

// Recursive-descent regex parser that builds the Thompson NFA directly.
//   alternation  := concat ('|' concat)*
//   concat       := repeat+            (empty concat = epsilon)
//   repeat       := atom ('*' | '+' | '?')*
//   atom         := literal | '.' | class | '(' alternation ')'
class RegexParser {
 public:
  explicit RegexParser(const std::string& pattern) : pattern_(pattern) {}

  RegexResult parse() {
    RegexResult result;
    try {
      const Fragment fragment = alternation();
      if (position_ != pattern_.size()) {
        throw std::string("unexpected '") + pattern_[position_] + "' at position " + std::to_string(position_);
      }
      nfa_.start = fragment.start;
      nfa_.accept = fragment.accept;
      result.nfa = std::move(nfa_);
    } catch (const std::string& error) {
      result.error = error;
    }
    return result;
  }

 private:
  std::size_t new_state() {
    nfa_.states.emplace_back();
    return nfa_.states.size() - 1;
  }

  Fragment symbol(std::set<char> chars) {
    const std::size_t start = new_state();
    const std::size_t accept = new_state();
    nfa_.states[start].edges.push_back({std::move(chars), accept});
    return {start, accept};
  }

  Fragment epsilon() {
    const std::size_t start = new_state();
    const std::size_t accept = new_state();
    nfa_.states[start].epsilon.push_back(accept);
    return {start, accept};
  }

  [[nodiscard]] bool at_end() const { return position_ >= pattern_.size(); }
  [[nodiscard]] char peek() const { return pattern_[position_]; }

  Fragment alternation() {
    Fragment left = concatenation();
    while (!at_end() && peek() == '|') {
      ++position_;
      const Fragment right = concatenation();
      const std::size_t start = new_state();
      const std::size_t accept = new_state();
      nfa_.states[start].epsilon = {left.start, right.start};
      nfa_.states[left.accept].epsilon.push_back(accept);
      nfa_.states[right.accept].epsilon.push_back(accept);
      left = {start, accept};
    }
    return left;
  }

  Fragment concatenation() {
    std::optional<Fragment> result;
    while (!at_end() && peek() != '|' && peek() != ')') {
      const Fragment next = repetition();
      if (!result.has_value()) {
        result = next;
      } else {
        nfa_.states[result->accept].epsilon.push_back(next.start);
        result->accept = next.accept;
      }
    }
    return result.value_or(epsilon());
  }

  Fragment repetition() {
    Fragment fragment = atom();
    while (!at_end() && (peek() == '*' || peek() == '+' || peek() == '?')) {
      const char op = pattern_[position_++];
      const std::size_t start = new_state();
      const std::size_t accept = new_state();
      nfa_.states[start].epsilon.push_back(fragment.start);
      nfa_.states[fragment.accept].epsilon.push_back(accept);
      if (op == '*' || op == '?') {
        nfa_.states[start].epsilon.push_back(accept);
      }
      if (op == '*' || op == '+') {
        nfa_.states[fragment.accept].epsilon.push_back(fragment.start);
      }
      fragment = {start, accept};
    }
    return fragment;
  }

  char escaped() {
    if (at_end()) {
      throw std::string("dangling escape at end of pattern");
    }
    const char ch = pattern_[position_++];
    switch (ch) {
      case 'n':
        return '\n';
      case 't':
        return '\t';
      default:
        return ch;
    }
  }

  Fragment atom() {
    if (at_end()) {
      throw std::string("unexpected end of pattern");
    }
    const char ch = pattern_[position_++];
    switch (ch) {
      case '(': {
        const Fragment inner = alternation();
        if (at_end() || peek() != ')') {
          throw std::string("missing ')'");
        }
        ++position_;
        return inner;
      }
      case '.':
        return symbol(printable());
      case '[':
        return symbol(character_class());
      case '\\':
        return symbol({escaped()});
      case '*':
      case '+':
      case '?':
      case ')':
      case '|':
        throw std::string("unexpected '") + ch + "' at position " + std::to_string(position_ - 1);
      default:
        return symbol({ch});
    }
  }

  std::set<char> character_class() {
    std::set<char> chars;
    bool negated = false;
    if (!at_end() && peek() == '^') {
      negated = true;
      ++position_;
    }
    bool first = true;
    while (true) {
      if (at_end()) {
        throw std::string("missing ']'");
      }
      char ch = pattern_[position_++];
      if (ch == ']' && !first) {
        break;
      }
      first = false;
      if (ch == '\\') {
        ch = escaped();
      }
      if (!at_end() && peek() == '-' && position_ + 1 < pattern_.size() && pattern_[position_ + 1] != ']') {
        ++position_;
        char high = pattern_[position_++];
        if (high == '\\') {
          high = escaped();
        }
        if (high < ch) {
          throw std::string("invalid range in character class");
        }
        for (int value = ch; value <= high; ++value) {
          chars.insert(static_cast<char>(value));
        }
      } else {
        chars.insert(ch);
      }
    }
    if (negated) {
      std::set<char> complement;
      for (const char candidate : printable()) {
        if (!chars.contains(candidate)) {
          complement.insert(candidate);
        }
      }
      return complement;
    }
    return chars;
  }

  const std::string& pattern_;
  std::size_t position_ = 0;
  Nfa nfa_;
};

std::set<std::size_t> epsilon_closure(const Nfa& nfa, std::set<std::size_t> states) {
  std::vector<std::size_t> work(states.begin(), states.end());
  while (!work.empty()) {
    const std::size_t state = work.back();
    work.pop_back();
    for (const std::size_t next : nfa.states[state].epsilon) {
      if (states.insert(next).second) {
        work.push_back(next);
      }
    }
  }
  return states;
}

std::set<std::size_t> move(const Nfa& nfa, const std::set<std::size_t>& states, char ch) {
  std::set<std::size_t> result;
  for (const std::size_t state : states) {
    for (const auto& [chars, target] : nfa.states[state].edges) {
      if (chars.contains(ch)) {
        result.insert(target);
      }
    }
  }
  return result;
}

std::string char_name(char ch) {
  if (ch == ' ') {
    return "' '";
  }
  if (ch == '\n') {
    return "\\n";
  }
  if (ch == '\t') {
    return "\\t";
  }
  return std::string(1, ch);
}

}  // namespace

RegexResult regex_to_nfa(const std::string& pattern) {
  return RegexParser(pattern).parse();
}

bool nfa_matches(const Nfa& nfa, const std::string& text) {
  std::set<std::size_t> current = epsilon_closure(nfa, {nfa.start});
  for (const char ch : text) {
    current = epsilon_closure(nfa, move(nfa, current, ch));
    if (current.empty()) {
      return false;
    }
  }
  return current.contains(nfa.accept);
}

bool Dfa::matches(const std::string& text) const {
  std::size_t state = start;
  for (const char ch : text) {
    const auto found = transitions[state].find(ch);
    if (found == transitions[state].end()) {
      return false;
    }
    state = found->second;
  }
  return accepting[state];
}

Dfa subset_construction(const Nfa& nfa) {
  std::set<char> alphabet;
  for (const auto& state : nfa.states) {
    for (const auto& [chars, target] : state.edges) {
      alphabet.insert(chars.begin(), chars.end());
    }
  }

  Dfa dfa;
  dfa.alphabet.assign(alphabet.begin(), alphabet.end());
  std::map<std::set<std::size_t>, std::size_t> index;
  std::deque<std::set<std::size_t>> work;
  const auto start = epsilon_closure(nfa, {nfa.start});
  index[start] = 0;
  dfa.transitions.emplace_back();
  dfa.accepting.push_back(start.contains(nfa.accept));
  work.push_back(start);
  while (!work.empty()) {
    const auto current = work.front();
    work.pop_front();
    const std::size_t from = index.at(current);
    for (const char ch : dfa.alphabet) {
      auto next = epsilon_closure(nfa, move(nfa, current, ch));
      if (next.empty()) {
        continue;
      }
      auto [found, inserted] = index.try_emplace(next, dfa.transitions.size());
      if (inserted) {
        dfa.transitions.emplace_back();
        dfa.accepting.push_back(next.contains(nfa.accept));
        work.push_back(next);
      }
      dfa.transitions[from][ch] = found->second;
    }
  }
  return dfa;
}

Dfa minimize(const Dfa& dfa) {
  // Drop unreachable states.
  std::vector<bool> reachable(dfa.size(), false);
  std::vector<std::size_t> work{dfa.start};
  reachable[dfa.start] = true;
  while (!work.empty()) {
    const std::size_t state = work.back();
    work.pop_back();
    for (const auto& [ch, target] : dfa.transitions[state]) {
      if (!reachable[target]) {
        reachable[target] = true;
        work.push_back(target);
      }
    }
  }

  // Moore refinement over a completed automaton (index dfa.size() is the explicit dead state).
  const std::size_t dead = dfa.size();
  auto target_of = [&](std::size_t state, char ch) {
    if (state == dead) {
      return dead;
    }
    const auto found = dfa.transitions[state].find(ch);
    return found == dfa.transitions[state].end() ? dead : found->second;
  };
  std::vector<std::size_t> block(dfa.size() + 1, 0);
  for (std::size_t state = 0; state < dfa.size(); ++state) {
    block[state] = dfa.accepting[state] ? 1U : 0U;
  }
  block[dead] = 0;
  std::size_t block_count = 0;
  while (true) {
    std::map<std::vector<std::size_t>, std::size_t> signatures;
    std::vector<std::size_t> next_block(block.size(), 0);
    for (std::size_t state = 0; state <= dead; ++state) {
      if (state != dead && !reachable[state]) {
        continue;
      }
      std::vector<std::size_t> signature{block[state]};
      for (const char ch : dfa.alphabet) {
        signature.push_back(block[target_of(state, ch)]);
      }
      next_block[state] = signatures.try_emplace(signature, signatures.size()).first->second;
    }
    const bool stable = signatures.size() == block_count;
    block_count = signatures.size();
    block = std::move(next_block);
    if (stable) {
      break;
    }
  }

  // Build the quotient automaton, omitting the dead block.
  const std::size_t dead_block = block[dead];
  Dfa minimal;
  minimal.alphabet = dfa.alphabet;
  std::map<std::size_t, std::size_t> renumber;
  std::vector<std::size_t> order{block[dfa.start]};
  renumber[block[dfa.start]] = 0;
  std::vector<std::size_t> representative(block_count, dead);
  for (std::size_t state = 0; state < dfa.size(); ++state) {
    if (reachable[state] && representative[block[state]] == dead) {
      representative[block[state]] = state;
    }
  }
  for (std::size_t cursor = 0; cursor < order.size(); ++cursor) {
    const std::size_t source = representative[order[cursor]];
    minimal.transitions.emplace_back();
    minimal.accepting.push_back(dfa.accepting[source]);
    for (const char ch : dfa.alphabet) {
      const std::size_t target_block = block[target_of(source, ch)];
      if (target_block == dead_block) {
        continue;
      }
      auto [found, inserted] = renumber.try_emplace(target_block, order.size());
      if (inserted) {
        order.push_back(target_block);
      }
      minimal.transitions[cursor][ch] = found->second;
    }
  }
  minimal.start = 0;
  return minimal;
}

std::string print_nfa(const Nfa& nfa) {
  std::ostringstream out;
  out << "Thompson NFA: " << nfa.states.size() << " states, start " << nfa.start << ", accept " << nfa.accept << '\n';
  for (std::size_t state = 0; state < nfa.states.size(); ++state) {
    for (const std::size_t target : nfa.states[state].epsilon) {
      out << "  " << state << " --eps--> " << target << '\n';
    }
    for (const auto& [chars, target] : nfa.states[state].edges) {
      out << "  " << state << " --";
      if (chars.size() > 6) {
        out << '{' << chars.size() << " chars}";
      } else {
        for (const char ch : chars) {
          out << char_name(ch);
        }
      }
      out << "--> " << target << '\n';
    }
  }
  return out.str();
}

std::string print_dfa(const Dfa& dfa, const std::string& title) {
  std::ostringstream out;
  out << title << ": " << dfa.size() << " states (start " << dfa.start << ")\n";
  for (std::size_t state = 0; state < dfa.size(); ++state) {
    out << "  " << (dfa.accepting[state] ? '*' : ' ') << state << ':';
    // group characters with the same target into compact runs
    std::map<std::size_t, std::string> by_target;
    for (const auto& [ch, target] : dfa.transitions[state]) {
      by_target[target] += char_name(ch);
    }
    for (const auto& [target, chars] : by_target) {
      out << "  [" << (chars.size() > 12 ? std::to_string(chars.size()) + " chars" : chars) << "]->" << target;
    }
    out << '\n';
  }
  return out.str();
}

}  // namespace nexus::compiler::formal
