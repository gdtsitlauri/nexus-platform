#include "nexus/compiler/passes/affine_stripmine.hpp"

#include <optional>
#include <unordered_map>

#include "nexus/compiler/analysis/cfg.hpp"

namespace nexus::compiler::passes {

namespace {

struct CountedLoopInfo {
  ir::BlockId preheader = ir::kInvalidId;
  ir::BlockId cond = ir::kInvalidId;
  ir::BlockId body = ir::kInvalidId;
  ir::BlockId exit = ir::kInvalidId;
  ir::LocalId induction_local = ir::kInvalidId;
  ir::BinaryOp compare_op = ir::BinaryOp::Less;
  ir::ValueId condition_value = ir::kInvalidId;
  std::optional<std::int64_t> init_constant;
  std::optional<std::int64_t> limit_constant;
  std::int64_t step = 0;
};

std::optional<std::int64_t> const_int_in_block(const ir::BasicBlock& block, ir::ValueId value) {
  for (const auto& instruction : block.instructions) {
    if (instruction.result == value && instruction.kind == ir::InstructionKind::ConstInt) {
      return instruction.int_immediate;
    }
  }
  return std::nullopt;
}

std::optional<std::pair<ir::LocalId, ir::BinaryOp>> loop_condition(
    const ir::BasicBlock& cond_block,
    ir::ValueId condition_value) {
  for (const auto& instruction : cond_block.instructions) {
    if (instruction.result != condition_value || instruction.kind != ir::InstructionKind::Binary) {
      continue;
    }
    for (const auto& candidate : cond_block.instructions) {
      if (candidate.kind == ir::InstructionKind::LoadLocal && candidate.result == instruction.operands[0]) {
        return std::pair{candidate.local, instruction.binary_op};
      }
      if (candidate.kind == ir::InstructionKind::LoadLocal && candidate.result == instruction.operands[1]) {
        return std::pair{candidate.local, instruction.binary_op};
      }
    }
  }
  return std::nullopt;
}

std::optional<std::int64_t> detect_limit_constant(
    const ir::BasicBlock& cond_block,
    ir::ValueId condition_value,
    ir::LocalId induction_local) {
  for (const auto& instruction : cond_block.instructions) {
    if (instruction.result != condition_value || instruction.kind != ir::InstructionKind::Binary) {
      continue;
    }
    for (const auto operand : instruction.operands) {
      bool is_iv = false;
      for (const auto& candidate : cond_block.instructions) {
        if (candidate.kind == ir::InstructionKind::LoadLocal && candidate.result == operand &&
            candidate.local == induction_local) {
          is_iv = true;
          break;
        }
      }
      if (!is_iv) {
        if (const auto limit = const_int_in_block(cond_block, operand); limit.has_value()) {
          return limit;
        }
      }
    }
  }
  return std::nullopt;
}

std::optional<std::int64_t> detect_init_constant(const ir::BasicBlock& preheader, ir::LocalId induction_local) {
  for (auto it = preheader.instructions.rbegin(); it != preheader.instructions.rend(); ++it) {
    if (it->kind == ir::InstructionKind::StoreLocal && it->local == induction_local) {
      return const_int_in_block(preheader, it->operands.front());
    }
  }
  return std::nullopt;
}

std::optional<std::int64_t> detect_step_constant(const ir::BasicBlock& body, ir::LocalId induction_local) {
  for (auto it = body.instructions.rbegin(); it != body.instructions.rend(); ++it) {
    if (it->kind != ir::InstructionKind::StoreLocal || it->local != induction_local) {
      continue;
    }
    const ir::ValueId step_value = it->operands.front();
    for (const auto& candidate : body.instructions) {
      if (candidate.result != step_value || candidate.kind != ir::InstructionKind::Binary) {
        continue;
      }
      if (candidate.binary_op != ir::BinaryOp::Add && candidate.binary_op != ir::BinaryOp::Sub) {
        continue;
      }
      for (const auto operand : candidate.operands) {
        if (const auto constant = const_int_in_block(body, operand); constant.has_value()) {
          return candidate.binary_op == ir::BinaryOp::Add ? *constant : -*constant;
        }
      }
    }
  }
  return std::nullopt;
}

std::optional<CountedLoopInfo> detect_supported_loop(const ir::Function& function) {
  const auto cfg = nexus::compiler::analysis::build_cfg(function);
  for (const auto& cond_block : function.blocks) {
    if (cond_block.label.find("while.cond") == std::string::npos || !cond_block.terminator.has_value() ||
        cond_block.terminator->kind != ir::TerminatorKind::Branch) {
      continue;
    }

    const ir::BlockId body = cond_block.terminator->true_target;
    const ir::BlockId exit = cond_block.terminator->false_target;
    if (body >= function.blocks.size() || exit >= function.blocks.size()) {
      continue;
    }
    const auto& body_block = function.blocks[body];
    if (body_block.label.find("while.body") == std::string::npos || !body_block.terminator.has_value() ||
        body_block.terminator->kind != ir::TerminatorKind::Jump ||
        body_block.terminator->true_target != cond_block.id) {
      continue;
    }

    ir::BlockId preheader = ir::kInvalidId;
    for (const auto pred : cfg.predecessors[cond_block.id]) {
      if (pred != body) {
        preheader = pred;
      }
    }
    if (preheader == ir::kInvalidId) {
      continue;
    }

    const auto condition = loop_condition(cond_block, *cond_block.terminator->condition);
    if (!condition.has_value()) {
      continue;
    }

    CountedLoopInfo info{
        .preheader = preheader,
        .cond = cond_block.id,
        .body = body,
        .exit = exit,
        .induction_local = condition->first,
        .compare_op = condition->second,
        .condition_value = *cond_block.terminator->condition,
        .init_constant = detect_init_constant(function.blocks[preheader], condition->first),
        .limit_constant = detect_limit_constant(cond_block, *cond_block.terminator->condition, condition->first),
        .step = detect_step_constant(body_block, condition->first).value_or(0),
    };

    if (info.step != 0) {
      return info;
    }
  }
  return std::nullopt;
}

struct CloneOutcome {
  std::unordered_map<ir::ValueId, ir::ValueId> value_map;
};

CloneOutcome clone_instructions_into(
    const ir::Function& original,
    ir::Function& target,
    const ir::BasicBlock& source_block,
    ir::BasicBlock& target_block) {
  CloneOutcome outcome;
  target_block.instructions.clear();

  auto remap_value = [&outcome](const ir::ValueId value) {
    const auto found = outcome.value_map.find(value);
    return found == outcome.value_map.end() ? value : found->second;
  };

  for (const auto& instruction : source_block.instructions) {
    ir::Instruction cloned = instruction;
    if (instruction.result.has_value()) {
      const ir::ValueId new_value = target.values.size();
      target.values.push_back(
          ir::ValueInfo{.id = new_value, .type = ir::value_info(original, *instruction.result).type});
      outcome.value_map.insert_or_assign(*instruction.result, new_value);
      cloned.result = new_value;
    }

    for (auto& operand : cloned.operands) {
      operand = remap_value(operand);
    }
    for (auto& argument : cloned.call_arguments) {
      if (argument.kind == ir::CallArgumentKind::Value) {
        argument.value = remap_value(argument.value);
      }
      for (auto& index : argument.indices) {
        index = remap_value(index);
      }
    }
    target_block.instructions.push_back(std::move(cloned));
  }

  return outcome;
}

bool apply_strip_mine(ir::Function& function, std::vector<std::string>& notes, std::size_t tile_factor) {
  const auto loop = detect_supported_loop(function);
  if (!loop.has_value()) {
    notes.push_back("affine strip-mine: no supported affine counted loop found");
    return false;
  }
  if (tile_factor != 2U) {
    notes.push_back("affine strip-mine: only tile factor 2 is implemented");
    return false;
  }
  if (loop->compare_op != ir::BinaryOp::Less || loop->step != 1) {
    notes.push_back("affine strip-mine: only affine i < limit with +1 step is supported");
    return false;
  }

  const auto original_cond = function.blocks[loop->cond];
  const auto original_body = function.blocks[loop->body];
  const ir::BlockId guard_id = function.blocks.size();
  function.blocks.push_back(ir::BasicBlock{.id = guard_id, .label = "while.strip.guard"});
  const ir::BlockId second_id = function.blocks.size();
  function.blocks.push_back(ir::BasicBlock{.id = second_id, .label = "while.strip.tile.1"});

  auto& first_tile = function.blocks[loop->body];
  first_tile.label = "while.strip.tile.0";
  clone_instructions_into(function, function, original_body, first_tile);
  first_tile.terminator = ir::Terminator{.kind = ir::TerminatorKind::Jump, .true_target = guard_id};

  auto& guard = function.blocks[guard_id];
  guard.label = "while.strip.guard";
  const auto guard_clone = clone_instructions_into(function, function, original_cond, guard);
  guard.terminator = ir::Terminator{
      .kind = ir::TerminatorKind::Branch,
      .condition = guard_clone.value_map.at(loop->condition_value),
      .true_target = second_id,
      .false_target = loop->exit,
  };

  auto& second_tile = function.blocks[second_id];
  second_tile.label = "while.strip.tile.1";
  clone_instructions_into(function, function, original_body, second_tile);
  second_tile.terminator = ir::Terminator{.kind = ir::TerminatorKind::Jump, .true_target = loop->cond};

  notes.push_back("affine strip-mine: applied tile factor 2 with residual guard on a canonical affine loop");
  return true;
}

}  // namespace

StripMineResult strip_mine_loops(const ir::Module& module, std::size_t tile_factor) {
  StripMineResult result{.module = module};
  for (auto& function : result.module.functions) {
    if (apply_strip_mine(function, result.notes, tile_factor)) {
      result.transformed_loops += 1;
    }
  }
  return result;
}

}  // namespace nexus::compiler::passes
