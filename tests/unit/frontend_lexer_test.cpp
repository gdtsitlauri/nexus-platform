#include <algorithm>
#include <iostream>
#include <string>

#include "nexus/compiler/frontend/lexer.hpp"

namespace {

bool contains_kind(
    const std::vector<nexus::compiler::frontend::Token>& tokens,
    nexus::compiler::frontend::TokenKind kind) {
  return std::any_of(tokens.begin(), tokens.end(), [kind](const auto& token) { return token.kind == kind; });
}

}  // namespace

int main() {
  using namespace nexus::compiler::frontend;

  const std::string valid_source =
      "fn sample(flag: bool, count: int) -> int {\n"
      "  var acc: int = 0;\n"
      "  if (flag && true || false) {\n"
      "    acc = count + 1;\n"
      "  } else {\n"
      "    while (acc <= count) {\n"
      "      acc = acc + 1;\n"
      "    }\n"
      "  }\n"
      "  return acc;\n"
      "}\n";

  const LexResult valid_result = lex_source(valid_source);
  if (!valid_result.diagnostics.empty()) {
    std::cerr << "Expected no lexical diagnostics for valid source.\n";
    return 1;
  }

  const TokenKind required_kinds[] = {
      TokenKind::Fn,       TokenKind::Var,         TokenKind::If,      TokenKind::Else,
      TokenKind::While,    TokenKind::Return,      TokenKind::True,    TokenKind::False,
      TokenKind::Int,      TokenKind::Bool,        TokenKind::AndAnd,  TokenKind::OrOr,
      TokenKind::LessEqual, TokenKind::Plus,       TokenKind::Identifier,
      TokenKind::IntegerLiteral,
  };
  for (TokenKind kind : required_kinds) {
    if (!contains_kind(valid_result.tokens, kind)) {
      std::cerr << "Missing token kind in valid lex test: " << token_kind_name(kind) << '\n';
      return 1;
    }
  }

  const LexResult invalid_result = lex_source("@");
  if (invalid_result.diagnostics.size() != 1U) {
    std::cerr << "Expected one lexical diagnostic for invalid input.\n";
    return 1;
  }
  if (invalid_result.diagnostics.front().span.begin.line != 1U ||
      invalid_result.diagnostics.front().span.begin.column != 1U) {
    std::cerr << "Unexpected lexical diagnostic location.\n";
    return 1;
  }

  return 0;
}
