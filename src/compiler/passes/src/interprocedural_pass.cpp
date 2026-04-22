#include "nexus/compiler/passes/interprocedural_pass.hpp"

#include <optional>
#include <unordered_map>
#include <vector>

#include "nexus/compiler/analysis/interprocedural.hpp"

namespace nexus::compiler::passes {

namespace {

using nexus::compiler::analysis::ConstantValue;

std::optional<ConstantValue> fold_binary(
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
        return ConstantValue{
            .type = ir::Type{.base = ir::BaseTypeKind::Bool},
            .known = true,
            .bool_value = lhs.int_value < rhs.int_value};
      case ir::BinaryOp::LessEqual:
        return ConstantValue{
            .type = ir::Type{.base = ir::BaseTypeKind::Bool},
            .known = true,
            .bool_value = lhs.int_value <= rhs.int_value};
      case ir::BinaryOp::Greater:
        return ConstantValue{
            .type = ir::Type{.base = ir::BaseTypeKind::Bool},
            .known = true,
            .bool_value = lhs.int_value > rhs.int_value};
      case ir::BinaryOp::GreaterEqual:
        return ConstantValue{
            .type = ir::Type{.base = ir::BaseTypeKind::Bool},
            .known = true,
            .bool_value = lhs.int_value >= rhs.int_value};
      case ir::BinaryOp::Equal:
        return ConstantValue{
            .type = ir::Type{.base = ir::BaseTypeKind::Bool},
            .known = true,
            .bool_value = lhs.int_value == rhs.int_value};
      case ir::BinaryOp::NotEqual:
        return ConstantValue{
            .type = ir::Type{.base = ir::BaseTypeKind::Bool},
            .known = true,
            .bool_value = lhs.int_value != rhs.int_value};
      case ir::BinaryOp::LogicalAnd:
        return ConstantValue{
            .type = ir::Type{.base = ir::BaseTypeKind::Bool},
            .known = true,
            .bool_value = (lhs.int_value != 0) && (rhs.int_value != 0)};
      case ir::BinaryOp::LogicalOr:
        return ConstantValue{
            .type = ir::Type{.base = ir::BaseTypeKind::Bool},
            .known = true,
            .bool_value = (lhs.int_value != 0) || (rhs.int_value != 0)};
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

std::optional<ConstantValue> fold_unary(ir::UnaryOp op, const ConstantValue& operand) {
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

}  // namespace

InterproceduralFoldResult fold_interprocedural_constants(const ir::Module& module) {
  InterproceduralFoldResult result{.module = module};
  const auto summaries = nexus::compiler::analysis::analyze_interprocedural(module);

  std::unordered_map<std::string, std::size_t> summary_index;
  for (std::size_t index = 0; index < summaries.summaries.size(); ++index) {
    summary_index.insert_or_assign(summaries.summaries[index].name, index);
  }

  for (auto& function : result.module.functions) {
    for (auto& block : function.blocks) {
      std::vector<ConstantValue> value_constants(function.values.size(), ConstantValue{});
      std::vector<ConstantValue> local_constants(function.locals.size(), ConstantValue{});
      for (const auto& parameter : function.parameters) {
        local_constants[parameter.local] = ConstantValue{.type = parameter.type, .known = false};
      }

      for (auto& instruction : block.instructions) {
        switch (instruction.kind) {
          case ir::InstructionKind::ConstInt:
            value_constants[*instruction.result] =
                ConstantValue{.type = instruction.result_type, .known = true, .int_value = instruction.int_immediate};
            break;
          case ir::InstructionKind::ConstBool:
            value_constants[*instruction.result] =
                ConstantValue{.type = instruction.result_type, .known = true, .bool_value = instruction.bool_immediate};
            break;
          case ir::InstructionKind::LoadLocal:
            if (instruction.result.has_value()) {
              value_constants[*instruction.result] = local_constants[instruction.local];
              value_constants[*instruction.result].type = instruction.result_type;
            }
            break;
          case ir::InstructionKind::StoreLocal:
            local_constants[instruction.local] = value_constants[instruction.operands.front()];
            break;
          case ir::InstructionKind::Unary:
            if (instruction.result.has_value()) {
              const auto folded = fold_unary(instruction.unary_op, value_constants[instruction.operands.front()]);
              value_constants[*instruction.result] = folded.value_or(ConstantValue{.type = instruction.result_type});
            }
            break;
          case ir::InstructionKind::Binary:
            if (instruction.result.has_value()) {
              const auto folded = fold_binary(
                  instruction.binary_op,
                  value_constants[instruction.operands[0]],
                  value_constants[instruction.operands[1]]);
              value_constants[*instruction.result] = folded.value_or(ConstantValue{.type = instruction.result_type});
              value_constants[*instruction.result].type = instruction.result_type;
            }
            break;
          case ir::InstructionKind::Call: {
            if (!instruction.result.has_value()) {
              break;
            }
            const auto found = summary_index.find(instruction.callee);
            if (found == summary_index.end()) {
              break;
            }
            const auto& summary = summaries.summaries[found->second];
            if (!summary.pure || !summary.tiny_candidate) {
              break;
            }

            std::vector<ConstantValue> arguments;
            bool all_constant = true;
            for (const auto& argument : instruction.call_arguments) {
              if (argument.kind != ir::CallArgumentKind::Value) {
                all_constant = false;
                break;
              }
              const auto value = value_constants[argument.value];
              arguments.push_back(value);
              if (!value.known) {
                all_constant = false;
              }
            }
            if (!all_constant) {
              break;
            }

            const auto evaluated = nexus::compiler::analysis::evaluate_function_with_constants(
                module.functions[found->second],
                arguments);
            if (!evaluated.has_value() || !evaluated->known) {
              break;
            }

            instruction.kind = evaluated->type.base == ir::BaseTypeKind::Bool
                ? ir::InstructionKind::ConstBool
                : ir::InstructionKind::ConstInt;
            instruction.result_type = evaluated->type;
            instruction.operands.clear();
            instruction.call_arguments.clear();
            instruction.callee.clear();
            instruction.local = ir::kInvalidId;
            instruction.int_immediate = evaluated->int_value;
            instruction.bool_immediate = evaluated->bool_value;
            value_constants[*instruction.result] = *evaluated;
            ++result.replaced_calls;
            break;
          }
          case ir::InstructionKind::LoadElement:
          case ir::InstructionKind::StoreElement:
            break;
        }
      }
    }
  }

  result.notes.push_back("interprocedural constant fold replaced " + std::to_string(result.replaced_calls) +
                         " call(s)");
  return result;
}

}  // namespace nexus::compiler::passes
