#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "nexus/compiler/frontend/diagnostic.hpp"

namespace nexus::compiler::frontend {

enum class TokenKind {
  EndOfFile,
  Identifier,
  IntegerLiteral,
  Fn,
  Var,
  If,
  Else,
  While,
  Return,
  True,
  False,
  Int,
  Bool,
  Void,
  LeftParen,
  RightParen,
  LeftBrace,
  RightBrace,
  LeftBracket,
  RightBracket,
  Colon,
  Comma,
  Semicolon,
  Arrow,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Bang,
  Equal,
  EqualEqual,
  BangEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  AndAnd,
  OrOr,
};

struct Token {
  TokenKind kind = TokenKind::EndOfFile;
  std::string lexeme;
  SourceSpan span{};
};

std::string_view token_kind_name(TokenKind kind);
std::string dump_tokens(const std::vector<Token>& tokens);

}  // namespace nexus::compiler::frontend
