#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Boolean algebra: expressions, truth tables and canonical forms (EY121 Logic Design).
//
// Syntax: identifiers are variables; 0 and 1 are constants; NOT is a prefix '!' or '~' or a postfix '\''; AND is
// '&', '*' or juxtaposition separated by spaces ("a b'"); XOR is '^'; OR is '|' or '+'. Precedence from high to
// low: NOT, AND, XOR, OR. Variable i of an n-variable function is bit (n - 1 - i) of the minterm number, so for
// variables a, b, c the minterm a b' c is number 5 (the textbook convention).
namespace nexus::logic {

struct Expr;
using ExprPtr = std::shared_ptr<const Expr>;

struct Expr {
  enum class Kind { Const, Var, Not, And, Or, Xor };
  Kind kind = Kind::Const;
  bool value = false;     // Const
  std::size_t var = 0;    // Var
  std::vector<ExprPtr> kids;
};

ExprPtr make_const(bool value);
ExprPtr make_var(std::size_t index);
ExprPtr make_not(ExprPtr a);
ExprPtr make_and(ExprPtr a, ExprPtr b);
ExprPtr make_or(ExprPtr a, ExprPtr b);
ExprPtr make_xor(ExprPtr a, ExprPtr b);

struct Function {
  ExprPtr expr;
  std::vector<std::string> vars;
};

// Parses an expression. With a non-empty order, variables are numbered by that list (every variable used must be
// in it); otherwise by first appearance. Throws std::runtime_error on a syntax error.
Function parse(std::string_view text, const std::vector<std::string>& order = {});

bool evaluate(const Expr& expr, std::size_t num_vars, std::uint64_t minterm);

// value[m] for every minterm m of the function's variables (at most 20 variables).
std::vector<std::uint8_t> truth_table(const Function& f);

std::vector<std::uint64_t> minterms(const std::vector<std::uint8_t>& table);
std::vector<std::uint64_t> maxterms(const std::vector<std::uint8_t>& table);

std::string to_string(const Expr& expr, const std::vector<std::string>& vars);
std::string canonical_sop(const std::vector<std::string>& vars, const std::vector<std::uint64_t>& minterms);
std::string canonical_pos(const std::vector<std::string>& vars, const std::vector<std::uint64_t>& maxterms);
std::string format_truth_table(const Function& f);

// Rewrites using only NAND (respectively NOR) gates and constants; the result is equivalent to the input.
ExprPtr to_nand_only(const ExprPtr& expr);
ExprPtr to_nor_only(const ExprPtr& expr);
std::size_t gate_count(const Expr& expr);
bool uses_only(const Expr& expr, bool nand);  // true when every gate is NAND (or NOR) written as Not(And/Or)

bool equivalent(const Function& f, const Function& g);  // same variables (by name), by truth table

}  // namespace nexus::logic
