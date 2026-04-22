#include "nexus/compiler/analysis/affine_analysis.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>

#include "nexus/compiler/analysis/cfg.hpp"

namespace nexus::compiler::analysis {

namespace {

struct DetectedLoop {
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

std::optional<DetectedLoop> detect_supported_loop(const ir::Function& function) {
  const auto cfg = build_cfg(function);
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

    const auto step = detect_step_constant(body_block, condition->first).value_or(0);
    if (step == 0) {
      continue;
    }

    return DetectedLoop{
        .preheader = preheader,
        .cond = cond_block.id,
        .body = body,
        .exit = exit,
        .induction_local = condition->first,
        .compare_op = condition->second,
        .condition_value = *cond_block.terminator->condition,
        .init_constant = detect_init_constant(function.blocks[preheader], condition->first),
        .limit_constant = detect_limit_constant(cond_block, *cond_block.terminator->condition, condition->first),
        .step = step,
    };
  }
  return std::nullopt;
}

std::vector<std::string> collect_memory_locals(const ir::Function& function, ir::BlockId body) {
  std::set<std::string> names;
  if (body >= function.blocks.size()) {
    return {};
  }

  for (const auto& instruction : function.blocks[body].instructions) {
    if (instruction.kind == ir::InstructionKind::LoadElement ||
        instruction.kind == ir::InstructionKind::StoreElement) {
      names.insert(ir::local_info(function, instruction.local).name);
    }
  }

  return {names.begin(), names.end()};
}

}  // namespace

AffineLoopSummary analyze_affine_loop(const ir::Function& function) {
  AffineLoopSummary summary;
  const auto loop = detect_supported_loop(function);
  if (!loop.has_value()) {
    return summary;
  }

  summary.supported = loop->compare_op == ir::BinaryOp::Less || loop->compare_op == ir::BinaryOp::LessEqual;
  summary.preheader = loop->preheader;
  summary.cond = loop->cond;
  summary.body = loop->body;
  summary.exit = loop->exit;
  summary.induction_local = ir::local_info(function, loop->induction_local).name;
  summary.init_constant = loop->init_constant;
  summary.limit_constant = loop->limit_constant;
  summary.step = loop->step;
  if (loop->init_constant.has_value() && loop->limit_constant.has_value() && loop->step != 0 &&
      loop->compare_op == ir::BinaryOp::Less) {
    summary.trip_count = std::max<std::int64_t>(0, *loop->limit_constant - *loop->init_constant);
  }
  summary.memory_locals = collect_memory_locals(function, loop->body);
  return summary;
}

std::string print_affine_loop(const ir::Function& function, const AffineLoopSummary& summary) {
  std::ostringstream output;
  output << "func " << function.name << " affine:\n";
  if (!summary.supported) {
    output << "  no supported affine counted loop found\n";
    return output.str();
  }

  output << "  loop: preheader=bb" << summary.preheader << "." << function.blocks[summary.preheader].label
         << " cond=bb" << summary.cond << "." << function.blocks[summary.cond].label
         << " body=bb" << summary.body << "." << function.blocks[summary.body].label
         << " exit=bb" << summary.exit << "." << function.blocks[summary.exit].label << '\n';
  output << "  induction: " << summary.induction_local
         << " init=" << (summary.init_constant.has_value() ? std::to_string(*summary.init_constant) : "?")
         << " limit=" << (summary.limit_constant.has_value() ? std::to_string(*summary.limit_constant) : "?")
         << " step=" << summary.step;
  if (summary.trip_count.has_value()) {
    output << " trip-count=" << *summary.trip_count;
  }
  output << '\n';
  if (summary.memory_locals.empty()) {
    output << "  memory-locals: -\n";
  } else {
    output << "  memory-locals:";
    for (const auto& local : summary.memory_locals) {
      output << ' ' << local;
    }
    output << '\n';
  }
  output << "  locality-note: strip-mining candidate with tile factor 2\n";
  return output.str();
}

}  // namespace nexus::compiler::analysis
