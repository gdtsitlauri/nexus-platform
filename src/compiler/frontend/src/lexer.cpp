#include "nexus/compiler/frontend/lexer.hpp"

#include <cctype>
#include <string>
#include <unordered_map>

namespace nexus::compiler::frontend {

namespace {

class Lexer {
 public:
  explicit Lexer(std::string_view source_text) : source_(source_text) {}

  LexResult run() {
    while (!is_at_end()) {
      skip_trivia();
      if (is_at_end()) {
        break;
      }
      scan_token();
    }

    tokens_.push_back(Token{TokenKind::EndOfFile, "", make_span(location_, location_)});
    return LexResult{.tokens = std::move(tokens_), .diagnostics = std::move(diagnostics_)};
  }

 private:
  static bool is_identifier_start(char character) {
    return std::isalpha(static_cast<unsigned char>(character)) != 0 || character == '_';
  }

  static bool is_identifier_continue(char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
  }

  static SourceSpan make_span(SourceLocation begin, SourceLocation end) {
    return SourceSpan{.begin = begin, .end = end};
  }

  bool is_at_end() const { return index_ >= source_.size(); }

  char peek() const { return is_at_end() ? '\0' : source_[index_]; }

  char peek_next() const {
    const std::size_t next_index = index_ + 1;
    return next_index >= source_.size() ? '\0' : source_[next_index];
  }

  char advance() {
    const char character = source_[index_++];
    if (character == '\n') {
      ++location_.line;
      location_.column = 1;
    } else {
      ++location_.column;
    }
    return character;
  }

  bool match(char expected) {
    if (peek() != expected) {
      return false;
    }
    advance();
    return true;
  }

  void emit(TokenKind kind, std::string lexeme, SourceLocation begin) {
    tokens_.push_back(Token{kind, std::move(lexeme), make_span(begin, location_)});
  }

  void emit_error(SourceLocation begin, const std::string& message) {
    diagnostics_.push_back(
        Diagnostic{.severity = DiagnosticSeverity::Error,
                   .span = make_span(begin, location_),
                   .message = message});
  }

  void skip_trivia() {
    while (!is_at_end()) {
      if (std::isspace(static_cast<unsigned char>(peek())) != 0) {
        advance();
        continue;
      }

      if (peek() == '/' && peek_next() == '/') {
        while (!is_at_end() && peek() != '\n') {
          advance();
        }
        continue;
      }

      break;
    }
  }

  void scan_token() {
    static const std::unordered_map<std::string, TokenKind> keywords = {
        {"fn", TokenKind::Fn},       {"var", TokenKind::Var},   {"if", TokenKind::If},
        {"else", TokenKind::Else},   {"while", TokenKind::While},
        {"return", TokenKind::Return},
        {"true", TokenKind::True},   {"false", TokenKind::False},
        {"int", TokenKind::Int},     {"bool", TokenKind::Bool}, {"void", TokenKind::Void},
    };

    const SourceLocation begin = location_;
    const char character = advance();

    if (is_identifier_start(character)) {
      std::string lexeme(1, character);
      while (is_identifier_continue(peek())) {
        lexeme.push_back(advance());
      }
      const auto keyword = keywords.find(lexeme);
      emit(keyword == keywords.end() ? TokenKind::Identifier : keyword->second, std::move(lexeme), begin);
      return;
    }

    if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
      std::string lexeme(1, character);
      while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
        lexeme.push_back(advance());
      }
      emit(TokenKind::IntegerLiteral, std::move(lexeme), begin);
      return;
    }

    switch (character) {
      case '(':
        emit(TokenKind::LeftParen, "(", begin);
        return;
      case ')':
        emit(TokenKind::RightParen, ")", begin);
        return;
      case '{':
        emit(TokenKind::LeftBrace, "{", begin);
        return;
      case '}':
        emit(TokenKind::RightBrace, "}", begin);
        return;
      case '[':
        emit(TokenKind::LeftBracket, "[", begin);
        return;
      case ']':
        emit(TokenKind::RightBracket, "]", begin);
        return;
      case ':':
        emit(TokenKind::Colon, ":", begin);
        return;
      case ',':
        emit(TokenKind::Comma, ",", begin);
        return;
      case ';':
        emit(TokenKind::Semicolon, ";", begin);
        return;
      case '+':
        emit(TokenKind::Plus, "+", begin);
        return;
      case '-':
        if (match('>')) {
          emit(TokenKind::Arrow, "->", begin);
        } else {
          emit(TokenKind::Minus, "-", begin);
        }
        return;
      case '*':
        emit(TokenKind::Star, "*", begin);
        return;
      case '/':
        emit(TokenKind::Slash, "/", begin);
        return;
      case '%':
        emit(TokenKind::Percent, "%", begin);
        return;
      case '!':
        if (match('=')) {
          emit(TokenKind::BangEqual, "!=", begin);
        } else {
          emit(TokenKind::Bang, "!", begin);
        }
        return;
      case '=':
        if (match('=')) {
          emit(TokenKind::EqualEqual, "==", begin);
        } else {
          emit(TokenKind::Equal, "=", begin);
        }
        return;
      case '<':
        if (match('=')) {
          emit(TokenKind::LessEqual, "<=", begin);
        } else {
          emit(TokenKind::Less, "<", begin);
        }
        return;
      case '>':
        if (match('=')) {
          emit(TokenKind::GreaterEqual, ">=", begin);
        } else {
          emit(TokenKind::Greater, ">", begin);
        }
        return;
      case '&':
        if (match('&')) {
          emit(TokenKind::AndAnd, "&&", begin);
        } else {
          emit_error(begin, "expected '&' to complete logical operator '&&'");
        }
        return;
      case '|':
        if (match('|')) {
          emit(TokenKind::OrOr, "||", begin);
        } else {
          emit_error(begin, "expected '|' to complete logical operator '||'");
        }
        return;
      default:
        emit_error(begin, "unexpected character in source");
        return;
    }
  }

  std::string_view source_;
  std::size_t index_ = 0;
  SourceLocation location_{};
  std::vector<Token> tokens_;
  std::vector<Diagnostic> diagnostics_;
};

}  // namespace

LexResult lex_source(std::string_view source_text) {
  Lexer lexer(source_text);
  return lexer.run();
}

}  // namespace nexus::compiler::frontend
