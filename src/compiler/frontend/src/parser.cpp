#include "nexus/compiler/frontend/parser.hpp"

#include <cstdlib>
#include <optional>
#include <utility>

#include "nexus/compiler/frontend/lexer.hpp"

namespace nexus::compiler::frontend {

namespace {

SourceSpan merge_spans(const SourceSpan& begin, const SourceSpan& end) {
  return SourceSpan{.begin = begin.begin, .end = end.end};
}

BuiltinTypeKind builtin_type_from_token(TokenKind kind) {
  switch (kind) {
    case TokenKind::Int:
      return BuiltinTypeKind::Int;
    case TokenKind::Bool:
      return BuiltinTypeKind::Bool;
    case TokenKind::Void:
      return BuiltinTypeKind::Void;
    default:
      return BuiltinTypeKind::Int;
  }
}

class Parser {
 public:
  explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

  ParseResult run() {
    std::vector<FunctionDecl> functions;
    const SourceSpan program_begin = peek().span;

    while (!is_at_end()) {
      auto function = parse_function();
      if (function.has_value()) {
        functions.push_back(std::move(*function));
      } else {
        synchronize_declaration();
      }
    }

    SourceSpan program_span = program_begin;
    if (!functions.empty()) {
      program_span = merge_spans(functions.front().span, functions.back().span);
    }

    std::unique_ptr<Program> program =
        std::make_unique<Program>(program_span, std::move(functions));
    return ParseResult{.program = std::move(program), .diagnostics = std::move(diagnostics_)};
  }

 private:
  bool is_at_end() const { return peek().kind == TokenKind::EndOfFile; }

  const Token& peek() const { return tokens_[current_]; }

  const Token& previous() const { return tokens_[current_ - 1]; }

  const Token& advance() {
    if (!is_at_end()) {
      ++current_;
    }
    return previous();
  }

  bool check(TokenKind kind) const { return peek().kind == kind; }

  bool match(TokenKind kind) {
    if (!check(kind)) {
      return false;
    }
    advance();
    return true;
  }

  const Token& consume(TokenKind kind, const std::string& message) {
    if (check(kind)) {
      return advance();
    }
    error_at(peek(), message);
    return peek();
  }

  void error_at(const Token& token, const std::string& message) {
    diagnostics_.push_back(Diagnostic{
        .severity = DiagnosticSeverity::Error,
        .span = token.span,
        .message = message,
    });
  }

  std::optional<FunctionDecl> parse_function() {
    if (!match(TokenKind::Fn)) {
      error_at(peek(), "expected 'fn' to begin a function declaration");
      return std::nullopt;
    }

    const Token fn_token = previous();
    const Token& name_token = consume(TokenKind::Identifier, "expected function name");
    consume(TokenKind::LeftParen, "expected '(' after function name");

    std::vector<Parameter> parameters;
    if (!check(TokenKind::RightParen)) {
      do {
        const Token& parameter_name = consume(TokenKind::Identifier, "expected parameter name");
        consume(TokenKind::Colon, "expected ':' after parameter name");
        TypeSyntax parameter_type = parse_type(false);
        const SourceSpan parameter_span = merge_spans(parameter_name.span, parameter_type.span);
        parameters.push_back(Parameter{
            .name = parameter_name.lexeme,
            .type = std::move(parameter_type),
            .span = parameter_span,
        });
      } while (match(TokenKind::Comma));
    }

    consume(TokenKind::RightParen, "expected ')' after parameter list");
    consume(TokenKind::Arrow, "expected '->' before function return type");
    TypeSyntax return_type = parse_type(true);
    const Token& block_open = consume(TokenKind::LeftBrace, "expected '{' to start function body");
    std::unique_ptr<BlockStmt> body = parse_block_after_open_brace(block_open);

    if (!body) {
      return std::nullopt;
    }

    const SourceSpan function_span = merge_spans(fn_token.span, body->span);
    return FunctionDecl(
        function_span,
        name_token.lexeme,
        std::move(parameters),
        std::move(return_type),
        std::move(body));
  }

