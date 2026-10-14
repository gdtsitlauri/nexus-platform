#include "nexus/logic/boolean.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace nexus::logic {

ExprPtr make_const(bool value) {
  auto e = std::make_shared<Expr>();
  e->kind = Expr::Kind::Const;
  e->value = value;
  return e;
}

ExprPtr make_var(std::size_t index) {
  auto e = std::make_shared<Expr>();
  e->kind = Expr::Kind::Var;
  e->var = index;
  return e;
}

namespace {

ExprPtr make_node(Expr::Kind kind, std::vector<ExprPtr> kids) {
  auto e = std::make_shared<Expr>();
  e->kind = kind;
  e->kids = std::move(kids);
  return e;
}

class Parser {
 public:
  Parser(std::string_view text, const std::vector<std::string>& order) : text_(text) {
    for (const auto& name : order) {
      vars_.push_back(name);
    }
    fixed_ = !order.empty();
  }

  Function run() {
    ExprPtr e = parse_or();
    skip_space();
    if (pos_ != text_.size()) {
      fail("unexpected character");
    }
    return Function{e, vars_};
  }

 private:
  [[noreturn]] void fail(const std::string& what) const {
    throw std::runtime_error("boolean expression: " + what + " at position " + std::to_string(pos_));
  }

  void skip_space() {
    while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_])) != 0) {
      ++pos_;
    }
  }

  bool peek(char c) {
    skip_space();
    return pos_ < text_.size() && text_[pos_] == c;
  }

  bool starts_atom() {
    skip_space();
    if (pos_ >= text_.size()) {
      return false;
    }
    const char c = text_[pos_];
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '(' || c == '!' || c == '~' ||
           c == '0' || c == '1';
  }

  ExprPtr parse_or() {
    ExprPtr left = parse_xor();
    while (peek('|') || peek('+')) {
      ++pos_;
      left = make_or(left, parse_xor());
    }
    return left;
  }

  ExprPtr parse_xor() {
    ExprPtr left = parse_and();
    while (peek('^')) {
      ++pos_;
      left = make_xor(left, parse_and());
    }
    return left;
  }

  ExprPtr parse_and() {
    ExprPtr left = parse_unary();
    for (;;) {
      if (peek('&') || peek('*')) {
        ++pos_;
        left = make_and(left, parse_unary());
      } else if (starts_atom()) {
        left = make_and(left, parse_unary());
      } else {
        return left;
      }
    }
  }

  ExprPtr parse_unary() {
    if (peek('!') || peek('~')) {
      ++pos_;
      return make_not(parse_unary());
    }
    ExprPtr e = parse_atom();
    while (peek('\'')) {
      ++pos_;
      e = make_not(e);
    }
    return e;
  }

  ExprPtr parse_atom() {
    skip_space();
    if (pos_ >= text_.size()) {
      fail("unexpected end");
    }
    const char c = text_[pos_];
    if (c == '(') {
      ++pos_;
      ExprPtr e = parse_or();
      if (!peek(')')) {
        fail("expected ')'");
      }
      ++pos_;
      return e;
    }
    if (c == '0' || c == '1') {
      ++pos_;
      return make_const(c == '1');
    }
    if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_') {
      const std::size_t start = pos_;
      while (pos_ < text_.size() &&
             (std::isalnum(static_cast<unsigned char>(text_[pos_])) != 0 || text_[pos_] == '_')) {
        ++pos_;
      }
      const std::string name(text_.substr(start, pos_ - start));
      const auto it = std::find(vars_.begin(), vars_.end(), name);
      if (it != vars_.end()) {
        return make_var(static_cast<std::size_t>(it - vars_.begin()));
      }
      if (fixed_) {
        fail("variable '" + name + "' not in the given order");
      }
      vars_.push_back(name);
      return make_var(vars_.size() - 1);
    }
    fail(std::string("unexpected '") + c + "'");
  }

  std::string_view text_;
  std::size_t pos_ = 0;
  std::vector<std::string> vars_;
  bool fixed_ = false;
};

int precedence(Expr::Kind k) {
  switch (k) {
    case Expr::Kind::Or:
      return 1;
    case Expr::Kind::Xor:
      return 2;
    case Expr::Kind::And:
      return 3;
    default:
      return 4;
  }
}

void print(const Expr& e, const std::vector<std::string>& vars, std::ostringstream& out, int parent) {
  const int prec = precedence(e.kind);
  const bool paren = prec < parent;
  if (paren) {
    out << '(';
  }
  switch (e.kind) {
    case Expr::Kind::Const:
      out << (e.value ? '1' : '0');
      break;
    case Expr::Kind::Var:
      out << (e.var < vars.size() ? vars[e.var] : "x" + std::to_string(e.var));
      break;
    case Expr::Kind::Not:
      print(*e.kids[0], vars, out, 5);
      out << '\'';
      break;
    case Expr::Kind::And:
      print(*e.kids[0], vars, out, prec);
      out << ' ';
      print(*e.kids[1], vars, out, prec + 1);
      break;
    case Expr::Kind::Xor:
      print(*e.kids[0], vars, out, prec);
      out << " ^ ";
      print(*e.kids[1], vars, out, prec + 1);
      break;
    case Expr::Kind::Or:
      print(*e.kids[0], vars, out, prec);
      out << " + ";
      print(*e.kids[1], vars, out, prec + 1);
      break;
  }
  if (paren) {
    out << ')';
  }
}

