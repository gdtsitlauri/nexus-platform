#include "nexus/compiler/passes/loop_unroll.hpp"

#include <algorithm>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>

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
    for (ir::ValueId operand : instruction.operands) {
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
      for (ir::ValueId operand : candidate.operands) {
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
    for (auto pred : cfg.predecessors[cond_block.id]) {
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
    // The first local found may be the bound (as in `n > i`): try the other operand of the comparison.
    for (const auto& instruction : cond_block.instructions) {
      if (instruction.result != info.condition_value || instruction.kind != ir::InstructionKind::Binary) {
        continue;
      }
      for (const ir::ValueId operand : instruction.operands) {
        for (const auto& candidate : cond_block.instructions) {
          if (candidate.kind != ir::InstructionKind::LoadLocal || candidate.result != operand ||
              candidate.local == info.induction_local) {
            continue;
          }
          const auto step = detect_step_constant(body_block, candidate.local);
          if (step.value_or(0) != 0) {
            info.induction_local = candidate.local;
            info.init_constant = detect_init_constant(function.blocks[preheader], candidate.local);
            info.limit_constant = detect_limit_constant(cond_block, info.condition_value, candidate.local);
            info.step = *step;
            return info;
          }
        }
      }
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

  auto remap_value = [&outcome](ir::ValueId value) {
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

void clear_to_jump(ir::BasicBlock& block, ir::BlockId target, std::string label) {
  block.label = std::move(label);
  block.instructions.clear();
  block.terminator = ir::Terminator{.kind = ir::TerminatorKind::Jump, .true_target = target};
}

bool apply_concrete_unroll(ir::Function& function, std::vector<std::string>& notes) {
  const auto loop = detect_supported_loop(function);
  if (!loop.has_value()) {
    notes.push_back("concrete unroll: no supported counted loop found");
    return false;
  }
  if (loop->compare_op != ir::BinaryOp::Less || !loop->init_constant.has_value() || !loop->limit_constant.has_value() ||
      loop->step != 1) {
    notes.push_back("concrete unroll: only i < constant with +1 step is supported");
    return false;
  }

  const auto trip_count = std::max<std::int64_t>(0, *loop->limit_constant - *loop->init_constant);
  if (trip_count > 8) {
    notes.push_back("concrete unroll: refusing to fully unroll loops with trip count > 8");
    return false;
  }

  const auto original_body = function.blocks[loop->body];
  auto& preheader = function.blocks[loop->preheader];
  auto& cond_block = function.blocks[loop->cond];
  auto& body_block = function.blocks[loop->body];

  if (trip_count == 0) {
    preheader.terminator = ir::Terminator{.kind = ir::TerminatorKind::Jump, .true_target = loop->exit};
    clear_to_jump(cond_block, loop->exit, "while.unrolled.empty");
    clear_to_jump(body_block, loop->exit, "while.unrolled.dead");
    notes.push_back("concrete unroll: removed zero-trip counted loop");
    return true;
  }

  std::vector<ir::BlockId> chain;
  chain.push_back(loop->cond);
  if (trip_count > 1) {
    chain.push_back(loop->body);
  }
  for (std::int64_t index = 2; index < trip_count; ++index) {
    const ir::BlockId new_id = function.blocks.size();
    function.blocks.push_back(ir::BasicBlock{.id = new_id, .label = "while.unrolled." + std::to_string(index)});
    chain.push_back(new_id);
  }

  preheader.terminator = ir::Terminator{.kind = ir::TerminatorKind::Jump, .true_target = chain.front()};
  for (std::int64_t index = 0; index < trip_count; ++index) {
    auto& target_block = function.blocks[chain[static_cast<std::size_t>(index)]];
    target_block.label = "while.unrolled." + std::to_string(index);
    clone_instructions_into(function, function, original_body, target_block);
    target_block.terminator = ir::Terminator{
        .kind = ir::TerminatorKind::Jump,
        .true_target = (index + 1 < trip_count)
            ? chain[static_cast<std::size_t>(index + 1)]
            : loop->exit,
    };
  }

  if (trip_count == 1) {
    clear_to_jump(body_block, loop->exit, "while.unrolled.dead");
  }

  notes.push_back("concrete unroll: fully unrolled loop with trip count " + std::to_string(trip_count));
  return true;
}

bool apply_symbolic_unroll(ir::Function& function, std::vector<std::string>& notes) {
  const auto loop = detect_supported_loop(function);
  if (!loop.has_value()) {
    notes.push_back("symbolic unroll: no supported counted loop found");
    return false;
  }
  if (loop->compare_op != ir::BinaryOp::Less || loop->step != 1) {
    notes.push_back("symbolic unroll: only affine i < limit with +1 step is supported");
    return false;
  }

  const auto original_cond = function.blocks[loop->cond];
  const auto original_body = function.blocks[loop->body];
  const ir::BlockId guard_id = function.blocks.size();
  function.blocks.push_back(ir::BasicBlock{.id = guard_id, .label = "while.unroll.check"});
  const ir::BlockId second_id = function.blocks.size();
  function.blocks.push_back(ir::BasicBlock{.id = second_id, .label = "while.unroll.1"});

  auto& first_body = function.blocks[loop->body];
  first_body.label = "while.unroll.0";
  clone_instructions_into(function, function, original_body, first_body);
  first_body.terminator = ir::Terminator{.kind = ir::TerminatorKind::Jump, .true_target = guard_id};

  auto& guard = function.blocks[guard_id];
  guard.label = "while.unroll.check";
  const auto guard_clone = clone_instructions_into(function, function, original_cond, guard);
  guard.terminator = ir::Terminator{
      .kind = ir::TerminatorKind::Branch,
      .condition = guard_clone.value_map.at(loop->condition_value),
      .true_target = second_id,
      .false_target = loop->exit,
  };

  auto& second_body = function.blocks[second_id];
  second_body.label = "while.unroll.1";
  clone_instructions_into(function, function, original_body, second_body);
  second_body.terminator = ir::Terminator{.kind = ir::TerminatorKind::Jump, .true_target = loop->cond};

  notes.push_back("symbolic unroll: applied factor-2 unrolling with residual guard");
  return true;
}

ir::BinaryOp mirrored(ir::BinaryOp op) {
  switch (op) {
    case ir::BinaryOp::Less: return ir::BinaryOp::Greater;
    case ir::BinaryOp::LessEqual: return ir::BinaryOp::GreaterEqual;
    case ir::BinaryOp::Greater: return ir::BinaryOp::Less;
    case ir::BinaryOp::GreaterEqual: return ir::BinaryOp::LessEqual;
    default: return op;
  }
}

bool apply_remainder_unroll(ir::Function& function, std::size_t factor, std::vector<std::string>& notes) {
  const auto loop = detect_supported_loop(function);
  if (!loop.has_value()) {
    notes.push_back("remainder unroll: no supported counted loop found");
    return false;
  }
  if (factor < 2) {
    notes.push_back("remainder unroll: the factor must be at least 2");
    return false;
  }
  const auto& cond_block = function.blocks[loop->cond];
  const auto& body_block = function.blocks[loop->body];
  // The comparison and the side of the induction variable.
  const ir::Instruction* compare = nullptr;
  for (const auto& instruction : cond_block.instructions) {
    if (instruction.result == loop->condition_value && instruction.kind == ir::InstructionKind::Binary) {
      compare = &instruction;
    }
  }
  std::optional<ir::ValueId> iv_load;
  for (const auto& instruction : cond_block.instructions) {
    if (instruction.kind == ir::InstructionKind::LoadLocal && instruction.local == loop->induction_local) {
      iv_load = instruction.result;
    }
  }
  if (compare == nullptr || !iv_load || compare->operands.size() != 2) {
    notes.push_back("remainder unroll: unsupported loop condition");
    return false;
  }
  const bool iv_left = compare->operands[0] == *iv_load;
  if (!iv_left && compare->operands[1] != *iv_load) {
    notes.push_back("remainder unroll: the condition does not compare the induction variable directly");
    return false;
  }
  const ir::BinaryOp relation = iv_left ? compare->binary_op : mirrored(compare->binary_op);
  const bool upward = (relation == ir::BinaryOp::Less || relation == ir::BinaryOp::LessEqual) && loop->step > 0;
  const bool downward = (relation == ir::BinaryOp::Greater || relation == ir::BinaryOp::GreaterEqual) && loop->step < 0;
  if (!upward && !downward) {
    notes.push_back("remainder unroll: the induction variable must move towards the bound (i < n with i += s, "
                    "or i > n with i -= s)");
    return false;
  }
  // The bound must be loop-invariant: no local read by the condition (other than i) is written in the body,
  // and the body writes i exactly once.
  std::size_t iv_stores = 0;
  for (const auto& instruction : body_block.instructions) {
    if (instruction.kind != ir::InstructionKind::StoreLocal) continue;
    if (instruction.local == loop->induction_local) {
      ++iv_stores;
      continue;
    }
    for (const auto& read : cond_block.instructions) {
      if (read.kind == ir::InstructionKind::LoadLocal && read.local == instruction.local) {
        notes.push_back("remainder unroll: the loop bound changes inside the body");
        return false;
      }
    }
  }
  if (iv_stores != 1) {
    notes.push_back("remainder unroll: the body must update the induction variable exactly once");
    return false;
  }

  const auto original_cond = function.blocks[loop->cond];
  const auto original_body = function.blocks[loop->body];
  const auto iv = *iv_load;
  const auto condition_value = loop->condition_value;
  const auto cond_id = loop->cond;
  const auto preheader_id = loop->preheader;
  const std::int64_t step = loop->step;
  const ir::BlockId main_cond_id = function.blocks.size();
  function.blocks.push_back(ir::BasicBlock{.id = main_cond_id, .label = "while.main.check"});
  const ir::BlockId main_body_id = function.blocks.size();
  function.blocks.push_back(ir::BasicBlock{.id = main_body_id, .label = "while.main.body"});

  // Main condition: the original test applied to i + (K-1)*step.
  ir::BasicBlock scratch;
  const auto cond_clone = clone_instructions_into(function, function, original_cond, scratch);
  const ir::ValueId cloned_iv = cond_clone.value_map.at(iv);
  const ir::ValueId cloned_compare = cond_clone.value_map.at(condition_value);
  const ir::ValueId offset_value = function.values.size();
  function.values.push_back(ir::ValueInfo{.id = offset_value, .type = ir::Type{}});
  const ir::ValueId shifted_value = function.values.size();
  function.values.push_back(ir::ValueInfo{.id = shifted_value, .type = ir::Type{}});
  std::vector<ir::Instruction> main_cond;
  for (auto instruction : scratch.instructions) {
    if (instruction.result == cloned_compare) {
      for (auto& operand : instruction.operands) {
        if (operand == cloned_iv) operand = shifted_value;
      }
    }
    const bool is_iv = instruction.result == cloned_iv;
    main_cond.push_back(std::move(instruction));
    if (is_iv) {
      main_cond.push_back(ir::Instruction{.kind = ir::InstructionKind::ConstInt, .result = offset_value,
                                          .int_immediate = static_cast<std::int64_t>(factor - 1) * step});
      main_cond.push_back(ir::Instruction{.kind = ir::InstructionKind::Binary, .result = shifted_value,
                                          .binary_op = ir::BinaryOp::Add, .operands = {cloned_iv, offset_value}});
    }
  }
  function.blocks[main_cond_id].instructions = std::move(main_cond);
  function.blocks[main_cond_id].terminator = ir::Terminator{
      .kind = ir::TerminatorKind::Branch, .condition = cloned_compare, .true_target = main_body_id,
      .false_target = cond_id};

  // Main body: K guard-free copies of the body.
  std::vector<ir::Instruction> copies;
  for (std::size_t copy = 0; copy < factor; ++copy) {
    ir::BasicBlock part;
    clone_instructions_into(function, function, original_body, part);
    copies.insert(copies.end(), part.instructions.begin(), part.instructions.end());
  }
  function.blocks[main_body_id].instructions = std::move(copies);
  function.blocks[main_body_id].terminator =
      ir::Terminator{.kind = ir::TerminatorKind::Jump, .true_target = main_cond_id};

  // The preheader enters the main loop; the original loop runs the remaining iterations.
  auto& preheader = function.blocks[preheader_id];
  if (preheader.terminator->true_target == cond_id) preheader.terminator->true_target = main_cond_id;
  if (preheader.terminator->false_target == cond_id) preheader.terminator->false_target = main_cond_id;

  const std::int64_t reach = static_cast<std::int64_t>(factor - 1) * (step > 0 ? step : -step);
  notes.push_back("remainder unroll: factor " + std::to_string(factor) + "; the main loop runs " +
                  std::to_string(factor) + " copies of the body while i " + (step > 0 ? "+ " : "- ") +
                  std::to_string(reach) + " still passes the test, the original loop runs the remaining iterations");
  return true;
}

}  // namespace

LoopUnrollResult unroll_loops(const ir::Module& module, LoopUnrollMode mode, std::size_t factor) {
  LoopUnrollResult result{.module = module};
  for (auto& function : result.module.functions) {
    bool changed = false;
    switch (mode) {
      case LoopUnrollMode::Concrete: changed = apply_concrete_unroll(function, result.notes); break;
      case LoopUnrollMode::Symbolic: changed = apply_symbolic_unroll(function, result.notes); break;
      case LoopUnrollMode::SymbolicRemainder: changed = apply_remainder_unroll(function, factor, result.notes); break;
    }
    if (changed) {
      ++result.transformed_loops;
    }
  }
  return result;
}

}  // namespace nexus::compiler::passes
