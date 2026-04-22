#include "nexus/compiler/analysis/symbolic.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace nexus::compiler::analysis {

namespace {

SymbolicValue unknown_symbol(const ir::Type& type) {
  return SymbolicValue{.type = type, .kind = SymbolicKind::Unknown};
}

SymbolicValue named_symbol(const ir::Type& type, std::string text) {
  return SymbolicValue{.type = type, .kind = SymbolicKind::Named, .text = std::move(text)};
}

SymbolicValue expression_symbol(const ir::Type& type, std::string text) {
  return SymbolicValue{.type = type, .kind = SymbolicKind::Expression, .text = std::move(text)};
}

SymbolicValue int_symbol(std::int64_t value) {
  return SymbolicValue{
      .type = ir::Type{.base = ir::BaseTypeKind::Int},
      .kind = SymbolicKind::IntegerConstant,
      .int_value = value,
      .text = std::to_string(value),
  };
}

SymbolicValue bool_symbol(bool value) {
  return SymbolicValue{
      .type = ir::Type{.base = ir::BaseTypeKind::Bool},
      .kind = SymbolicKind::BooleanConstant,
      .bool_value = value,
      .text = value ? "true" : "false",
  };
}

bool is_true(const SymbolicValue& value) {
  return value.kind == SymbolicKind::BooleanConstant && value.bool_value;
}

bool is_false(const SymbolicValue& value) {
  return value.kind == SymbolicKind::BooleanConstant && !value.bool_value;
}

bool is_zero(const SymbolicValue& value) {
  return value.kind == SymbolicKind::IntegerConstant && value.int_value == 0;
}

bool is_one(const SymbolicValue& value) {
  return value.kind == SymbolicKind::IntegerConstant && value.int_value == 1;
}

std::string text_of(const SymbolicValue& value) {
  if (!value.text.empty()) {
    return value.text;
  }
  if (value.kind == SymbolicKind::IntegerConstant) {
    return std::to_string(value.int_value);
  }
  if (value.kind == SymbolicKind::BooleanConstant) {
    return value.bool_value ? "true" : "false";
  }
  return "?";
}

std::string local_env_text(const ir::Function& function, const std::vector<SymbolicValue>& locals) {
  std::ostringstream output;
  output << '{';
  bool first = true;
  for (std::size_t index = 0; index < locals.size(); ++index) {
    if (locals[index].kind == SymbolicKind::Unknown) {
      continue;
    }
    if (!first) {
      output << ", ";
    }
    output << ir::local_info(function, index).name << " = " << text_of(locals[index]);
    first = false;
  }
  output << '}';
  return output.str();
}

std::vector<SymbolicValue> boundary_locals(const ir::Function& function) {
  std::vector<SymbolicValue> locals(function.locals.size(), unknown_symbol(ir::Type{}));
  for (const auto& local : function.locals) {
    locals[local.id] = local.is_parameter ? named_symbol(local.type, local.name) : unknown_symbol(local.type);
  }
  return locals;
}

std::vector<SymbolicValue> meet_locals(
    const ir::Function& function,
    const std::vector<std::vector<SymbolicValue>>& out_sets,
    const std::vector<ir::BlockId>& predecessors,
    bool is_entry) {
  if (predecessors.empty()) {
    return boundary_locals(function);
  }

  std::vector<SymbolicValue> merged = out_sets[predecessors.front()];
  for (std::size_t pred_index = 1; pred_index < predecessors.size(); ++pred_index) {
    const auto& next = out_sets[predecessors[pred_index]];
    for (std::size_t local = 0; local < merged.size(); ++local) {
      if (!(merged[local] == next[local])) {
        merged[local] = unknown_symbol(function.locals[local].type);
      }
    }
  }

  if (is_entry) {
    const auto boundary = boundary_locals(function);
    for (std::size_t local = 0; local < merged.size(); ++local) {
      if (boundary[local].kind != SymbolicKind::Unknown && merged[local].kind == SymbolicKind::Unknown) {
        merged[local] = boundary[local];
      }
    }
  }

  return merged;
}

std::optional<SymbolicValue> fold_binary_constants(
    ir::BinaryOp op,
    const SymbolicValue& lhs,
    const SymbolicValue& rhs,
    const ir::Type& result_type) {
  if (lhs.kind == SymbolicKind::IntegerConstant && rhs.kind == SymbolicKind::IntegerConstant) {
    switch (op) {
      case ir::BinaryOp::Add:
        return int_symbol(lhs.int_value + rhs.int_value);
      case ir::BinaryOp::Sub:
        return int_symbol(lhs.int_value - rhs.int_value);
      case ir::BinaryOp::Mul:
        return int_symbol(lhs.int_value * rhs.int_value);
      case ir::BinaryOp::Div:
        if (rhs.int_value != 0) {
          return int_symbol(lhs.int_value / rhs.int_value);
        }
        return std::nullopt;
      case ir::BinaryOp::Mod:
        if (rhs.int_value != 0) {
          return int_symbol(lhs.int_value % rhs.int_value);
        }
        return std::nullopt;
      case ir::BinaryOp::Less:
        return bool_symbol(lhs.int_value < rhs.int_value);
      case ir::BinaryOp::LessEqual:
        return bool_symbol(lhs.int_value <= rhs.int_value);
      case ir::BinaryOp::Greater:
        return bool_symbol(lhs.int_value > rhs.int_value);
      case ir::BinaryOp::GreaterEqual:
        return bool_symbol(lhs.int_value >= rhs.int_value);
      case ir::BinaryOp::Equal:
        return bool_symbol(lhs.int_value == rhs.int_value);
      case ir::BinaryOp::NotEqual:
        return bool_symbol(lhs.int_value != rhs.int_value);
      case ir::BinaryOp::LogicalAnd:
        return bool_symbol((lhs.int_value != 0) && (rhs.int_value != 0));
      case ir::BinaryOp::LogicalOr:
        return bool_symbol((lhs.int_value != 0) || (rhs.int_value != 0));
    }
  }

  if (lhs.kind == SymbolicKind::BooleanConstant && rhs.kind == SymbolicKind::BooleanConstant) {
    switch (op) {
      case ir::BinaryOp::Equal:
        return bool_symbol(lhs.bool_value == rhs.bool_value);
      case ir::BinaryOp::NotEqual:
        return bool_symbol(lhs.bool_value != rhs.bool_value);
      case ir::BinaryOp::LogicalAnd:
        return bool_symbol(lhs.bool_value && rhs.bool_value);
      case ir::BinaryOp::LogicalOr:
        return bool_symbol(lhs.bool_value || rhs.bool_value);
      default:
        return std::nullopt;
    }
  }

  (void)result_type;
  return std::nullopt;
}

SymbolicValue simplify_binary(
    ir::BinaryOp op,
    const SymbolicValue& lhs,
    const SymbolicValue& rhs,
    const ir::Type& result_type) {
  if (const auto folded = fold_binary_constants(op, lhs, rhs, result_type); folded.has_value()) {
    return *folded;
  }

  switch (op) {
    case ir::BinaryOp::Add:
      if (is_zero(lhs)) {
        return rhs;
      }
      if (is_zero(rhs)) {
        return lhs;
      }
      break;
    case ir::BinaryOp::Sub:
      if (is_zero(rhs)) {
        return lhs;
      }
      if (text_of(lhs) == text_of(rhs)) {
        return int_symbol(0);
      }
      break;
    case ir::BinaryOp::Mul:
      if (is_zero(lhs) || is_zero(rhs)) {
        return int_symbol(0);
      }
      if (is_one(lhs)) {
        return rhs;
      }
      if (is_one(rhs)) {
        return lhs;
      }
      break;
    case ir::BinaryOp::Div:
      if (is_one(rhs)) {
        return lhs;
      }
      break;
    case ir::BinaryOp::Equal:
      if (text_of(lhs) == text_of(rhs)) {
        return bool_symbol(true);
      }
      break;
    case ir::BinaryOp::NotEqual:
      if (text_of(lhs) == text_of(rhs)) {
        return bool_symbol(false);
      }
      break;
    case ir::BinaryOp::LogicalAnd:
      if (is_false(lhs) || is_false(rhs)) {
        return bool_symbol(false);
      }
      if (is_true(lhs)) {
        return rhs;
      }
      if (is_true(rhs)) {
        return lhs;
      }
      break;
    case ir::BinaryOp::LogicalOr:
      if (is_true(lhs) || is_true(rhs)) {
        return bool_symbol(true);
      }
      if (is_false(lhs)) {
        return rhs;
      }
      if (is_false(rhs)) {
        return lhs;
      }
      break;
    case ir::BinaryOp::Less:
      if (text_of(lhs) == text_of(rhs)) {
        return bool_symbol(false);
      }
      break;
    case ir::BinaryOp::LessEqual:
      if (text_of(lhs) == text_of(rhs)) {
        return bool_symbol(true);
      }
      break;
    case ir::BinaryOp::Greater:
      if (text_of(lhs) == text_of(rhs)) {
        return bool_symbol(false);
      }
      break;
    case ir::BinaryOp::GreaterEqual:
      if (text_of(lhs) == text_of(rhs)) {
        return bool_symbol(true);
      }
      break;
    case ir::BinaryOp::Mod:
      break;
  }

  return expression_symbol(
      result_type,
      "(" + text_of(lhs) + " " + std::string(ir::binary_op_name(op)) + " " + text_of(rhs) + ")");
}

SymbolicValue simplify_unary(ir::UnaryOp op, const SymbolicValue& operand, const ir::Type& result_type) {
  if (op == ir::UnaryOp::Negate && operand.kind == SymbolicKind::IntegerConstant) {
    return int_symbol(-operand.int_value);
  }
  if (op == ir::UnaryOp::LogicalNot && operand.kind == SymbolicKind::BooleanConstant) {
    return bool_symbol(!operand.bool_value);
  }
  return expression_symbol(result_type, std::string(ir::unary_op_name(op)) + "(" + text_of(operand) + ")");
}

}  // namespace

