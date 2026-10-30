#include "nexus/compiler/passes/value_numbering.hpp"

#include <algorithm>
#include <map>
#include <string>

namespace nexus::compiler::passes {

namespace {

bool commutative(ir::BinaryOp op) {
  switch (op) {
    case ir::BinaryOp::Add:
    case ir::BinaryOp::Mul:
    case ir::BinaryOp::Equal:
    case ir::BinaryOp::NotEqual:
    case ir::BinaryOp::LogicalAnd:
    case ir::BinaryOp::LogicalOr:
      return true;
    default:
      return false;
  }
}

class FunctionNumbering {
 public:
  explicit FunctionNumbering(ir::Function& function) : function_(function) {}

  void run(ValueNumberingResult& result) {
    for (auto& block : function_.blocks) {
      number_block(block, result);
    }
    // Rewrite every remaining use (the first occurrence always comes earlier in the same block).
    for (auto& block : function_.blocks) {
      for (auto& instruction : block.instructions) {
        for (auto& operand : instruction.operands) operand = find(operand);
        for (auto& argument : instruction.call_arguments) {
          if (argument.value != ir::kInvalidId) argument.value = find(argument.value);
          for (auto& index : argument.indices) index = find(index);
        }
      }
      if (block.terminator.has_value()) {
        if (block.terminator->condition) block.terminator->condition = find(*block.terminator->condition);
        if (block.terminator->return_value) block.terminator->return_value = find(*block.terminator->return_value);
      }
    }
  }

 private:
  ir::ValueId find(ir::ValueId value) const {
    auto found = replaced_.find(value);
    while (found != replaced_.end()) {
      value = found->second;
      found = replaced_.find(value);
    }
    return value;
  }

  std::string load_key(ir::LocalId local) { return "load " + std::to_string(local) + "#" + std::to_string(version_[local]); }

  void number_block(ir::BasicBlock& block, ValueNumberingResult& result) {
    std::map<std::string, ir::ValueId> table;  // expression -> value that already holds it
    std::map<std::string, bool> stored;         // load keys whose value comes from a store in this block
    version_.clear();
    std::vector<ir::Instruction> kept;
    for (auto instruction : block.instructions) {
      for (auto& operand : instruction.operands) operand = find(operand);
      std::string key;
      switch (instruction.kind) {
        case ir::InstructionKind::ConstInt:
          key = "int " + std::to_string(instruction.int_immediate);
          break;
        case ir::InstructionKind::ConstBool:
          key = std::string("bool ") + (instruction.bool_immediate ? "1" : "0");
          break;
        case ir::InstructionKind::LoadLocal:
          key = load_key(instruction.local);
          break;
        case ir::InstructionKind::LoadElement:
          key = "element " + std::to_string(instruction.local) + "#" + std::to_string(version_[instruction.local]);
          for (const auto index : instruction.operands) key += " " + std::to_string(index);
          break;
        case ir::InstructionKind::Unary:
          key = "unary " + std::to_string(static_cast<int>(instruction.unary_op)) + " " +
                std::to_string(instruction.operands.at(0));
          break;
        case ir::InstructionKind::Binary: {
          auto lhs = instruction.operands.at(0);
          auto rhs = instruction.operands.at(1);
          if (commutative(instruction.binary_op) && rhs < lhs) std::swap(lhs, rhs);
          key = "binary " + std::to_string(static_cast<int>(instruction.binary_op)) + " " + std::to_string(lhs) + " " +
                std::to_string(rhs);
          break;
        }
        case ir::InstructionKind::StoreLocal:
          ++version_[instruction.local];
          if (!instruction.operands.empty()) {
            table[load_key(instruction.local)] = instruction.operands.front();
            stored[load_key(instruction.local)] = true;
          }
          kept.push_back(std::move(instruction));
          continue;
        case ir::InstructionKind::StoreElement:
          ++version_[instruction.local];
          kept.push_back(std::move(instruction));
          continue;
        case ir::InstructionKind::Call:
          for (const auto& argument : instruction.call_arguments) {
            if (argument.kind == ir::CallArgumentKind::LocalRef) ++version_[argument.local];
          }
          kept.push_back(std::move(instruction));
          continue;
      }
      const auto found = table.find(key);
      if (found != table.end() && instruction.result.has_value()) {
        replaced_[*instruction.result] = found->second;
        ++result.removed;
        if (stored[key]) {
          ++result.forwarded_loads;
        }
        continue;
      }
      if (instruction.result.has_value()) table[key] = *instruction.result;
      kept.push_back(std::move(instruction));
    }
    block.instructions = std::move(kept);
  }

  ir::Function& function_;
  std::map<ir::ValueId, ir::ValueId> replaced_;
  std::map<ir::LocalId, std::size_t> version_;
};

}  // namespace

ValueNumberingResult number_values(const ir::Module& module) {
  ValueNumberingResult result;
  result.module = module;
  for (auto& function : result.module.functions) {
    const std::size_t before = result.removed;
    const std::size_t forwarded_before = result.forwarded_loads;
    FunctionNumbering(function).run(result);
    result.notes.push_back("value numbering: " + function.name + ": " + std::to_string(result.removed - before) +
                           " redundant instruction(s) removed (" +
                           std::to_string(result.forwarded_loads - forwarded_before) +
                           " load(s) replaced by the value just stored)");
  }
  return result;
}

}  // namespace nexus::compiler::passes