  TypeSyntax parse_type(bool allow_void) {
    const Token base_token = peek();
    if (!(match(TokenKind::Int) || match(TokenKind::Bool) || (allow_void && match(TokenKind::Void)))) {
      error_at(peek(), allow_void ? "expected type name" : "expected non-void type name");
      return TypeSyntax{.base = BuiltinTypeKind::Int, .array_extents = {}, .span = peek().span};
    }

    TypeSyntax type{
        .base = builtin_type_from_token(previous().kind),
        .array_extents = {},
        .span = previous().span,
    };

    while (match(TokenKind::LeftBracket)) {
      const Token& extent_token =
          consume(TokenKind::IntegerLiteral, "expected array extent literal inside type");
      std::int64_t extent = 0;
      if (!extent_token.lexeme.empty()) {
        extent = std::strtoll(extent_token.lexeme.c_str(), nullptr, 10);
      }
      type.array_extents.push_back(extent);
      const Token& close = consume(TokenKind::RightBracket, "expected ']' after array extent");
      type.span = merge_spans(base_token.span, close.span);
    }

    return type;
  }

  std::unique_ptr<BlockStmt> parse_block_after_open_brace(const Token& open_brace) {
    std::vector<StmtPtr> statements;

    while (!check(TokenKind::RightBrace) && !is_at_end()) {
      StmtPtr statement = parse_statement();
      if (statement) {
        statements.push_back(std::move(statement));
      } else {
        synchronize_statement();
      }
    }

    const Token& close_brace = consume(TokenKind::RightBrace, "expected '}' to close block");
    return std::make_unique<BlockStmt>(
        merge_spans(open_brace.span, close_brace.span), std::move(statements));
  }

  StmtPtr parse_statement() {
    if (match(TokenKind::LeftBrace)) {
      return parse_block_after_open_brace(previous());
    }

    if (match(TokenKind::Var)) {
      return parse_var_decl_statement(previous());
    }

    if (match(TokenKind::If)) {
      return parse_if_statement(previous());
    }

    if (match(TokenKind::While)) {
      return parse_while_statement(previous());
    }

    if (match(TokenKind::Return)) {
      return parse_return_statement(previous());
    }

    return parse_expression_or_assignment_statement();
  }

  StmtPtr parse_var_decl_statement(const Token& var_token) {
    const Token& name_token = consume(TokenKind::Identifier, "expected variable name after 'var'");
    consume(TokenKind::Colon, "expected ':' after variable name");
    TypeSyntax type = parse_type(false);
    ExprPtr initializer;
    if (match(TokenKind::Equal)) {
      initializer = parse_expression();
    }
    const Token& semicolon = consume(TokenKind::Semicolon, "expected ';' after variable declaration");
    return std::make_unique<VarDeclStmt>(
        merge_spans(var_token.span, semicolon.span),
        name_token.lexeme,
        std::move(type),
        std::move(initializer));
  }

  StmtPtr parse_if_statement(const Token& if_token) {
    consume(TokenKind::LeftParen, "expected '(' after 'if'");
    ExprPtr condition = parse_expression();
    consume(TokenKind::RightParen, "expected ')' after if condition");
    StmtPtr then_branch = parse_statement();
    StmtPtr else_branch;
    SourceSpan end_span = then_branch ? then_branch->span : condition->span;
    if (match(TokenKind::Else)) {
      else_branch = parse_statement();
      if (else_branch) {
        end_span = else_branch->span;
      }
    }
    return std::make_unique<IfStmt>(
        merge_spans(if_token.span, end_span), std::move(condition), std::move(then_branch), std::move(else_branch));
  }

  StmtPtr parse_while_statement(const Token& while_token) {
    consume(TokenKind::LeftParen, "expected '(' after 'while'");
    ExprPtr condition = parse_expression();
    consume(TokenKind::RightParen, "expected ')' after while condition");
    StmtPtr body = parse_statement();
    const SourceSpan end_span = body ? body->span : condition->span;
    return std::make_unique<WhileStmt>(
        merge_spans(while_token.span, end_span), std::move(condition), std::move(body));
  }

  StmtPtr parse_return_statement(const Token& return_token) {
    ExprPtr value;
    if (!check(TokenKind::Semicolon)) {
      value = parse_expression();
    }
    const Token& semicolon = consume(TokenKind::Semicolon, "expected ';' after return statement");
    return std::make_unique<ReturnStmt>(
        merge_spans(return_token.span, semicolon.span), std::move(value));
  }

