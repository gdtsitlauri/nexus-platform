#include "nexus/compiler/semantics/semantic_analyzer.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nexus::compiler::semantics {

using frontend::AssignmentStmt;
using frontend::BinaryExpr;
using frontend::BlockStmt;
using frontend::BooleanLiteralExpr;
using frontend::BuiltinTypeKind;
using frontend::CallExpr;
using frontend::Diagnostic;
using frontend::DiagnosticSeverity;
using frontend::Expr;
using frontend::ExprKind;
using frontend::ExprStmt;
using frontend::FunctionDecl;
using frontend::IdentifierExpr;
using frontend::IfStmt;
using frontend::IndexExpr;
using frontend::IntegerLiteralExpr;
using frontend::Parameter;
using frontend::Program;
using frontend::ReturnStmt;
using frontend::SourceSpan;
using frontend::Stmt;
using frontend::StmtKind;
using frontend::TokenKind;
using frontend::TypeSyntax;
using frontend::UnaryExpr;
using frontend::VarDeclStmt;
using frontend::WhileStmt;

namespace {

struct Type {
  BuiltinTypeKind base = BuiltinTypeKind::Int;
  std::vector<std::int64_t> array_extents;

  [[nodiscard]] bool is_array() const { return !array_extents.empty(); }
  [[nodiscard]] bool is_scalar() const { return array_extents.empty() && base != BuiltinTypeKind::Void; }
  [[nodiscard]] bool is_void() const { return base == BuiltinTypeKind::Void && array_extents.empty(); }

  [[nodiscard]] std::string to_string() const {
    std::string rendered(frontend::builtin_type_name(base));
    for (std::int64_t extent : array_extents) {
      rendered += '[' + std::to_string(extent) + ']';
    }
    return rendered;
  }

  [[nodiscard]] Type element_type() const {
    Type element = *this;
    if (!element.array_extents.empty()) {
      element.array_extents.erase(element.array_extents.begin());
    }
    return element;
  }

  friend bool operator==(const Type& lhs, const Type& rhs) {
    return lhs.base == rhs.base && lhs.array_extents == rhs.array_extents;
  }
};

struct FunctionInfo {
  Type return_type;
  std::vector<Type> parameter_types;
  SourceSpan span{};
};

struct SymbolInfo {
  Type type;
  SourceSpan span{};
};

bool guarantees_return(const Stmt& stmt) {
  switch (stmt.kind) {
    case StmtKind::Return:
      return true;
    case StmtKind::Block: {
      const auto& block = static_cast<const BlockStmt&>(stmt);
      for (const auto& child : block.statements) {
        if (guarantees_return(*child)) {
          return true;
        }
      }
      return false;
    }
    case StmtKind::If: {
      const auto& if_stmt = static_cast<const IfStmt&>(stmt);
      return if_stmt.else_branch != nullptr && guarantees_return(*if_stmt.then_branch) &&
             guarantees_return(*if_stmt.else_branch);
    }
    case StmtKind::While:
    case StmtKind::VarDecl:
    case StmtKind::Expression:
    case StmtKind::Assignment:
      return false;
  }

  return false;
}

class Analyzer {
 public:
  SemanticResult run(const Program& program) {
    collect_function_signatures(program);
    for (const FunctionDecl& function : program.functions) {
      analyze_function(function);
    }
    return SemanticResult{.diagnostics = std::move(diagnostics_)};
  }

 private:
  void emit(SourceSpan span, const std::string& message) {
    diagnostics_.push_back(
        Diagnostic{.severity = DiagnosticSeverity::Error, .span = span, .message = message});
  }

  static Type convert_type(const TypeSyntax& syntax) {
    return Type{.base = syntax.base, .array_extents = syntax.array_extents};
  }

  void validate_declared_type(const TypeSyntax& syntax, const std::string& context, bool allow_void) {
    const Type type = convert_type(syntax);
    if (!allow_void && type.base == BuiltinTypeKind::Void) {
      emit(syntax.span, context + " may not use void type");
    }
    if (type.base == BuiltinTypeKind::Void && !type.array_extents.empty()) {
      emit(syntax.span, context + " may not use arrays of void");
    }
    for (std::int64_t extent : type.array_extents) {
      if (extent <= 0) {
        emit(syntax.span, context + " must use strictly positive array extents");
      }
    }
  }