bool operator==(const SymbolicValue& lhs, const SymbolicValue& rhs) {
  return lhs.type == rhs.type && lhs.kind == rhs.kind && lhs.int_value == rhs.int_value &&
      lhs.bool_value == rhs.bool_value && lhs.text == rhs.text;
}

SymbolicResult analyze_symbolic(const ir::Function& function, const ControlFlowGraph& cfg) {
  SymbolicResult result;
  result.block_in_locals.assign(
      function.blocks.size(), std::vector<SymbolicValue>(function.locals.size(), unknown_symbol(ir::Type{})));
  result.block_out_locals.assign(
      function.blocks.size(), std::vector<SymbolicValue>(function.locals.size(), unknown_symbol(ir::Type{})));
  result.value_symbols.assign(function.values.size(), unknown_symbol(ir::Type{}));

  bool changed = true;
  while (changed) {
    changed = false;
    ++result.iterations;

    for (const auto& block : function.blocks) {
      auto in_locals =
          meet_locals(function, result.block_out_locals, cfg.predecessors[block.id], block.id == cfg.entry_block);
      auto out_locals = in_locals;
      auto new_value_symbols = result.value_symbols;

      auto symbol_for_value = [&](ir::ValueId value) -> SymbolicValue {
        if (value >= new_value_symbols.size()) {
          return unknown_symbol(ir::Type{});
        }
        return new_value_symbols[value];
      };

      for (const auto& instruction : block.instructions) {
        switch (instruction.kind) {
          case ir::InstructionKind::ConstInt:
            new_value_symbols[*instruction.result] = int_symbol(instruction.int_immediate);
            break;
          case ir::InstructionKind::ConstBool:
            new_value_symbols[*instruction.result] = bool_symbol(instruction.bool_immediate);
            break;
          case ir::InstructionKind::LoadLocal:
            new_value_symbols[*instruction.result] =
                out_locals[instruction.local].kind == SymbolicKind::Unknown
                ? named_symbol(instruction.result_type, ir::local_info(function, instruction.local).name)
                : out_locals[instruction.local];
            break;
          case ir::InstructionKind::StoreLocal:
            out_locals[instruction.local] = symbol_for_value(instruction.operands.front());
            break;
          case ir::InstructionKind::LoadElement: {
            std::ostringstream text;
            text << ir::local_info(function, instruction.local).name;
            for (ir::ValueId index : instruction.operands) {
              text << '[' << text_of(symbol_for_value(index)) << ']';
            }
            new_value_symbols[*instruction.result] = expression_symbol(instruction.result_type, text.str());
            break;
          }
          case ir::InstructionKind::StoreElement:
            break;
          case ir::InstructionKind::Unary:
            new_value_symbols[*instruction.result] = simplify_unary(
                instruction.unary_op,
                symbol_for_value(instruction.operands.front()),
                instruction.result_type);
            break;
          case ir::InstructionKind::Binary:
            new_value_symbols[*instruction.result] = simplify_binary(
                instruction.binary_op,
                symbol_for_value(instruction.operands[0]),
                symbol_for_value(instruction.operands[1]),
                instruction.result_type);
            break;
          case ir::InstructionKind::Call:
            if (instruction.result.has_value()) {
              new_value_symbols[*instruction.result] = unknown_symbol(instruction.result_type);
            }
            break;
        }
      }

      if (result.block_in_locals[block.id] != in_locals || result.block_out_locals[block.id] != out_locals ||
          result.value_symbols != new_value_symbols) {
        changed = true;
        result.block_in_locals[block.id] = std::move(in_locals);
        result.block_out_locals[block.id] = std::move(out_locals);
        result.value_symbols = std::move(new_value_symbols);
      }
    }
  }

  return result;
}

std::string print_symbolic(
    const ir::Function& function,
    const ControlFlowGraph& cfg,
    const SymbolicResult& result) {
  std::ostringstream output;
  output << "func " << function.name << " symbolic:\n";
  output << "  iterations: " << result.iterations << '\n';
  for (const auto& block : function.blocks) {
    output << "  bb" << block.id << '.' << block.label << ":\n";
    output << "    in locals:  " << local_env_text(function, result.block_in_locals[block.id]) << '\n';
    for (const auto& instruction : block.instructions) {
      if (instruction.result.has_value()) {
        output << "    " << ir::value_name(*instruction.result) << " -> "
               << text_of(result.value_symbols[*instruction.result]) << '\n';
      }
    }
    output << "    out locals: " << local_env_text(function, result.block_out_locals[block.id]) << '\n';
  }
  (void)cfg;
  return output.str();
}

}  // namespace nexus::compiler::analysis
