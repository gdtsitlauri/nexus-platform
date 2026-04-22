#include "nexus/compiler/analysis/interprocedural.hpp"

#include <sstream>

namespace nexus::compiler::analysis {

namespace {

struct AbstractValue {
  ConstantValue constant;
  std::string text;
  bool known = false;
};

AbstractValue unknown_value(const ir::Type& type) {
  return AbstractValue{.constant = ConstantValue{.type = type, .known = false}, .text = "?"};
}

AbstractValue int_value(std::int64_t value) {
  return AbstractValue{
      .constant = ConstantValue{.type = ir::Type{.base = ir::BaseTypeKind::Int}, .known = true, .int_value = value},
      .text = std::to_string(value),
      .known = true,
  };
}

AbstractValue bool_value(bool value) {
  return AbstractValue{
      .constant = ConstantValue{.type = ir::Type{.base = ir::BaseTypeKind::Bool}, .known = true, .bool_value = value},
      .text = value ? "true" : "false",
      .known = true,
  };
}

std::optional<ConstantValue> evaluate_binary_const(
    ir::BinaryOp op,
    const ConstantValue& lhs,
    const ConstantValue& rhs) {
  if (!lhs.known || !rhs.known) {
    return std::nullopt;
  }

  if (lhs.type.base == ir::BaseTypeKind::Int && rhs.type.base == ir::BaseTypeKind::Int) {
    switch (op) {
      case ir::BinaryOp::Add:
        return ConstantValue{.type = lhs.type, .known = true, .int_value = lhs.int_value + rhs.int_value};
      case ir::BinaryOp::Sub:
        return ConstantValue{.type = lhs.type, .known = true, .int_value = lhs.int_value - rhs.int_value};
      case ir::BinaryOp::Mul:
        return ConstantValue{.type = lhs.type, .known = true, .int_value = lhs.int_value * rhs.int_value};
      case ir::BinaryOp::Div:
        if (rhs.int_value != 0) {
          return ConstantValue{.type = lhs.type, .known = true, .int_value = lhs.int_value / rhs.int_value};
        }
        return std::nullopt;
      case ir::BinaryOp::Mod:
        if (rhs.int_value != 0) {
          return ConstantValue{.type = lhs.type, .known = true, .int_value = lhs.int_value % rhs.int_value};
        }
        return std::nullopt;
      case ir::BinaryOp::Less:
        return ConstantValue{.type = ir::Type{.base = ir::BaseTypeKind::Bool}, .known = true, .bool_value = lhs.int_value < rhs.int_value};
      case ir::BinaryOp::LessEqual:
        return ConstantValue{.type = ir::Type{.base = ir::BaseTypeKind::Bool}, .known = true, .bool_value = lhs.int_value <= rhs.int_value};
      case ir::BinaryOp::Greater:
        return ConstantValue{.type = ir::Type{.base = ir::BaseTypeKind::Bool}, .known = true, .bool_value = lhs.int_value > rhs.int_value};
      case ir::BinaryOp::GreaterEqual:
        return ConstantValue{.type = ir::Type{.base = ir::BaseTypeKind::Bool}, .known = true, .bool_value = lhs.int_value >= rhs.int_value};
      case ir::BinaryOp::Equal:
        return ConstantValue{.type = ir::Type{.base = ir::BaseTypeKind::Bool}, .known = true, .bool_value = lhs.int_value == rhs.int_value};
      case ir::BinaryOp::NotEqual:
        return ConstantValue{.type = ir::Type{.base = ir::BaseTypeKind::Bool}, .known = true, .bool_value = lhs.int_value != rhs.int_value};
      case ir::BinaryOp::LogicalAnd:
        return ConstantValue{.type = ir::Type{.base = ir::BaseTypeKind::Bool}, .known = true, .bool_value = (lhs.int_value != 0) && (rhs.int_value != 0)};
      case ir::BinaryOp::LogicalOr:
        return ConstantValue{.type = ir::Type{.base = ir::BaseTypeKind::Bool}, .known = true, .bool_value = (lhs.int_value != 0) || (rhs.int_value != 0)};
    }
  }

  if (lhs.type.base == ir::BaseTypeKind::Bool && rhs.type.base == ir::BaseTypeKind::Bool) {
    switch (op) {
      case ir::BinaryOp::Equal:
        return ConstantValue{.type = lhs.type, .known = true, .bool_value = lhs.bool_value == rhs.bool_value};
      case ir::BinaryOp::NotEqual:
        return ConstantValue{.type = lhs.type, .known = true, .bool_value = lhs.bool_value != rhs.bool_value};
      case ir::BinaryOp::LogicalAnd:
        return ConstantValue{.type = lhs.type, .known = true, .bool_value = lhs.bool_value && rhs.bool_value};
      case ir::BinaryOp::LogicalOr:
        return ConstantValue{.type = lhs.type, .known = true, .bool_value = lhs.bool_value || rhs.bool_value};
      default:
        return std::nullopt;
    }
  }

  return std::nullopt;
}

std::optional<ConstantValue> evaluate_unary_const(ir::UnaryOp op, const ConstantValue& operand) {
  if (!operand.known) {
    return std::nullopt;
  }

  if (op == ir::UnaryOp::Negate && operand.type.base == ir::BaseTypeKind::Int) {
    return ConstantValue{.type = operand.type, .known = true, .int_value = -operand.int_value};
  }
  if (op == ir::UnaryOp::LogicalNot && operand.type.base == ir::BaseTypeKind::Bool) {
    return ConstantValue{.type = operand.type, .known = true, .bool_value = !operand.bool_value};
  }
  return std::nullopt;
}

AbstractValue eval_instruction(
    const ir::Instruction& instruction,
    const ir::Function& function,
    std::vector<AbstractValue>& locals,
    const std::vector<AbstractValue>& values,
    bool* pure,
    bool* calls_other) {
  switch (instruction.kind) {
    case ir::InstructionKind::ConstInt:
      return int_value(instruction.int_immediate);
    case ir::InstructionKind::ConstBool:
      return bool_value(instruction.bool_immediate);
    case ir::InstructionKind::LoadLocal:
      return locals[instruction.local];
    case ir::InstructionKind::StoreLocal:
      locals[instruction.local] = values[instruction.operands.front()];
      return unknown_value(ir::Type{});
    case ir::InstructionKind::Unary: {
      const auto folded = evaluate_unary_const(instruction.unary_op, values[instruction.operands.front()].constant);
      if (folded.has_value()) {
        return folded->type.base == ir::BaseTypeKind::Bool ? bool_value(folded->bool_value)
                                                           : int_value(folded->int_value);
      }
      return AbstractValue{
          .constant = ConstantValue{.type = instruction.result_type, .known = false},
          .text = std::string(ir::unary_op_name(instruction.unary_op)) + "(" +
              values[instruction.operands.front()].text + ")",
          .known = false,
      };
    }
    case ir::InstructionKind::Binary: {
      const auto folded = evaluate_binary_const(
          instruction.binary_op,
          values[instruction.operands[0]].constant,
          values[instruction.operands[1]].constant);
      if (folded.has_value()) {
        return folded->type.base == ir::BaseTypeKind::Bool ? bool_value(folded->bool_value)
                                                           : int_value(folded->int_value);
      }
      return AbstractValue{
          .constant = ConstantValue{.type = instruction.result_type, .known = false},
          .text = "(" + values[instruction.operands[0]].text + " " +
              std::string(ir::binary_op_name(instruction.binary_op)) + " " +
              values[instruction.operands[1]].text + ")",
          .known = false,
      };
    }
    case ir::InstructionKind::LoadElement:
    case ir::InstructionKind::StoreElement:
      *pure = false;
      return unknown_value(instruction.result_type);
    case ir::InstructionKind::Call:
      *pure = false;
      *calls_other = true;
      return unknown_value(instruction.result_type);
  }

  (void)function;
  return unknown_value(ir::Type{});
}

}  // namespace

std::string constant_to_string(const ConstantValue& value) {
  if (!value.known) {
    return "?";
  }
  return value.type.base == ir::BaseTypeKind::Bool ? (value.bool_value ? "true" : "false")
                                                   : std::to_string(value.int_value);
}

std::optional<ConstantValue> evaluate_function_with_constants(
    const ir::Function& function,
    const std::vector<ConstantValue>& arguments) {
  if (function.blocks.size() != 1 || !function.blocks.front().terminator.has_value() ||
      function.blocks.front().terminator->kind != ir::TerminatorKind::Return ||
      !function.blocks.front().terminator->return_value.has_value()) {
    return std::nullopt;
  }

  std::vector<AbstractValue> locals(function.locals.size(), unknown_value(ir::Type{}));
  for (std::size_t index = 0; index < function.parameters.size() && index < arguments.size(); ++index) {
    const auto local = function.parameters[index].local;
    locals[local] = AbstractValue{
        .constant = arguments[index],
        .text = constant_to_string(arguments[index]),
        .known = arguments[index].known,
    };
  }
  std::vector<AbstractValue> values(function.values.size(), unknown_value(ir::Type{}));

  bool pure = true;
  bool calls_other = false;
  for (const auto& instruction : function.blocks.front().instructions) {
    const auto value = eval_instruction(instruction, function, locals, values, &pure, &calls_other);
    if (instruction.result.has_value()) {
      values[*instruction.result] = value;
    }
  }

  if (!pure || calls_other) {
    return std::nullopt;
  }

  const auto return_value = values[*function.blocks.front().terminator->return_value].constant;
  if (!return_value.known) {
    return std::nullopt;
  }
  return return_value;
}

InterproceduralResult analyze_interprocedural(const ir::Module& module) {
  InterproceduralResult result;
  result.summaries.reserve(module.functions.size());

  for (const auto& function : module.functions) {
    FunctionSummary summary;
    summary.name = function.name;
    summary.tiny_candidate = function.blocks.size() == 1 && function.blocks.front().instructions.size() <= 8U;

    bool pure = true;
    bool calls_other = false;
    std::vector<AbstractValue> locals(function.locals.size(), unknown_value(ir::Type{}));
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
      const auto local = function.parameters[index].local;
      locals[local] = AbstractValue{
          .constant = ConstantValue{.type = function.parameters[index].type, .known = false},
          .text = "arg" + std::to_string(index),
          .known = false,
      };
    }
    std::vector<AbstractValue> values(function.values.size(), unknown_value(ir::Type{}));

    for (const auto& block : function.blocks) {
      for (const auto& instruction : block.instructions) {
        const auto value = eval_instruction(instruction, function, locals, values, &pure, &calls_other);
        if (instruction.result.has_value()) {
          values[*instruction.result] = value;
        }
        if (instruction.kind == ir::InstructionKind::StoreElement) {
          summary.writes_memory = true;
        }
      }
    }

    summary.pure = pure && !calls_other;
    summary.calls_other_functions = calls_other;
    if (function.blocks.size() == 1 && function.blocks.front().terminator.has_value() &&
        function.blocks.front().terminator->kind == ir::TerminatorKind::Return &&
        function.blocks.front().terminator->return_value.has_value()) {
      const auto return_value = values[*function.blocks.front().terminator->return_value];
      summary.return_summary = return_value.text;
      if (return_value.constant.known) {
        summary.constant_return = return_value.constant;
      }
    } else {
      summary.return_summary = "non-canonical control flow";
    }
    result.summaries.push_back(std::move(summary));
  }

  return result;
}

std::string print_interprocedural(const ir::Module& module, const InterproceduralResult& result) {
  std::ostringstream output;
  output << "interprocedural summaries:\n";
  for (std::size_t index = 0; index < result.summaries.size(); ++index) {
    const auto& summary = result.summaries[index];
    output << "  func " << module.functions[index].name << ": pure=" << (summary.pure ? "yes" : "no")
           << ", tiny=" << (summary.tiny_candidate ? "yes" : "no")
           << ", writes-memory=" << (summary.writes_memory ? "yes" : "no")
           << ", calls=" << (summary.calls_other_functions ? "yes" : "no")
           << ", return=" << summary.return_summary;
    if (summary.constant_return.has_value()) {
      output << " [const " << constant_to_string(*summary.constant_return) << ']';
    }
    output << '\n';
  }
  return output.str();
}

}  // namespace nexus::compiler::analysis
