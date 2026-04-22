#include "nexus/compiler/frontend/token.hpp"

#include <iomanip>
#include <sstream>

namespace nexus::compiler::frontend {

std::string_view token_kind_name(TokenKind kind) {
  switch (kind) {
    case TokenKind::EndOfFile:
      return "eof";
    case TokenKind::Identifier:
      return "identifier";
    case TokenKind::IntegerLiteral:
      return "integer";
    case TokenKind::Fn:
      return "fn";
    case TokenKind::Var:
      return "var";
    case TokenKind::If:
      return "if";
    case TokenKind::Else:
      return "else";
    case TokenKind::While:
      return "while";
    case TokenKind::Return:
      return "return";
    case TokenKind::True:
      return "true";
    case TokenKind::False:
      return "false";
    case TokenKind::Int:
      return "int";
    case TokenKind::Bool:
      return "bool";
    case TokenKind::Void:
      return "void";
    case TokenKind::LeftParen:
      return "left_paren";
    case TokenKind::RightParen:
      return "right_paren";
    case TokenKind::LeftBrace:
      return "left_brace";
    case TokenKind::RightBrace:
      return "right_brace";
    case TokenKind::LeftBracket:
      return "left_bracket";
    case TokenKind::RightBracket:
      return "right_bracket";
    case TokenKind::Colon:
      return "colon";
    case TokenKind::Comma:
      return "comma";
    case TokenKind::Semicolon:
      return "semicolon";
    case TokenKind::Arrow:
      return "arrow";
    case TokenKind::Plus:
      return "plus";
    case TokenKind::Minus:
      return "minus";
    case TokenKind::Star:
      return "star";
    case TokenKind::Slash:
      return "slash";
    case TokenKind::Percent:
      return "percent";
    case TokenKind::Bang:
      return "bang";
    case TokenKind::Equal:
      return "equal";
    case TokenKind::EqualEqual:
      return "equal_equal";
    case TokenKind::BangEqual:
      return "bang_equal";
    case TokenKind::Less:
      return "less";
    case TokenKind::LessEqual:
      return "less_equal";
    case TokenKind::Greater:
      return "greater";
    case TokenKind::GreaterEqual:
      return "greater_equal";
    case TokenKind::AndAnd:
      return "and_and";
    case TokenKind::OrOr:
      return "or_or";
  }

  return "unknown";
}

std::string dump_tokens(const std::vector<Token>& tokens) {
  std::ostringstream output;
  for (const Token& token : tokens) {
    output << token.span.begin.line << ':' << token.span.begin.column << "  "
           << std::left << std::setw(14) << token_kind_name(token.kind) << "  \"" << token.lexeme
           << "\"\n";
  }
  return output.str();
}

}  // namespace nexus::compiler::frontend
