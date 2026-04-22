#include "nexus/compiler/frontend/ast_printer.hpp"

#include <sstream>
#include <string>

namespace nexus::compiler::frontend {

namespace {

std::string indent(int depth) {
  return std::string(static_cast<std::size_t>(depth) * 2, ' ');
}

void print_expr(const Expr& expr, int depth, std::ostringstream& output);
void print_stmt(const Stmt& stmt, int depth, std::ostringstream& output);

void print_expr(const Expr& expr, int depth, std::ostringstream& output) {
  switch (expr.kind) {
    case ExprKind::IntegerLiteral: {
      const auto& literal = static_cast<const IntegerLiteralExpr&>(expr);
      output << indent(depth) << "Integer " << literal.value << '\n';
      return;
    }
    case ExprKind::BooleanLiteral: {
      const auto& literal = static_cast<const BooleanLiteralExpr&>(expr);
      output << indent(depth) << "Boolean " << (literal.value ? "true" : "false") << '\n';
      return;
    }
    case ExprKind::Identifier: {
      const auto& identifier = static_cast<const IdentifierExpr&>(expr);
      output << indent(depth) << "Identifier " << identifier.name << '\n';
      return;
    }
    case ExprKind::Unary: {
      const auto& unary = static_cast<const UnaryExpr&>(expr);
      output << indent(depth) << "Unary " << token_kind_name(unary.op) << '\n';
      print_expr(*unary.operand, depth + 1, output);
      return;
    }
    case ExprKind::Binary: {
      const auto& binary = static_cast<const BinaryExpr&>(expr);
      output << indent(depth) << "Binary " << token_kind_name(binary.op) << '\n';
      print_expr(*binary.left, depth + 1, output);
      print_expr(*binary.right, depth + 1, output);
      return;
    }
    case ExprKind::Call: {
      const auto& call = static_cast<const CallExpr&>(expr);
      output << indent(depth) << "Call\n";
      output << indent(depth + 1) << "Callee\n";
      print_expr(*call.callee, depth + 2, output);
      if (!call.arguments.empty()) {
        output << indent(depth + 1) << "Arguments\n";
        for (const auto& argument : call.arguments) {
          print_expr(*argument, depth + 2, output);
        }
      }
      return;
    }
    case ExprKind::Index: {
      const auto& index = static_cast<const IndexExpr&>(expr);
      output << indent(depth) << "Index\n";
      output << indent(depth + 1) << "Base\n";
      print_expr(*index.base, depth + 2, output);
      output << indent(depth + 1) << "Subscript\n";
      print_expr(*index.index, depth + 2, output);
      return;
    }
  }
}

void print_stmt(const Stmt& stmt, int depth, std::ostringstream& output) {
  switch (stmt.kind) {
    case StmtKind::Block: {
      const auto& block = static_cast<const BlockStmt&>(stmt);
      output << indent(depth) << "Block\n";
      for (const auto& child : block.statements) {
        print_stmt(*child, depth + 1, output);
      }
      return;
    }
    case StmtKind::VarDecl: {
      const auto& decl = static_cast<const VarDeclStmt&>(stmt);
      output << indent(depth) << "VarDecl " << decl.name << ": " << type_syntax_to_string(decl.type)
             << '\n';
      if (decl.initializer) {
        print_expr(*decl.initializer, depth + 1, output);
      }
      return;
    }
    case StmtKind::If: {
      const auto& if_stmt = static_cast<const IfStmt&>(stmt);
      output << indent(depth) << "If\n";
      output << indent(depth + 1) << "Condition\n";
      print_expr(*if_stmt.condition, depth + 2, output);
      output << indent(depth + 1) << "Then\n";
      print_stmt(*if_stmt.then_branch, depth + 2, output);
      if (if_stmt.else_branch) {
        output << indent(depth + 1) << "Else\n";
        print_stmt(*if_stmt.else_branch, depth + 2, output);
      }
      return;
    }
    case StmtKind::While: {
      const auto& while_stmt = static_cast<const WhileStmt&>(stmt);
      output << indent(depth) << "While\n";
      output << indent(depth + 1) << "Condition\n";
      print_expr(*while_stmt.condition, depth + 2, output);
      output << indent(depth + 1) << "Body\n";
      print_stmt(*while_stmt.body, depth + 2, output);
      return;
    }
    case StmtKind::Return: {
      const auto& return_stmt = static_cast<const ReturnStmt&>(stmt);
      output << indent(depth) << "Return\n";
      if (return_stmt.value) {
        print_expr(*return_stmt.value, depth + 1, output);
      }
      return;
    }
    case StmtKind::Expression: {
      const auto& expr_stmt = static_cast<const ExprStmt&>(stmt);
      output << indent(depth) << "ExprStmt\n";
      print_expr(*expr_stmt.expression, depth + 1, output);
      return;
    }
    case StmtKind::Assignment: {
      const auto& assignment = static_cast<const AssignmentStmt&>(stmt);
      output << indent(depth) << "Assignment\n";
      output << indent(depth + 1) << "Target\n";
      print_expr(*assignment.target, depth + 2, output);
      output << indent(depth + 1) << "Value\n";
      print_expr(*assignment.value, depth + 2, output);
      return;
    }
  }
}

}  // namespace

std::string print_program(const Program& program) {
  std::ostringstream output;
  output << "Program\n";
  for (const FunctionDecl& function : program.functions) {
    output << indent(1) << "Function " << function.name << '(';
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
      if (index != 0) {
        output << ", ";
      }
      output << function.parameters[index].name << ": "
             << type_syntax_to_string(function.parameters[index].type);
    }
    output << ") -> " << type_syntax_to_string(function.return_type) << '\n';
    print_stmt(*function.body, 2, output);
  }
  return output.str();
}

}  // namespace nexus::compiler::frontend
