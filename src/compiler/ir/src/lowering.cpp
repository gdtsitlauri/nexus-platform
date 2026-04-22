#include "nexus/compiler/ir/lowering.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace nexus::compiler::ir {

namespace {

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
using frontend::UnaryExpr;
using frontend::VarDeclStmt;
using frontend::WhileStmt;

Type convert_type(const frontend::TypeSyntax& syntax) {
  BaseTypeKind base = BaseTypeKind::Int;
  switch (syntax.base) {
    case BuiltinTypeKind::Int:
      base = BaseTypeKind::Int;
      break;
    case BuiltinTypeKind::Bool:
      base = BaseTypeKind::Bool;
      break;
    case BuiltinTypeKind::Void:
      base = BaseTypeKind::Void;
      break;
  }
  return Type{.base = base, .array_extents = syntax.array_extents};
}

UnaryOp convert_unary(TokenKind token) {
  switch (token) {
    case TokenKind::Minus:
      return UnaryOp::Negate;
    case TokenKind::Bang:
      return UnaryOp::LogicalNot;
    default:
      return UnaryOp::Negate;
  }
}

BinaryOp convert_binary(TokenKind token) {
  switch (token) {
    case TokenKind::Plus:
      return BinaryOp::Add;
    case TokenKind::Minus:
      return BinaryOp::Sub;
    case TokenKind::Star:
      return BinaryOp::Mul;
    case TokenKind::Slash:
      return BinaryOp::Div;
    case TokenKind::Percent:
      return BinaryOp::Mod;
    case TokenKind::Less:
      return BinaryOp::Less;
    case TokenKind::LessEqual:
      return BinaryOp::LessEqual;
    case TokenKind::Greater:
      return BinaryOp::Greater;
    case TokenKind::GreaterEqual:
      return BinaryOp::GreaterEqual;
    case TokenKind::EqualEqual:
      return BinaryOp::Equal;
    case TokenKind::BangEqual:
      return BinaryOp::NotEqual;
    case TokenKind::AndAnd:
      return BinaryOp::LogicalAnd;
    case TokenKind::OrOr:
      return BinaryOp::LogicalOr;
    default:
      return BinaryOp::Add;
  }
}

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

struct FunctionSignature {
  Type return_type;
};

class FunctionLowerer {
 public:
  FunctionLowerer(
      const FunctionDecl& function_decl,
      const std::unordered_map<std::string, FunctionSignature>& signatures)
      : ast_function_(function_decl), function_signatures_(signatures) {
    function_.name = ast_function_.name;
    function_.return_type = convert_type(ast_function_.return_type);
  }

  std::pair<Function, std::vector<Diagnostic>> lower() {
    push_scope();
    function_.entry_block = create_block("entry");
    current_block_ = function_.entry_block;

    for (const Parameter& parameter : ast_function_.parameters) {
      const Type type = convert_type(parameter.type);
      const LocalId local = reserve_local(parameter.name, type, true);
      bind_local(parameter.name, local, type);
      function_.parameters.push_back(
          ParameterInfo{.name = function_.locals[local].name, .type = type, .local = local});
    }

    for (const auto& statement : ast_function_.body->statements) {
      lower_stmt(*statement);
    }

    if (current_block_.has_value()) {
      if (function_.return_type.is_void()) {
        set_terminator(Terminator{.kind = TerminatorKind::Return});
      } else {
        emit(ast_function_.span, "lowering reached end of non-void function without a return");
      }
    }

    pop_scope();
    return {std::move(function_), std::move(diagnostics_)};
  }

 private:
  struct LocalBinding {
    LocalId local = kInvalidId;
    Type type{};
  };

  struct Reference {
    LocalId local = kInvalidId;
    Type type{};
    std::vector<ValueId> indices;
  };

  struct ExprResult {
    Type type{};
    std::optional<ValueId> value;
    std::optional<Reference> reference;

    [[nodiscard]] bool is_void() const { return type.is_void(); }
    [[nodiscard]] bool is_reference() const { return reference.has_value(); }
  };

  void emit(SourceSpan span, const std::string& message) {
    diagnostics_.push_back(
        Diagnostic{.severity = DiagnosticSeverity::Error, .span = span, .message = message});
  }

  void push_scope() { scopes_.emplace_back(); }

  void pop_scope() { scopes_.pop_back(); }