std::string literal(const std::vector<std::string>& vars, std::size_t i, bool positive) {
  return vars[i] + (positive ? "" : "'");
}

}  // namespace

ExprPtr make_not(ExprPtr a) { return make_node(Expr::Kind::Not, {std::move(a)}); }
ExprPtr make_and(ExprPtr a, ExprPtr b) { return make_node(Expr::Kind::And, {std::move(a), std::move(b)}); }
ExprPtr make_or(ExprPtr a, ExprPtr b) { return make_node(Expr::Kind::Or, {std::move(a), std::move(b)}); }
ExprPtr make_xor(ExprPtr a, ExprPtr b) { return make_node(Expr::Kind::Xor, {std::move(a), std::move(b)}); }

Function parse(std::string_view text, const std::vector<std::string>& order) { return Parser(text, order).run(); }

bool evaluate(const Expr& e, std::size_t num_vars, std::uint64_t minterm) {
  switch (e.kind) {
    case Expr::Kind::Const:
      return e.value;
    case Expr::Kind::Var:
      return ((minterm >> (num_vars - 1 - e.var)) & 1U) != 0;
    case Expr::Kind::Not:
      return !evaluate(*e.kids[0], num_vars, minterm);
    case Expr::Kind::And:
      return evaluate(*e.kids[0], num_vars, minterm) && evaluate(*e.kids[1], num_vars, minterm);
    case Expr::Kind::Or:
      return evaluate(*e.kids[0], num_vars, minterm) || evaluate(*e.kids[1], num_vars, minterm);
    case Expr::Kind::Xor:
      return evaluate(*e.kids[0], num_vars, minterm) != evaluate(*e.kids[1], num_vars, minterm);
  }
  return false;
}

std::vector<std::uint8_t> truth_table(const Function& f) {
  const std::size_t n = f.vars.size();
  if (n > 20) {
    throw std::runtime_error("truth table: too many variables");
  }
  std::vector<std::uint8_t> table(std::size_t{1} << n);
  for (std::uint64_t m = 0; m < table.size(); ++m) {
    table[m] = evaluate(*f.expr, n, m) ? 1 : 0;
  }
  return table;
}

std::vector<std::uint64_t> minterms(const std::vector<std::uint8_t>& table) {
  std::vector<std::uint64_t> out;
  for (std::uint64_t m = 0; m < table.size(); ++m) {
    if (table[m] != 0) {
      out.push_back(m);
    }
  }
  return out;
}

std::vector<std::uint64_t> maxterms(const std::vector<std::uint8_t>& table) {
  std::vector<std::uint64_t> out;
  for (std::uint64_t m = 0; m < table.size(); ++m) {
    if (table[m] == 0) {
      out.push_back(m);
    }
  }
  return out;
}

std::string to_string(const Expr& expr, const std::vector<std::string>& vars) {
  std::ostringstream out;
  print(expr, vars, out, 0);
  return out.str();
}

std::string canonical_sop(const std::vector<std::string>& vars, const std::vector<std::uint64_t>& mins) {
  if (mins.empty()) {
    return "0";
  }
  const std::size_t n = vars.size();
  std::string out;
  for (std::size_t t = 0; t < mins.size(); ++t) {
    if (t > 0) {
      out += " + ";
    }
    for (std::size_t i = 0; i < n; ++i) {
      if (i > 0) {
        out += ' ';
      }
      out += literal(vars, i, ((mins[t] >> (n - 1 - i)) & 1U) != 0);
    }
  }
  return out;
}

std::string canonical_pos(const std::vector<std::string>& vars, const std::vector<std::uint64_t>& maxs) {
  if (maxs.empty()) {
    return "1";
  }
  const std::size_t n = vars.size();
  std::string out;
  for (const auto m : maxs) {
    out += '(';
    for (std::size_t i = 0; i < n; ++i) {
      if (i > 0) {
        out += " + ";
      }
      // a maxterm is 0 exactly at m: a variable that is 1 in m appears complemented
      out += literal(vars, i, ((m >> (n - 1 - i)) & 1U) == 0);
    }
    out += ')';
  }
  return out;
}