  static bool is_assignable_expr(const Expr& expr) {
    return expr.kind == ExprKind::Identifier || expr.kind == ExprKind::Index;
  }

  StmtPtr parse_expression_or_assignment_statement() {
    ExprPtr expression = parse_expression();
    if (match(TokenKind::Equal)) {
      const Token equal_token = previous();
      if (!is_assignable_expr(*expression)) {
        error_at(equal_token, "left-hand side of assignment must be an identifier or array access");
      }
      ExprPtr value = parse_expression();
      const Token& semicolon = consume(TokenKind::Semicolon, "expected ';' after assignment");
      const SourceSpan statement_span = merge_spans(expression->span, semicolon.span);
      return std::make_unique<AssignmentStmt>(
          statement_span, std::move(expression), std::move(value));
    }

    const Token& semicolon = consume(TokenKind::Semicolon, "expected ';' after expression statement");
    const SourceSpan statement_span = merge_spans(expression->span, semicolon.span);
    return std::make_unique<ExprStmt>(
        statement_span, std::move(expression));
  }

  ExprPtr parse_expression() { return parse_logical_or(); }

  ExprPtr parse_logical_or() {
    ExprPtr expression = parse_logical_and();
    while (match(TokenKind::OrOr)) {
      const Token operator_token = previous();
      ExprPtr right = parse_logical_and();
      const SourceSpan expression_span = merge_spans(expression->span, right->span);
      expression = std::make_unique<BinaryExpr>(
          expression_span,
          operator_token.kind,
          std::move(expression),
          std::move(right));
    }
    return expression;
  }

  ExprPtr parse_logical_and() {
    ExprPtr expression = parse_equality();
    while (match(TokenKind::AndAnd)) {
      const Token operator_token = previous();
      ExprPtr right = parse_equality();
      const SourceSpan expression_span = merge_spans(expression->span, right->span);
      expression = std::make_unique<BinaryExpr>(
          expression_span,
          operator_token.kind,
          std::move(expression),
          std::move(right));
    }
    return expression;
  }

  ExprPtr parse_equality() {
    ExprPtr expression = parse_comparison();
    while (match(TokenKind::EqualEqual) || match(TokenKind::BangEqual)) {
      const Token operator_token = previous();
      ExprPtr right = parse_comparison();
      const SourceSpan expression_span = merge_spans(expression->span, right->span);
      expression = std::make_unique<BinaryExpr>(
          expression_span,
          operator_token.kind,
          std::move(expression),
          std::move(right));
    }
    return expression;
  }

  ExprPtr parse_comparison() {
    ExprPtr expression = parse_additive();
    while (match(TokenKind::Less) || match(TokenKind::LessEqual) || match(TokenKind::Greater) ||
           match(TokenKind::GreaterEqual)) {
      const Token operator_token = previous();
      ExprPtr right = parse_additive();
      const SourceSpan expression_span = merge_spans(expression->span, right->span);
      expression = std::make_unique<BinaryExpr>(
          expression_span,
          operator_token.kind,
          std::move(expression),
          std::move(right));
    }
    return expression;
  }

  ExprPtr parse_additive() {
    ExprPtr expression = parse_multiplicative();
    while (match(TokenKind::Plus) || match(TokenKind::Minus)) {
      const Token operator_token = previous();
      ExprPtr right = parse_multiplicative();
      const SourceSpan expression_span = merge_spans(expression->span, right->span);
      expression = std::make_unique<BinaryExpr>(
          expression_span,
          operator_token.kind,
          std::move(expression),
          std::move(right));
    }
    return expression;
  }

  ExprPtr parse_multiplicative() {
    ExprPtr expression = parse_unary();
    while (match(TokenKind::Star) || match(TokenKind::Slash) || match(TokenKind::Percent)) {
      const Token operator_token = previous();
      ExprPtr right = parse_unary();
      const SourceSpan expression_span = merge_spans(expression->span, right->span);
      expression = std::make_unique<BinaryExpr>(
          expression_span,
          operator_token.kind,
          std::move(expression),
          std::move(right));
    }
    return expression;
  }

  ExprPtr parse_unary() {
    if (match(TokenKind::Bang) || match(TokenKind::Minus)) {
      const Token operator_token = previous();
      ExprPtr operand = parse_unary();
      const SourceSpan expression_span = merge_spans(operator_token.span, operand->span);
      return std::make_unique<UnaryExpr>(
          expression_span, operator_token.kind, std::move(operand));
    }

    return parse_postfix();
  }