  LocalId reserve_local(const std::string& source_name, const Type& type, bool is_parameter) {
    const std::size_t version = local_name_counters_[source_name]++;
    std::string unique_name = source_name;
    if (version != 0) {
      unique_name += '$' + std::to_string(version);
    }
    const LocalId id = function_.locals.size();
    function_.locals.push_back(
        LocalInfo{.id = id, .name = std::move(unique_name), .type = type, .is_parameter = is_parameter});
    return id;
  }

  void bind_local(const std::string& source_name, LocalId local, const Type& type) {
    scopes_.back().insert_or_assign(source_name, LocalBinding{.local = local, .type = type});
  }

  const LocalBinding* lookup_local(const std::string& source_name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      const auto found = it->find(source_name);
      if (found != it->end()) {
        return &found->second;
      }
    }
    return nullptr;
  }

  BlockId create_block(const std::string& hint) {
    const BlockId id = function_.blocks.size();
    function_.blocks.push_back(BasicBlock{.id = id, .label = hint});
    return id;
  }

  BasicBlock& current_block() { return function_.blocks[*current_block_]; }

  ValueId create_value(const Type& type) {
    const ValueId id = function_.values.size();
    function_.values.push_back(ValueInfo{.id = id, .type = type});
    return id;
  }

  void append_instruction(Instruction instruction) {
    if (!current_block_.has_value()) {
      return;
    }
    current_block().instructions.push_back(std::move(instruction));
  }

  void set_terminator(Terminator terminator) {
    if (!current_block_.has_value()) {
      return;
    }
    current_block().terminator = std::move(terminator);
    current_block_.reset();
  }

  ValueId emit_const_int(std::int64_t value) {
    const Type type{.base = BaseTypeKind::Int, .array_extents = {}};
    const ValueId result = create_value(type);
    append_instruction(Instruction{
        .kind = InstructionKind::ConstInt,
        .result = result,
        .result_type = type,
        .int_immediate = value,
    });
    return result;
  }

  ValueId emit_const_bool(bool value) {
    const Type type{.base = BaseTypeKind::Bool, .array_extents = {}};
    const ValueId result = create_value(type);
    append_instruction(Instruction{
        .kind = InstructionKind::ConstBool,
        .result = result,
        .result_type = type,
        .bool_immediate = value,
    });
    return result;
  }

  ValueId emit_load_local(LocalId local, const Type& type) {
    const ValueId result = create_value(type);
    append_instruction(Instruction{
        .kind = InstructionKind::LoadLocal,
        .result = result,
        .result_type = type,
        .local = local,
    });
    return result;
  }

  ValueId emit_load_element(LocalId local, const Type& type, const std::vector<ValueId>& indices) {
    const ValueId result = create_value(type);
    append_instruction(Instruction{
        .kind = InstructionKind::LoadElement,
        .result = result,
        .result_type = type,
        .local = local,
        .operands = indices,
    });
    return result;
  }

  void emit_store_local(LocalId local, ValueId value) {
    append_instruction(Instruction{
        .kind = InstructionKind::StoreLocal,
        .local = local,
        .operands = {value},
    });
  }

  void emit_store_element(LocalId local, const std::vector<ValueId>& indices, ValueId value) {
    std::vector<ValueId> operands = indices;
    operands.push_back(value);
    append_instruction(Instruction{
        .kind = InstructionKind::StoreElement,
        .local = local,
        .operands = std::move(operands),
    });
  }

  ValueId emit_unary(UnaryOp op, ValueId operand, const Type& type) {
    const ValueId result = create_value(type);
    append_instruction(Instruction{
        .kind = InstructionKind::Unary,
        .result = result,
        .result_type = type,
        .unary_op = op,
        .operands = {operand},
    });
    return result;
  }

  ValueId emit_binary(BinaryOp op, ValueId lhs, ValueId rhs, const Type& type) {
    const ValueId result = create_value(type);
    append_instruction(Instruction{
        .kind = InstructionKind::Binary,
        .result = result,
        .result_type = type,
        .binary_op = op,
        .operands = {lhs, rhs},
    });
    return result;
  }