  void collect_function_signatures(const Program& program) {
    for (const FunctionDecl& function : program.functions) {
      if (functions_.contains(function.name)) {
        emit(function.span, "duplicate function definition '" + function.name + "'");
        continue;
      }

      validate_declared_type(function.return_type, "function return type", true);
      Type return_type = convert_type(function.return_type);
      if (return_type.is_array()) {
        emit(function.return_type.span, "Phase 2 only supports scalar or void function return types");
      }

      std::vector<Type> parameter_types;
      parameter_types.reserve(function.parameters.size());
      for (const Parameter& parameter : function.parameters) {
        validate_declared_type(parameter.type, "parameter '" + parameter.name + "'", false);
        parameter_types.push_back(convert_type(parameter.type));
      }

      functions_.emplace(
          function.name,
          FunctionInfo{
              .return_type = std::move(return_type),
              .parameter_types = std::move(parameter_types),
              .span = function.span,
          });
    }
  }

  void analyze_function(const FunctionDecl& function) {
    const FunctionInfo& signature = functions_.at(function.name);
    current_return_type_ = signature.return_type;

    push_scope();
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
      const Parameter& parameter = function.parameters[index];
      declare_symbol(parameter.name, SymbolInfo{.type = signature.parameter_types[index], .span = parameter.span}, true);
    }

    for (const auto& child : function.body->statements) {
      analyze_stmt(*child);
    }

    if (!current_return_type_->is_void() && !guarantees_return(*function.body)) {
      emit(function.span, "not all control paths in function '" + function.name + "' return a value");
    }

    pop_scope();
    current_return_type_.reset();
  }

  void push_scope() { scopes_.emplace_back(); }

  void pop_scope() { scopes_.pop_back(); }

  bool declare_symbol(const std::string& name, SymbolInfo symbol, bool allow_shadow) {
    auto& scope = scopes_.back();
    if (scope.contains(name)) {
      emit(symbol.span, "duplicate definition of '" + name + "' in the same scope");
      return false;
    }
    if (!allow_shadow && scope.contains(name)) {
      return false;
    }
    scope.emplace(name, std::move(symbol));
    return true;
  }

