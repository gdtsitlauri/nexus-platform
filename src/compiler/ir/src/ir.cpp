#include "nexus/compiler/ir/ir.hpp"

#include <stdexcept>

namespace nexus::compiler::ir {

Type Type::element_type() const {
  Type element = *this;
  if (!element.array_extents.empty()) {
    element.array_extents.erase(element.array_extents.begin());
  }
  return element;
}

bool operator==(const Type& lhs, const Type& rhs) {
  return lhs.base == rhs.base && lhs.array_extents == rhs.array_extents;
}

std::string_view base_type_name(BaseTypeKind kind) {
  switch (kind) {
    case BaseTypeKind::Int:
      return "int";
    case BaseTypeKind::Bool:
      return "bool";
    case BaseTypeKind::Void:
      return "void";
  }

  return "int";
}

std::string type_to_string(const Type& type) {
  std::string rendered(base_type_name(type.base));
  for (std::int64_t extent : type.array_extents) {
    rendered += '[' + std::to_string(extent) + ']';
  }
  return rendered;
}

std::string_view unary_op_name(UnaryOp op) {
  switch (op) {
    case UnaryOp::Negate:
      return "neg";
    case UnaryOp::LogicalNot:
      return "not";
  }

  return "neg";
}

std::string_view binary_op_name(BinaryOp op) {
  switch (op) {
    case BinaryOp::Add:
      return "add";
    case BinaryOp::Sub:
      return "sub";
    case BinaryOp::Mul:
      return "mul";
    case BinaryOp::Div:
      return "div";
    case BinaryOp::Mod:
      return "mod";
    case BinaryOp::Less:
      return "lt";
    case BinaryOp::LessEqual:
      return "le";
    case BinaryOp::Greater:
      return "gt";
    case BinaryOp::GreaterEqual:
      return "ge";
    case BinaryOp::Equal:
      return "eq";
    case BinaryOp::NotEqual:
      return "ne";
    case BinaryOp::LogicalAnd:
      return "and";
    case BinaryOp::LogicalOr:
      return "or";
  }

  return "add";
}

std::string value_name(ValueId id) { return '%' + std::to_string(id); }

const LocalInfo& local_info(const Function& function, LocalId id) {
  if (id >= function.locals.size()) {
    throw std::out_of_range("invalid local id");
  }
  return function.locals[id];
}

const ValueInfo& value_info(const Function& function, ValueId id) {
  if (id >= function.values.size()) {
    throw std::out_of_range("invalid value id");
  }
  return function.values[id];
}

std::vector<ValueId> instruction_uses(const Instruction& instruction) {
  std::vector<ValueId> uses = instruction.operands;
  if (instruction.kind == InstructionKind::Call) {
    for (const CallArgument& argument : instruction.call_arguments) {
      if (argument.kind == CallArgumentKind::Value) {
        uses.push_back(argument.value);
      }
      uses.insert(uses.end(), argument.indices.begin(), argument.indices.end());
    }
  }
  return uses;
}

std::optional<ValueId> instruction_def(const Instruction& instruction) { return instruction.result; }

std::vector<ValueId> terminator_uses(const Terminator& terminator) {
  std::vector<ValueId> uses;
  if (terminator.condition.has_value()) {
    uses.push_back(*terminator.condition);
  }
  if (terminator.return_value.has_value()) {
    uses.push_back(*terminator.return_value);
  }
  return uses;
}

}  // namespace nexus::compiler::ir