  ExprResult emit_call(
      SourceSpan span,
      const std::string& callee,
      const std::vector<CallArgument>& arguments) {
    const auto signature = function_signatures_.find(callee);
    if (signature == function_signatures_.end()) {
      emit(span, "lowering could not resolve function '" + callee + "'");
      return ExprResult{.type = Type{.base = BaseTypeKind::Void, .array_extents = {}}};
    }

    if (signature->second.return_type.is_void()) {
      append_instruction(Instruction{
          .kind = InstructionKind::Call,
          .callee = callee,
          .call_arguments = arguments,
      });
      return ExprResult{.type = signature->second.return_type};
    }

    const ValueId result = create_value(signature->second.return_type);
    append_instruction(Instruction{
        .kind = InstructionKind::Call,
        .result = result,
        .result_type = signature->second.return_type,
        .callee = callee,
        .call_arguments = arguments,
    });
    return ExprResult{.type = signature->second.return_type, .value = result};
  }

  ValueId materialize_scalar(const ExprResult& expression, SourceSpan span, const std::string& context) {
    if (expression.value.has_value()) {
      return *expression.value;
    }

    if (expression.is_void()) {
      emit(span, context + " requires a non-void value");
      return emit_const_int(0);
    }

    if (!expression.reference.has_value()) {
      emit(span, context + " could not be materialized");
      return emit_const_int(0);
    }

    const Reference& reference = *expression.reference;
    if (!reference.type.is_scalar()) {
      emit(span, context + " requires a scalar value but found '" + type_to_string(reference.type) + "'");
      return emit_const_int(0);
    }

    if (reference.indices.empty()) {
      return emit_load_local(reference.local, reference.type);
    }
    return emit_load_element(reference.local, reference.type, reference.indices);
  }

  std::optional<Reference> extract_reference(const ExprResult& expression, SourceSpan span) {
    if (expression.reference.has_value()) {
      return expression.reference;
    }
    emit(span, "expression is not assignable in the current lowering pipeline");
    return std::nullopt;
  }

  ExprResult lower_expr(const Expr& expr) {
    switch (expr.kind) {
      case ExprKind::IntegerLiteral: {
        const auto& literal = static_cast<const IntegerLiteralExpr&>(expr);
        return ExprResult{
            .type = Type{.base = BaseTypeKind::Int, .array_extents = {}},
            .value = emit_const_int(literal.value),
        };
      }
      case ExprKind::BooleanLiteral: {
        const auto& literal = static_cast<const BooleanLiteralExpr&>(expr);
        return ExprResult{
            .type = Type{.base = BaseTypeKind::Bool, .array_extents = {}},
            .value = emit_const_bool(literal.value),
        };
      }
      case ExprKind::Identifier: {
        const auto& identifier = static_cast<const IdentifierExpr&>(expr);
        const LocalBinding* local = lookup_local(identifier.name);
        if (!local) {
          emit(expr.span, "lowering could not resolve local '" + identifier.name + "'");
          return ExprResult{.type = Type{.base = BaseTypeKind::Int, .array_extents = {}}};
        }
        return ExprResult{
            .type = local->type,
            .reference = Reference{.local = local->local, .type = local->type, .indices = {}},
        };
      }
      case ExprKind::Unary: {
        const auto& unary = static_cast<const UnaryExpr&>(expr);
        const ExprResult operand = lower_expr(*unary.operand);
        const ValueId operand_value = materialize_scalar(operand, expr.span, "unary operator");
        return ExprResult{
            .type = operand.type.base == BaseTypeKind::Bool
                        ? Type{.base = BaseTypeKind::Bool, .array_extents = {}}
                        : Type{.base = BaseTypeKind::Int, .array_extents = {}},
            .value = emit_unary(
                convert_unary(unary.op),
                operand_value,
                operand.type.base == BaseTypeKind::Bool
                    ? Type{.base = BaseTypeKind::Bool, .array_extents = {}}
                    : Type{.base = BaseTypeKind::Int, .array_extents = {}}),
        };
      }
      case ExprKind::Binary: {
        const auto& binary = static_cast<const BinaryExpr&>(expr);
        const ExprResult lhs = lower_expr(*binary.left);
        const ExprResult rhs = lower_expr(*binary.right);
        const ValueId lhs_value = materialize_scalar(lhs, expr.span, "binary operator");
        const ValueId rhs_value = materialize_scalar(rhs, expr.span, "binary operator");
        const BinaryOp op = convert_binary(binary.op);
        Type result_type{.base = BaseTypeKind::Int, .array_extents = {}};
        switch (op) {
          case BinaryOp::Less:
          case BinaryOp::LessEqual:
          case BinaryOp::Greater:
          case BinaryOp::GreaterEqual:
          case BinaryOp::Equal:
          case BinaryOp::NotEqual:
          case BinaryOp::LogicalAnd:
          case BinaryOp::LogicalOr:
            result_type = Type{.base = BaseTypeKind::Bool, .array_extents = {}};
            break;
          default:
            result_type = Type{.base = BaseTypeKind::Int, .array_extents = {}};
            break;
        }
        return ExprResult{
            .type = result_type,
            .value = emit_binary(op, lhs_value, rhs_value, result_type),
        };
      }
      case ExprKind::Call: {
        const auto& call = static_cast<const CallExpr&>(expr);
        if (call.callee->kind != ExprKind::Identifier) {
          emit(expr.span, "current lowering only supports direct calls to named functions");
          return ExprResult{.type = Type{.base = BaseTypeKind::Void, .array_extents = {}}};
        }

        const auto& callee = static_cast<const IdentifierExpr&>(*call.callee);
        std::vector<CallArgument> arguments;
        arguments.reserve(call.arguments.size());
        for (const auto& argument : call.arguments) {
          ExprResult lowered_argument = lower_expr(*argument);
          if (lowered_argument.reference.has_value() && lowered_argument.type.is_array()) {
            arguments.push_back(CallArgument{
                .kind = CallArgumentKind::LocalRef,
                .type = lowered_argument.type,
                .local = lowered_argument.reference->local,
                .indices = lowered_argument.reference->indices,
            });
          } else {
            arguments.push_back(CallArgument{
                .kind = CallArgumentKind::Value,
                .type = lowered_argument.type,
                .value = materialize_scalar(lowered_argument, argument->span, "call argument"),
            });
          }
        }
        return emit_call(expr.span, callee.name, arguments);
      }
      case ExprKind::Index: {
        const auto& index = static_cast<const IndexExpr&>(expr);
        const ExprResult base = lower_expr(*index.base);
        const ValueId index_value = materialize_scalar(lower_expr(*index.index), expr.span, "array index");
        if (!base.reference.has_value()) {
          emit(expr.span, "array index base is not addressable in the current lowering pipeline");
          return ExprResult{.type = Type{.base = BaseTypeKind::Int, .array_extents = {}}};
        }
        Reference reference = *base.reference;
        reference.indices.push_back(index_value);
        reference.type = reference.type.element_type();
        return ExprResult{.type = reference.type, .reference = std::move(reference)};
      }
    }

    emit(expr.span, "unsupported expression in the current lowering pipeline");
    return ExprResult{.type = Type{.base = BaseTypeKind::Int, .array_extents = {}}};
  }

