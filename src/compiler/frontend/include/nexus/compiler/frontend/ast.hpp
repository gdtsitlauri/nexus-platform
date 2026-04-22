#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "nexus/compiler/frontend/diagnostic.hpp"
#include "nexus/compiler/frontend/token.hpp"

namespace nexus::compiler::frontend {

enum class BuiltinTypeKind {
  Int,
  Bool,
  Void,
};

struct TypeSyntax {
  BuiltinTypeKind base = BuiltinTypeKind::Int;
  std::vector<std::int64_t> array_extents;
  SourceSpan span{};
};

std::string_view builtin_type_name(BuiltinTypeKind kind);
std::string type_syntax_to_string(const TypeSyntax& type);

struct Parameter {
  std::string name;
  TypeSyntax type;
  SourceSpan span{};
};

struct Node {
  explicit Node(SourceSpan source_span) : span(source_span) {}
  virtual ~Node() = default;

  SourceSpan span;
};

enum class ExprKind {
  IntegerLiteral,
  BooleanLiteral,
  Identifier,
  Unary,
  Binary,
  Call,
  Index,
};

struct Expr : Node {
  Expr(ExprKind expr_kind, SourceSpan source_span) : Node(source_span), kind(expr_kind) {}
  ~Expr() override = default;

  ExprKind kind;
};

using ExprPtr = std::unique_ptr<Expr>;

struct IntegerLiteralExpr final : Expr {
  IntegerLiteralExpr(SourceSpan source_span, std::int64_t literal_value)
      : Expr(ExprKind::IntegerLiteral, source_span), value(literal_value) {}

  std::int64_t value;
};

struct BooleanLiteralExpr final : Expr {
  BooleanLiteralExpr(SourceSpan source_span, bool literal_value)
      : Expr(ExprKind::BooleanLiteral, source_span), value(literal_value) {}

  bool value;
};

struct IdentifierExpr final : Expr {
  IdentifierExpr(SourceSpan source_span, std::string identifier_name)
      : Expr(ExprKind::Identifier, source_span), name(std::move(identifier_name)) {}

  std::string name;
};

struct UnaryExpr final : Expr {
  UnaryExpr(SourceSpan source_span, TokenKind operator_kind, ExprPtr operand_expr)
      : Expr(ExprKind::Unary, source_span), op(operator_kind), operand(std::move(operand_expr)) {}

  TokenKind op;
  ExprPtr operand;
};

struct BinaryExpr final : Expr {
  BinaryExpr(SourceSpan source_span, TokenKind operator_kind, ExprPtr lhs, ExprPtr rhs)
      : Expr(ExprKind::Binary, source_span),
        op(operator_kind),
        left(std::move(lhs)),
        right(std::move(rhs)) {}

  TokenKind op;
  ExprPtr left;
  ExprPtr right;
};

struct CallExpr final : Expr {
  CallExpr(SourceSpan source_span, ExprPtr callee_expr, std::vector<ExprPtr> call_arguments)
      : Expr(ExprKind::Call, source_span),
        callee(std::move(callee_expr)),
        arguments(std::move(call_arguments)) {}

  ExprPtr callee;
  std::vector<ExprPtr> arguments;
};

struct IndexExpr final : Expr {
  IndexExpr(SourceSpan source_span, ExprPtr base_expr, ExprPtr index_expr)
      : Expr(ExprKind::Index, source_span),
        base(std::move(base_expr)),
        index(std::move(index_expr)) {}

  ExprPtr base;
  ExprPtr index;
};

enum class StmtKind {
  Block,
  VarDecl,
  If,
  While,
  Return,
  Expression,
  Assignment,
};

struct Stmt : Node {
  Stmt(StmtKind stmt_kind, SourceSpan source_span) : Node(source_span), kind(stmt_kind) {}
  ~Stmt() override = default;

  StmtKind kind;
};

using StmtPtr = std::unique_ptr<Stmt>;

struct BlockStmt final : Stmt {
  BlockStmt(SourceSpan source_span, std::vector<StmtPtr> block_statements)
      : Stmt(StmtKind::Block, source_span), statements(std::move(block_statements)) {}

  std::vector<StmtPtr> statements;
};

struct VarDeclStmt final : Stmt {
  VarDeclStmt(
      SourceSpan source_span,
      std::string identifier_name,
      TypeSyntax declared_type,
      ExprPtr init_expr)
      : Stmt(StmtKind::VarDecl, source_span),
        name(std::move(identifier_name)),
        type(std::move(declared_type)),
        initializer(std::move(init_expr)) {}

  std::string name;
  TypeSyntax type;
  ExprPtr initializer;
};

struct IfStmt final : Stmt {
  IfStmt(SourceSpan source_span, ExprPtr cond, StmtPtr then_stmt, StmtPtr else_stmt)
      : Stmt(StmtKind::If, source_span),
        condition(std::move(cond)),
        then_branch(std::move(then_stmt)),
        else_branch(std::move(else_stmt)) {}

  ExprPtr condition;
  StmtPtr then_branch;
  StmtPtr else_branch;
};

struct WhileStmt final : Stmt {
  WhileStmt(SourceSpan source_span, ExprPtr cond, StmtPtr loop_body)
      : Stmt(StmtKind::While, source_span),
        condition(std::move(cond)),
        body(std::move(loop_body)) {}

  ExprPtr condition;
  StmtPtr body;
};

struct ReturnStmt final : Stmt {
  ReturnStmt(SourceSpan source_span, ExprPtr return_value)
      : Stmt(StmtKind::Return, source_span), value(std::move(return_value)) {}

  ExprPtr value;
};

struct ExprStmt final : Stmt {
  ExprStmt(SourceSpan source_span, ExprPtr expr)
      : Stmt(StmtKind::Expression, source_span), expression(std::move(expr)) {}

  ExprPtr expression;
};

struct AssignmentStmt final : Stmt {
  AssignmentStmt(SourceSpan source_span, ExprPtr assignment_target, ExprPtr assignment_value)
      : Stmt(StmtKind::Assignment, source_span),
        target(std::move(assignment_target)),
        value(std::move(assignment_value)) {}

  ExprPtr target;
  ExprPtr value;
};

struct FunctionDecl final : Node {
  FunctionDecl(
      SourceSpan source_span,
      std::string function_name,
      std::vector<Parameter> function_parameters,
      TypeSyntax function_return_type,
      std::unique_ptr<BlockStmt> function_body)
      : Node(source_span),
        name(std::move(function_name)),
        parameters(std::move(function_parameters)),
        return_type(std::move(function_return_type)),
        body(std::move(function_body)) {}

  std::string name;
  std::vector<Parameter> parameters;
  TypeSyntax return_type;
  std::unique_ptr<BlockStmt> body;
};

struct Program final : Node {
  Program(SourceSpan source_span, std::vector<FunctionDecl> function_list)
      : Node(source_span), functions(std::move(function_list)) {}

  std::vector<FunctionDecl> functions;
};

}  // namespace nexus::compiler::frontend