  ExprPtr parse_postfix() {
    ExprPtr expression = parse_primary();
    while (true) {
      if (match(TokenKind::LeftParen)) {
        const Token open_paren = previous();
        std::vector<ExprPtr> arguments;
        if (!check(TokenKind::RightParen)) {
          do {
            arguments.push_back(parse_expression());
          } while (match(TokenKind::Comma));
        }
        const Token& close_paren = consume(TokenKind::RightParen, "expected ')' after argument list");
        const SourceSpan expression_span = merge_spans(expression->span, close_paren.span);
        expression = std::make_unique<CallExpr>(
            expression_span, std::move(expression), std::move(arguments));
        (void)open_paren;
        continue;
      }

      if (match(TokenKind::LeftBracket)) {
        ExprPtr index = parse_expression();
        const Token& close_bracket = consume(TokenKind::RightBracket, "expected ']' after array index");
        const SourceSpan expression_span = merge_spans(expression->span, close_bracket.span);
        expression = std::make_unique<IndexExpr>(
            expression_span, std::move(expression), std::move(index));
        continue;
      }

      break;
    }

    return expression;
  }

  ExprPtr parse_primary() {
    if (match(TokenKind::IntegerLiteral)) {
      const Token token = previous();
      return std::make_unique<IntegerLiteralExpr>(
          token.span, std::strtoll(token.lexeme.c_str(), nullptr, 10));
    }

    if (match(TokenKind::True)) {
      return std::make_unique<BooleanLiteralExpr>(previous().span, true);
    }

    if (match(TokenKind::False)) {
      return std::make_unique<BooleanLiteralExpr>(previous().span, false);
    }

    if (match(TokenKind::Identifier)) {
      const Token token = previous();
      return std::make_unique<IdentifierExpr>(token.span, token.lexeme);
    }

    if (match(TokenKind::LeftParen)) {
      const Token open_paren = previous();
      ExprPtr expression = parse_expression();
      const Token& close_paren = consume(TokenKind::RightParen, "expected ')' after expression");
      expression->span = merge_spans(open_paren.span, close_paren.span);
      return expression;
    }

    error_at(peek(), "expected expression");
    const Token unexpected = advance();
    return std::make_unique<IntegerLiteralExpr>(unexpected.span, 0);
  }

  void synchronize_statement() {
    while (!is_at_end()) {
      if (previous().kind == TokenKind::Semicolon) {
        return;
      }

      switch (peek().kind) {
        case TokenKind::Var:
        case TokenKind::If:
        case TokenKind::While:
        case TokenKind::Return:
        case TokenKind::LeftBrace:
        case TokenKind::RightBrace:
          return;
        default:
          break;
      }

      advance();
    }
  }

  void synchronize_declaration() {
    while (!is_at_end()) {
      if (previous().kind == TokenKind::RightBrace || previous().kind == TokenKind::Semicolon) {
        return;
      }

      if (peek().kind == TokenKind::Fn) {
        return;
      }

      advance();
    }
  }

  std::vector<Token> tokens_;
  std::size_t current_ = 0;
  std::vector<Diagnostic> diagnostics_;
};

}  // namespace

ParseResult parse_source(std::string_view source_text) {
  LexResult lexed = lex_source(source_text);
  if (!lexed.diagnostics.empty()) {
    return ParseResult{.program = nullptr, .diagnostics = std::move(lexed.diagnostics)};
  }

  Parser parser(std::move(lexed.tokens));
  return parser.run();
}

std::string_view builtin_type_name(BuiltinTypeKind kind) {
  switch (kind) {
    case BuiltinTypeKind::Int:
      return "int";
    case BuiltinTypeKind::Bool:
      return "bool";
    case BuiltinTypeKind::Void:
      return "void";
  }

  return "int";
}

std::string type_syntax_to_string(const TypeSyntax& type) {
  std::string rendered(builtin_type_name(type.base));
  for (std::int64_t extent : type.array_extents) {
    rendered += '[' + std::to_string(extent) + ']';
  }
  return rendered;
}

}  // namespace nexus::compiler::frontend