  void lower_stmt(const Stmt& stmt) {
    if (!current_block_.has_value()) {
      return;
    }

    switch (stmt.kind) {
      case StmtKind::Block: {
        const auto& block = static_cast<const BlockStmt&>(stmt);
        push_scope();
        for (const auto& child : block.statements) {
          lower_stmt(*child);
        }
        pop_scope();
        return;
      }
      case StmtKind::VarDecl: {
        const auto& decl = static_cast<const VarDeclStmt&>(stmt);
        const Type type = convert_type(decl.type);
        const LocalId local = reserve_local(decl.name, type, false);
        if (decl.initializer) {
          ExprResult initializer = lower_expr(*decl.initializer);
          bind_local(decl.name, local, type);
          if (type.is_array()) {
            emit(decl.span, "current lowering does not support whole-array initializers yet");
            return;
          }
          const ValueId value = materialize_scalar(initializer, decl.initializer->span, "variable initializer");
          emit_store_local(local, value);
          return;
        }
        bind_local(decl.name, local, type);
        return;
      }
      case StmtKind::Expression: {
        const auto& expr_stmt = static_cast<const ExprStmt&>(stmt);
        (void)lower_expr(*expr_stmt.expression);
        return;
      }
      case StmtKind::Assignment: {
        const auto& assignment = static_cast<const AssignmentStmt&>(stmt);
        const ExprResult target_expr = lower_expr(*assignment.target);
        const std::optional<Reference> target = extract_reference(target_expr, assignment.target->span);
        const ExprResult value_expr = lower_expr(*assignment.value);
        if (!target.has_value()) {
          return;
        }
        if (target->type.is_array()) {
          emit(assignment.span, "current lowering does not support whole-array assignment yet");
          return;
        }
        const ValueId value =
            materialize_scalar(value_expr, assignment.value->span, "assignment value");
        if (target->indices.empty()) {
          emit_store_local(target->local, value);
        } else {
          emit_store_element(target->local, target->indices, value);
        }
        return;
      }
      case StmtKind::Return: {
        const auto& return_stmt = static_cast<const ReturnStmt&>(stmt);
        if (return_stmt.value) {
          const ExprResult value_expr = lower_expr(*return_stmt.value);
          const ValueId value = materialize_scalar(value_expr, return_stmt.value->span, "return");
          set_terminator(Terminator{.kind = TerminatorKind::Return, .return_value = value});
        } else {
          set_terminator(Terminator{.kind = TerminatorKind::Return});
        }
        return;
      }
      case StmtKind::If: {
        const auto& if_stmt = static_cast<const IfStmt&>(stmt);
        const ValueId condition =
            materialize_scalar(lower_expr(*if_stmt.condition), if_stmt.condition->span, "if condition");
        const BlockId then_block = create_block("if.then");
        const bool has_else = if_stmt.else_branch != nullptr;
        const bool then_returns = guarantees_return(*if_stmt.then_branch);
        const bool else_returns = has_else && guarantees_return(*if_stmt.else_branch);
        const bool needs_continue = !has_else || !then_returns || !else_returns;
        const BlockId else_block = has_else ? create_block("if.else") : kInvalidId;
        const BlockId continue_block = needs_continue ? create_block("if.cont") : kInvalidId;

        set_terminator(Terminator{
            .kind = TerminatorKind::Branch,
            .condition = condition,
            .true_target = then_block,
            .false_target = has_else ? else_block : continue_block,
        });

        current_block_ = then_block;
        lower_stmt(*if_stmt.then_branch);
        if (current_block_.has_value() && needs_continue) {
          set_terminator(Terminator{.kind = TerminatorKind::Jump, .true_target = continue_block});
        }

        if (has_else) {
          current_block_ = else_block;
          lower_stmt(*if_stmt.else_branch);
          if (current_block_.has_value() && needs_continue) {
            set_terminator(Terminator{.kind = TerminatorKind::Jump, .true_target = continue_block});
          }
        }

        if (needs_continue) {
          current_block_ = continue_block;
        } else {
          current_block_.reset();
        }
        return;
      }
      case StmtKind::While: {
        const auto& while_stmt = static_cast<const WhileStmt&>(stmt);
        const BlockId cond_block = create_block("while.cond");
        const BlockId body_block = create_block("while.body");
        const BlockId continue_block = create_block("while.cont");

        set_terminator(Terminator{.kind = TerminatorKind::Jump, .true_target = cond_block});

        current_block_ = cond_block;
        const ValueId condition =
            materialize_scalar(lower_expr(*while_stmt.condition), while_stmt.condition->span, "while condition");
        set_terminator(Terminator{
            .kind = TerminatorKind::Branch,
            .condition = condition,
            .true_target = body_block,
            .false_target = continue_block,
        });

        current_block_ = body_block;
        lower_stmt(*while_stmt.body);
        if (current_block_.has_value()) {
          set_terminator(Terminator{.kind = TerminatorKind::Jump, .true_target = cond_block});
        }

        current_block_ = continue_block;
        return;
      }
    }
  }