  const SymbolInfo* lookup_symbol(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      auto found = it->find(name);
      if (found != it->end()) {
        return &found->second;
      }
    }
    return nullptr;
  }

  void analyze_stmt(const Stmt& stmt) {
    switch (stmt.kind) {
      case StmtKind::Block: {
        const auto& block = static_cast<const BlockStmt&>(stmt);
        push_scope();
        for (const auto& child : block.statements) {
          analyze_stmt(*child);
        }
        pop_scope();
        return;
      }
      case StmtKind::VarDecl: {
        const auto& decl = static_cast<const VarDeclStmt&>(stmt);
        validate_declared_type(decl.type, "variable '" + decl.name + "'", false);
        const Type declared_type = convert_type(decl.type);
        if (decl.initializer) {
          const std::optional<Type> init_type = analyze_expr(*decl.initializer);
          if (init_type.has_value() && *init_type != declared_type) {
            emit(
                decl.initializer->span,
                "initializer for '" + decl.name + "' has type '" + init_type->to_string() +
                    "' but variable type is '" + declared_type.to_string() + "'");
          }
        }
        declare_symbol(decl.name, SymbolInfo{.type = declared_type, .span = decl.span}, true);
        return;
      }
      case StmtKind::If: {
        const auto& if_stmt = static_cast<const IfStmt&>(stmt);
        require_bool_condition(*if_stmt.condition, "if");
        analyze_stmt(*if_stmt.then_branch);
        if (if_stmt.else_branch) {
          analyze_stmt(*if_stmt.else_branch);
        }
        return;
      }
      case StmtKind::While: {
        const auto& while_stmt = static_cast<const WhileStmt&>(stmt);
        require_bool_condition(*while_stmt.condition, "while");
        analyze_stmt(*while_stmt.body);
        return;
      }
      case StmtKind::Return: {
        const auto& return_stmt = static_cast<const ReturnStmt&>(stmt);
        if (current_return_type_->is_void()) {
          if (return_stmt.value) {
            emit(return_stmt.value->span, "void function may not return a value");
            analyze_expr(*return_stmt.value);
          }
          return;
        }

        if (!return_stmt.value) {
          emit(return_stmt.span, "non-void function must return a value");
          return;
        }

        const std::optional<Type> returned_type = analyze_expr(*return_stmt.value);
        if (returned_type.has_value() && *returned_type != *current_return_type_) {
          emit(
              return_stmt.value->span,
              "return expression has type '" + returned_type->to_string() +
                  "' but function expects '" + current_return_type_->to_string() + "'");
        }
        return;
      }
      case StmtKind::Expression: {
        const auto& expr_stmt = static_cast<const ExprStmt&>(stmt);
        analyze_expr(*expr_stmt.expression);
        return;
      }
      case StmtKind::Assignment: {
        const auto& assignment = static_cast<const AssignmentStmt&>(stmt);
        if (assignment.target->kind != ExprKind::Identifier && assignment.target->kind != ExprKind::Index) {
          emit(assignment.target->span, "assignment target must be an identifier or array access");
        }
        const std::optional<Type> target_type = analyze_expr(*assignment.target);
        const std::optional<Type> value_type = analyze_expr(*assignment.value);
        if (target_type.has_value() && value_type.has_value() && *target_type != *value_type) {
          emit(
              assignment.value->span,
              "assignment value has type '" + value_type->to_string() + "' but target expects '" +
                  target_type->to_string() + "'");
        }
        return;
      }
    }
  }

  void require_bool_condition(const Expr& expr, const std::string& owner) {
    const std::optional<Type> condition_type = analyze_expr(expr);
    if (condition_type.has_value() &&
        !(condition_type->base == BuiltinTypeKind::Bool && condition_type->array_extents.empty())) {
      emit(expr.span, owner + " condition must have type 'bool'");
    }
  }

  std::optional<Type> analyze_expr(const Expr& expr) {
    switch (expr.kind) {
      case ExprKind::IntegerLiteral:
        return Type{.base = BuiltinTypeKind::Int, .array_extents = {}};
      case ExprKind::BooleanLiteral:
        return Type{.base = BuiltinTypeKind::Bool, .array_extents = {}};
      case ExprKind::Identifier: {
        const auto& identifier = static_cast<const IdentifierExpr&>(expr);
        const SymbolInfo* symbol = lookup_symbol(identifier.name);
        if (!symbol) {
          emit(expr.span, "use of undefined identifier '" + identifier.name + "'");
          return std::nullopt;
        }
        return symbol->type;
      }
      case ExprKind::Unary: {
        const auto& unary = static_cast<const UnaryExpr&>(expr);
        const std::optional<Type> operand_type = analyze_expr(*unary.operand);
        if (!operand_type.has_value()) {
          return std::nullopt;
        }
        if (unary.op == TokenKind::Minus) {
          if (!(operand_type->base == BuiltinTypeKind::Int && operand_type->array_extents.empty())) {
            emit(expr.span, "unary '-' expects an int operand");
            return std::nullopt;
          }
          return Type{.base = BuiltinTypeKind::Int, .array_extents = {}};
        }
        if (unary.op == TokenKind::Bang) {
          if (!(operand_type->base == BuiltinTypeKind::Bool && operand_type->array_extents.empty())) {
            emit(expr.span, "logical '!' expects a bool operand");
            return std::nullopt;
          }
          return Type{.base = BuiltinTypeKind::Bool, .array_extents = {}};
        }
        emit(expr.span, "unsupported unary operator in semantic analysis");
        return std::nullopt;
      }
      case ExprKind::Binary: {
        const auto& binary = static_cast<const BinaryExpr&>(expr);
        const std::optional<Type> left = analyze_expr(*binary.left);
        const std::optional<Type> right = analyze_expr(*binary.right);
        if (!left.has_value() || !right.has_value()) {
          return std::nullopt;
        }

        switch (binary.op) {
          case TokenKind::Plus:
          case TokenKind::Minus:
          case TokenKind::Star:
          case TokenKind::Slash:
          case TokenKind::Percent:
            if (left->base != BuiltinTypeKind::Int || right->base != BuiltinTypeKind::Int ||
                left->is_array() || right->is_array()) {
              emit(expr.span, "arithmetic operators require int operands");
              return std::nullopt;
            }
            return Type{.base = BuiltinTypeKind::Int, .array_extents = {}};
          case TokenKind::Less:
          case TokenKind::LessEqual:
          case TokenKind::Greater:
          case TokenKind::GreaterEqual:
            if (left->base != BuiltinTypeKind::Int || right->base != BuiltinTypeKind::Int ||
                left->is_array() || right->is_array()) {
              emit(expr.span, "comparison operators require int operands");
              return std::nullopt;
            }
            return Type{.base = BuiltinTypeKind::Bool, .array_extents = {}};
          case TokenKind::EqualEqual:
          case TokenKind::BangEqual:
            if (!left->is_scalar() || !right->is_scalar() || *left != *right) {
              emit(expr.span, "equality operators require matching scalar operands");
              return std::nullopt;
            }
            return Type{.base = BuiltinTypeKind::Bool, .array_extents = {}};
          case TokenKind::AndAnd:
          case TokenKind::OrOr:
            if (left->base != BuiltinTypeKind::Bool || right->base != BuiltinTypeKind::Bool ||
                left->is_array() || right->is_array()) {
              emit(expr.span, "logical operators require bool operands");
              return std::nullopt;
            }
            return Type{.base = BuiltinTypeKind::Bool, .array_extents = {}};
          default:
            emit(expr.span, "unsupported binary operator in semantic analysis");
            return std::nullopt;
        }
      }
      case ExprKind::Call: {
        const auto& call = static_cast<const CallExpr&>(expr);
        if (call.callee->kind != ExprKind::Identifier) {
          emit(call.callee->span, "Phase 2 only supports direct calls to named functions");
          for (const auto& argument : call.arguments) {
            analyze_expr(*argument);
          }
          return std::nullopt;
        }

        const auto& callee = static_cast<const IdentifierExpr&>(*call.callee);
        const auto function = functions_.find(callee.name);
        if (function == functions_.end()) {
          emit(call.callee->span, "call to undefined function '" + callee.name + "'");
          for (const auto& argument : call.arguments) {
            analyze_expr(*argument);
          }
          return std::nullopt;
        }

        const FunctionInfo& signature = function->second;
        if (call.arguments.size() != signature.parameter_types.size()) {
          emit(
              expr.span,
              "function '" + callee.name + "' expects " +
                  std::to_string(signature.parameter_types.size()) + " argument(s) but received " +
                  std::to_string(call.arguments.size()));
        }

        const std::size_t checked_count =
            std::min(call.arguments.size(), signature.parameter_types.size());
        for (std::size_t index = 0; index < checked_count; ++index) {
          const std::optional<Type> argument_type = analyze_expr(*call.arguments[index]);
          if (argument_type.has_value() && *argument_type != signature.parameter_types[index]) {
            emit(
                call.arguments[index]->span,
                "argument " + std::to_string(index) + " to '" + callee.name + "' has type '" +
                    argument_type->to_string() + "' but expected '" +
                    signature.parameter_types[index].to_string() + "'");
          }
        }
        for (std::size_t index = checked_count; index < call.arguments.size(); ++index) {
          analyze_expr(*call.arguments[index]);
        }
        return signature.return_type;
      }
      case ExprKind::Index: {
        const auto& index = static_cast<const IndexExpr&>(expr);
        const std::optional<Type> base_type = analyze_expr(*index.base);
        const std::optional<Type> index_type = analyze_expr(*index.index);
        if (index_type.has_value() &&
            !(index_type->base == BuiltinTypeKind::Int && index_type->array_extents.empty())) {
          emit(index.index->span, "array index must have type 'int'");
        }
        if (!base_type.has_value()) {
          return std::nullopt;
        }
        if (!base_type->is_array()) {
          emit(index.base->span, "subscripted expression is not an array");
          return std::nullopt;
        }
        return base_type->element_type();
      }
    }

    return std::nullopt;
  }

  std::unordered_map<std::string, FunctionInfo> functions_;
  std::vector<std::unordered_map<std::string, SymbolInfo>> scopes_;
  std::optional<Type> current_return_type_;
  std::vector<Diagnostic> diagnostics_;
};

}  // namespace

SemanticResult analyze_program(const Program& program) {
  Analyzer analyzer;
  return analyzer.run(program);
}

}  // namespace nexus::compiler::semantics