std::string format_truth_table(const Function& f) {
  const auto table = truth_table(f);
  const std::size_t n = f.vars.size();
  std::ostringstream out;
  out << "  m |";
  for (const auto& v : f.vars) {
    out << ' ' << v;
  }
  out << " | f\n";
  for (std::uint64_t m = 0; m < table.size(); ++m) {
    out << (m < 10 ? "  " : (m < 100 ? " " : "")) << m << " |";
    for (std::size_t i = 0; i < n; ++i) {
      out << ' ' << std::string(f.vars[i].size() - 1, ' ') << ((m >> (n - 1 - i)) & 1U);
    }
    out << " | " << static_cast<int>(table[m]) << '\n';
  }
  return out.str();
}

namespace {

ExprPtr nand(ExprPtr a, ExprPtr b) { return make_not(make_and(std::move(a), std::move(b))); }
ExprPtr nor(ExprPtr a, ExprPtr b) { return make_not(make_or(std::move(a), std::move(b))); }

ExprPtr nand_rewrite(const ExprPtr& e) {
  switch (e->kind) {
    case Expr::Kind::Const:
    case Expr::Kind::Var:
      return e;
    case Expr::Kind::Not: {
      const ExprPtr a = nand_rewrite(e->kids[0]);
      return nand(a, a);
    }
    case Expr::Kind::And: {
      const ExprPtr t = nand(nand_rewrite(e->kids[0]), nand_rewrite(e->kids[1]));
      return nand(t, t);
    }
    case Expr::Kind::Or: {
      const ExprPtr a = nand_rewrite(e->kids[0]);
      const ExprPtr b = nand_rewrite(e->kids[1]);
      return nand(nand(a, a), nand(b, b));
    }
    case Expr::Kind::Xor: {
      const ExprPtr a = nand_rewrite(e->kids[0]);
      const ExprPtr b = nand_rewrite(e->kids[1]);
      const ExprPtr m = nand(a, b);
      return nand(nand(a, m), nand(b, m));  // the classic four-NAND XOR
    }
  }
  return e;
}

ExprPtr nor_rewrite(const ExprPtr& e) {
  switch (e->kind) {
    case Expr::Kind::Const:
    case Expr::Kind::Var:
      return e;
    case Expr::Kind::Not: {
      const ExprPtr a = nor_rewrite(e->kids[0]);
      return nor(a, a);
    }
    case Expr::Kind::Or: {
      const ExprPtr t = nor(nor_rewrite(e->kids[0]), nor_rewrite(e->kids[1]));
      return nor(t, t);
    }
    case Expr::Kind::And: {
      const ExprPtr a = nor_rewrite(e->kids[0]);
      const ExprPtr b = nor_rewrite(e->kids[1]);
      return nor(nor(a, a), nor(b, b));
    }
    case Expr::Kind::Xor: {
      // XNOR from four NORs, then invert
      const ExprPtr a = nor_rewrite(e->kids[0]);
      const ExprPtr b = nor_rewrite(e->kids[1]);
      const ExprPtr m = nor(a, b);
      const ExprPtr xnor = nor(nor(a, m), nor(b, m));
      return nor(xnor, xnor);
    }
  }
  return e;
}

}  // namespace

ExprPtr to_nand_only(const ExprPtr& expr) { return nand_rewrite(expr); }
ExprPtr to_nor_only(const ExprPtr& expr) { return nor_rewrite(expr); }

std::size_t gate_count(const Expr& e) {
  std::size_t count = 0;
  if (e.kind == Expr::Kind::Not && !e.kids.empty() &&
      (e.kids[0]->kind == Expr::Kind::And || e.kids[0]->kind == Expr::Kind::Or)) {
    count = 1;
    for (const auto& k : e.kids[0]->kids) {
      count += gate_count(*k);
    }
    return count;
  }
  if (e.kind != Expr::Kind::Const && e.kind != Expr::Kind::Var) {
    count = 1;
  }
  for (const auto& k : e.kids) {
    count += gate_count(*k);
  }
  return count;
}

bool uses_only(const Expr& e, bool nand_gates) {
  switch (e.kind) {
    case Expr::Kind::Const:
    case Expr::Kind::Var:
      return true;
    case Expr::Kind::Not: {
      const Expr& k = *e.kids[0];
      const Expr::Kind want = nand_gates ? Expr::Kind::And : Expr::Kind::Or;
      return k.kind == want && uses_only(*k.kids[0], nand_gates) && uses_only(*k.kids[1], nand_gates);
    }
    default:
      return false;
  }
}

bool equivalent(const Function& f, const Function& g) {
  std::vector<std::string> names = f.vars;
  for (const auto& v : g.vars) {
    if (std::find(names.begin(), names.end(), v) == names.end()) {
      names.push_back(v);
    }
  }
  const Function f2 = parse(to_string(*f.expr, f.vars), names);
  const Function g2 = parse(to_string(*g.expr, g.vars), names);
  return truth_table(f2) == truth_table(g2);
}

}  // namespace nexus::logic