  const FunctionDecl& ast_function_;
  const std::unordered_map<std::string, FunctionSignature>& function_signatures_;
  Function function_;
  std::optional<BlockId> current_block_;
  std::vector<std::unordered_map<std::string, LocalBinding>> scopes_;
  std::unordered_map<std::string, std::size_t> local_name_counters_;
  std::vector<Diagnostic> diagnostics_;
};

}  // namespace

LoweringResult lower_program(const Program& program) {
  std::unordered_map<std::string, FunctionSignature> function_signatures;
  for (const FunctionDecl& function : program.functions) {
    function_signatures.insert_or_assign(
        function.name, FunctionSignature{.return_type = convert_type(function.return_type)});
  }

  auto module = std::make_unique<Module>();
  std::vector<Diagnostic> diagnostics;
  for (const FunctionDecl& function : program.functions) {
    FunctionLowerer lowerer(function, function_signatures);
    auto [lowered_function, function_diagnostics] = lowerer.lower();
    diagnostics.insert(
        diagnostics.end(),
        std::make_move_iterator(function_diagnostics.begin()),
        std::make_move_iterator(function_diagnostics.end()));
    module->functions.push_back(std::move(lowered_function));
  }

  return LoweringResult{.module = std::move(module), .diagnostics = std::move(diagnostics)};
}

}  // namespace nexus::compiler::ir
