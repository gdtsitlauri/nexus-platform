#include "nexus/compiler/analysis/alias_analysis.hpp"

#include <sstream>
#include <unordered_map>

namespace nexus::compiler::analysis {

namespace {

std::unordered_map<ir::ValueId, std::int64_t> collect_constant_ints(const ir::Function& function) {
  std::unordered_map<ir::ValueId, std::int64_t> constants;
  for (const auto& block : function.blocks) {
    for (const auto& instruction : block.instructions) {
      if (instruction.kind == ir::InstructionKind::ConstInt && instruction.result.has_value()) {
        constants.insert_or_assign(*instruction.result, instruction.int_immediate);
      }
    }
  }
  return constants;
}

std::string instruction_label(const ir::Function& function, const ir::Instruction& instruction) {
  std::ostringstream output;
  switch (instruction.kind) {
    case ir::InstructionKind::LoadLocal:
      output << "load " << ir::local_info(function, instruction.local).name;
      break;
    case ir::InstructionKind::StoreLocal:
      output << "store " << ir::local_info(function, instruction.local).name;
      break;
    case ir::InstructionKind::LoadElement:
      output << "load_element " << ir::local_info(function, instruction.local).name;
      break;
    case ir::InstructionKind::StoreElement:
      output << "store_element " << ir::local_info(function, instruction.local).name;
      break;
    case ir::InstructionKind::Call:
      output << "call " << instruction.callee;
      break;
    default:
      output << "memory-op";
      break;
  }
  return output.str();
}

std::vector<std::optional<std::int64_t>> extract_indices(
    const ir::Instruction& instruction,
    const std::unordered_map<ir::ValueId, std::int64_t>& constants) {
  std::vector<std::optional<std::int64_t>> indices;
  std::size_t count = instruction.operands.size();
  if (instruction.kind == ir::InstructionKind::StoreElement && count > 0) {
    --count;
  }
  for (std::size_t index = 0; index < count; ++index) {
    const auto found = constants.find(instruction.operands[index]);
    indices.push_back(found == constants.end() ? std::optional<std::int64_t>{} : found->second);
  }
  return indices;
}

}  // namespace

std::string_view alias_kind_name(AliasKind kind) {
  switch (kind) {
    case AliasKind::NoAlias:
      return "no-alias";
    case AliasKind::MayAlias:
      return "may-alias";
    case AliasKind::MustAlias:
      return "must-alias";
  }
  return "may-alias";
}

AliasAnalysisResult analyze_aliases(const ir::Function& function) {
  AliasAnalysisResult result;
  const auto constants = collect_constant_ints(function);

  for (const auto& block : function.blocks) {
    for (std::size_t instruction_index = 0; instruction_index < block.instructions.size(); ++instruction_index) {
      const auto& instruction = block.instructions[instruction_index];
      switch (instruction.kind) {
        case ir::InstructionKind::LoadLocal:
        case ir::InstructionKind::StoreLocal:
        case ir::InstructionKind::LoadElement:
        case ir::InstructionKind::StoreElement:
          result.references.push_back(
              MemoryReference{
                  .id = result.references.size(),
                  .block = block.id,
                  .instruction_index = instruction_index,
                  .base_local = instruction.local,
                  .is_element = instruction.kind == ir::InstructionKind::LoadElement ||
                      instruction.kind == ir::InstructionKind::StoreElement,
                  .from_call = false,
                  .constant_indices = extract_indices(instruction, constants),
                  .label = instruction_label(function, instruction),
              });
          break;
        case ir::InstructionKind::Call:
          for (const auto& argument : instruction.call_arguments) {
            if (argument.kind != ir::CallArgumentKind::LocalRef) {
              continue;
            }
            std::ostringstream label;
            label << "call " << instruction.callee << '(' << ir::local_info(function, argument.local).name << ')';
            result.references.push_back(
                MemoryReference{
                    .id = result.references.size(),
                    .block = block.id,
                    .instruction_index = instruction_index,
                    .base_local = argument.local,
                    .is_element = !argument.indices.empty(),
                    .from_call = true,
                    .constant_indices = {},
                    .label = label.str(),
                });
          }
          break;
        case ir::InstructionKind::ConstInt:
        case ir::InstructionKind::ConstBool:
        case ir::InstructionKind::Unary:
        case ir::InstructionKind::Binary:
          break;
      }
    }
  }

  result.matrix.assign(result.references.size(), std::vector<AliasKind>(result.references.size(), AliasKind::NoAlias));
  for (std::size_t lhs = 0; lhs < result.references.size(); ++lhs) {
    result.matrix[lhs][lhs] = AliasKind::MustAlias;
    for (std::size_t rhs = lhs + 1; rhs < result.references.size(); ++rhs) {
      const auto& left = result.references[lhs];
      const auto& right = result.references[rhs];

      AliasKind relation = AliasKind::NoAlias;
      if (left.base_local == right.base_local) {
        if (!left.is_element && !right.is_element) {
          relation = AliasKind::MustAlias;
        } else if (left.constant_indices.size() == right.constant_indices.size() &&
                   !left.constant_indices.empty()) {
          bool all_known = true;
          bool all_equal = true;
          bool any_different = false;
          for (std::size_t index = 0; index < left.constant_indices.size(); ++index) {
            if (!left.constant_indices[index].has_value() || !right.constant_indices[index].has_value()) {
              all_known = false;
              break;
            }
            if (left.constant_indices[index] != right.constant_indices[index]) {
              all_equal = false;
              any_different = true;
            }
          }
          if (all_known && all_equal) {
            relation = AliasKind::MustAlias;
          } else if (all_known && any_different) {
            relation = AliasKind::NoAlias;
          } else {
            relation = AliasKind::MayAlias;
          }
        } else {
          relation = AliasKind::MayAlias;
        }
      }
      result.matrix[lhs][rhs] = relation;
      result.matrix[rhs][lhs] = relation;
    }
  }

  return result;
}

std::string print_aliases(const ir::Function& function, const AliasAnalysisResult& result) {
  std::ostringstream output;
  output << "func " << function.name << " alias:\n";
  for (const auto& reference : result.references) {
    output << "  ref" << reference.id << ": bb" << reference.block << '.'
           << function.blocks[reference.block].label << " #" << reference.instruction_index << " "
           << reference.label << '\n';
  }
  output << "  pairs:\n";
  for (std::size_t lhs = 0; lhs < result.references.size(); ++lhs) {
    for (std::size_t rhs = lhs + 1; rhs < result.references.size(); ++rhs) {
      output << "    ref" << lhs << " <-> ref" << rhs << ": "
             << alias_kind_name(result.matrix[lhs][rhs]) << '\n';
    }
  }
  return output.str();
}

}  // namespace nexus::compiler::analysis
