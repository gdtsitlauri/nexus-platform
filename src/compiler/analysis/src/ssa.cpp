#include "nexus/compiler/analysis/ssa.hpp"

#include <algorithm>
#include <functional>
#include <sstream>

namespace nexus::compiler::analysis {

namespace {

using ir::BlockId;
using ir::Function;
using ir::Instruction;
using ir::InstructionKind;
using ir::LocalId;

std::size_t block_index(const Function& function, BlockId id) {
  for (std::size_t index = 0; index < function.blocks.size(); ++index) {
    if (function.blocks[index].id == id) {
      return index;
    }
  }
  return 0;
}

std::string local_name(const Function& function, LocalId local, std::size_t version) {
  return function.locals[local].name + "." + std::to_string(version);
}

std::string block_name(const Function& function, BlockId id) {
  const auto& block = function.blocks[block_index(function, id)];
  return "bb" + std::to_string(id) + (block.label.empty() ? "" : "." + block.label);
}

LatticeValue meet(const LatticeValue& lhs, const LatticeValue& rhs) {
  using Kind = LatticeValue::Kind;
  if (lhs.kind == Kind::Top) {
    return rhs;
  }
  if (rhs.kind == Kind::Top) {
    return lhs;
  }
  if (lhs.kind == Kind::Bottom || rhs.kind == Kind::Bottom || lhs.value != rhs.value) {
    return {Kind::Bottom, 0};
  }
  return lhs;
}

LatticeValue constant(std::int64_t value) {
  return {LatticeValue::Kind::Constant, value};
}

const LatticeValue kBottom{LatticeValue::Kind::Bottom, 0};

std::string lattice_text(const LatticeValue& value) {
  switch (value.kind) {
    case LatticeValue::Kind::Top:
      return "TOP";
    case LatticeValue::Kind::Constant:
      return std::to_string(value.value);
    case LatticeValue::Kind::Bottom:
      return "BOTTOM";
  }
  return "?";
}

std::int32_t wrap(std::int64_t value) {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(value)));
}

}  // namespace

SsaForm build_ssa(const Function& function, const ControlFlowGraph& cfg, const DominatorResult& dominators) {
  const std::size_t block_count = cfg.successors.size();
  SsaForm ssa;
  ssa.dominance_frontier.assign(block_count, {});
  ssa.dominator_children.assign(block_count, {});
  ssa.phis.assign(block_count, {});
  ssa.versions.assign(block_count, {});
  ssa.tracked_local.assign(function.locals.size(), false);
  ssa.version_count.assign(function.locals.size(), 1);
  for (const auto& local : function.locals) {
    ssa.tracked_local[local.id] = local.type.is_scalar();
  }

  // Dominator tree and dominance frontiers (Cooper, Harvey & Kennedy).
  auto idom = [&](BlockId block) { return dominators.immediate_dominators[block]; };
  auto reachable = [&](BlockId block) { return block == cfg.entry_block || idom(block).has_value(); };
  for (BlockId block = 0; block < block_count; ++block) {
    if (block != cfg.entry_block && idom(block).has_value()) {
      ssa.dominator_children[*idom(block)].push_back(block);
    }
  }
  for (BlockId block = 0; block < block_count; ++block) {
    if (!reachable(block) || cfg.predecessors[block].size() < 2) {
      continue;
    }
    for (const BlockId predecessor : cfg.predecessors[block]) {
      if (!reachable(predecessor)) {
        continue;
      }
      BlockId runner = predecessor;
      while (runner != idom(block).value_or(cfg.entry_block)) {
        auto& frontier = ssa.dominance_frontier[runner];
        if (std::find(frontier.begin(), frontier.end(), block) == frontier.end()) {
          frontier.push_back(block);
        }
        if (!idom(runner).has_value()) {
          break;
        }
        runner = *idom(runner);
      }
    }
  }

  // Phi placement at the iterated dominance frontier of each local's definition sites.
  for (const auto& local : function.locals) {
    if (!ssa.tracked_local[local.id]) {
      continue;
    }
    std::vector<BlockId> work;
    std::set<BlockId> defining;
    for (const auto& block : function.blocks) {
      for (const auto& instruction : block.instructions) {
        if (instruction.kind == InstructionKind::StoreLocal && instruction.local == local.id) {
          defining.insert(block.id);
        }
      }
    }
    defining.insert(cfg.entry_block);  // version 0 is live on entry
    work.assign(defining.begin(), defining.end());
    std::set<BlockId> has_phi;
    while (!work.empty()) {
      const BlockId block = work.back();
      work.pop_back();
      for (const BlockId frontier : ssa.dominance_frontier[block]) {
        if (has_phi.insert(frontier).second) {
          ssa.phis[frontier].push_back(PhiNode{local.id, 0, {}});
          ++ssa.phi_count;
          if (!defining.contains(frontier)) {
            work.push_back(frontier);
          }
        }
      }
    }
  }

  // Renaming by a dominator-tree walk.
  std::vector<std::vector<std::size_t>> stacks(function.locals.size(), std::vector<std::size_t>{0});
  std::function<void(BlockId)> rename = [&](BlockId block) {
    std::vector<LocalId> pushed;
    for (auto& phi : ssa.phis[block]) {
      phi.version = ssa.version_count[phi.local]++;
      stacks[phi.local].push_back(phi.version);
      pushed.push_back(phi.local);
    }
    const auto& instructions = function.blocks[block_index(function, block)].instructions;
    ssa.versions[block].assign(instructions.size(), 0);
    for (std::size_t index = 0; index < instructions.size(); ++index) {
      const Instruction& instruction = instructions[index];
      if (instruction.kind == InstructionKind::LoadLocal && ssa.tracked_local[instruction.local]) {
        ssa.versions[block][index] = stacks[instruction.local].back();
      } else if (instruction.kind == InstructionKind::StoreLocal && ssa.tracked_local[instruction.local]) {
        const std::size_t version = ssa.version_count[instruction.local]++;
        ssa.versions[block][index] = version;
        stacks[instruction.local].push_back(version);
        pushed.push_back(instruction.local);
      }
    }
    for (const BlockId successor : cfg.successors[block]) {
      for (auto& phi : ssa.phis[successor]) {
        phi.arguments.push_back({block, stacks[phi.local].back()});
      }
    }
    for (const BlockId child : ssa.dominator_children[block]) {
      rename(child);
    }
    for (const LocalId local : pushed) {
      stacks[local].pop_back();
    }
  };
  rename(cfg.entry_block);
  return ssa;
}

std::string print_ssa(const Function& function, const SsaForm& ssa) {
  std::ostringstream out;
  out << "func " << function.name << " ssa: " << ssa.phi_count << " phi node(s)\n";
  for (const auto& block : function.blocks) {
    out << "  " << block_name(function, block.id) << ":";
    if (!ssa.dominance_frontier[block.id].empty()) {
      out << "    ; DF = {";
      for (std::size_t index = 0; index < ssa.dominance_frontier[block.id].size(); ++index) {
        out << (index ? ", " : "") << block_name(function, ssa.dominance_frontier[block.id][index]);
      }
      out << '}';
    }
    out << '\n';
    for (const auto& phi : ssa.phis[block.id]) {
      out << "    " << local_name(function, phi.local, phi.version) << " = phi";
      for (const auto& [predecessor, version] : phi.arguments) {
        out << " [" << block_name(function, predecessor) << ": " << local_name(function, phi.local, version) << ']';
      }
      out << '\n';
    }
    for (std::size_t index = 0; index < block.instructions.size(); ++index) {
      const Instruction& instruction = block.instructions[index];
      const bool tracked = instruction.local < ssa.tracked_local.size() && ssa.tracked_local[instruction.local];
      out << "    ";
      switch (instruction.kind) {
        case InstructionKind::ConstInt:
          out << ir::value_name(*instruction.result) << " = " << instruction.int_immediate;
          break;
        case InstructionKind::ConstBool:
          out << ir::value_name(*instruction.result) << " = " << (instruction.bool_immediate ? "true" : "false");
          break;
        case InstructionKind::LoadLocal:
          out << ir::value_name(*instruction.result) << " = "
              << (tracked ? local_name(function, instruction.local, ssa.versions[block.id][index])
                          : function.locals[instruction.local].name);
          break;
        case InstructionKind::StoreLocal:
          out << (tracked ? local_name(function, instruction.local, ssa.versions[block.id][index])
                          : function.locals[instruction.local].name)
              << " = " << ir::value_name(instruction.operands.front());
          break;
        case InstructionKind::LoadElement:
          out << ir::value_name(*instruction.result) << " = " << function.locals[instruction.local].name << "[...]";
          break;
        case InstructionKind::StoreElement:
          out << function.locals[instruction.local].name << "[...] = " << ir::value_name(instruction.operands.back());
          break;
        case InstructionKind::Unary:
          out << ir::value_name(*instruction.result) << " = " << ir::unary_op_name(instruction.unary_op) << ' '
              << ir::value_name(instruction.operands.front());
          break;
        case InstructionKind::Binary:
          out << ir::value_name(*instruction.result) << " = " << ir::binary_op_name(instruction.binary_op) << ' '
              << ir::value_name(instruction.operands[0]) << ", " << ir::value_name(instruction.operands[1]);
          break;
        case InstructionKind::Call:
          if (instruction.result.has_value()) {
            out << ir::value_name(*instruction.result) << " = ";
          }
          out << "call " << instruction.callee << "(...)";
          break;
      }
      out << '\n';
    }
  }
  return out.str();
}

SccpResult run_sccp(const Function& function, const ControlFlowGraph& cfg, const SsaForm& ssa) {
  using Kind = LatticeValue::Kind;
  SccpResult result;
  result.values.assign(function.values.size(), LatticeValue{});
  std::set<std::pair<BlockId, BlockId>> executable_edges;
  result.executable_blocks.insert(cfg.entry_block);

  auto local_value = [&](LocalId local, std::size_t version) -> LatticeValue {
    if (version == 0) {
      return kBottom;  // parameter value or uninitialised local on entry
    }
    const auto found = result.local_versions.find({local, version});
    return found == result.local_versions.end() ? LatticeValue{} : found->second;
  };
  bool changed = true;
  auto lower_to = [&](LatticeValue& slot, const LatticeValue& computed) {
    const LatticeValue next = meet(slot, computed);
    if (!(next == slot)) {
      slot = next;
      changed = true;
    }
  };
  auto mark_edge = [&](BlockId from, BlockId to) {
    if (executable_edges.insert({from, to}).second) {
      result.executable_blocks.insert(to);
      changed = true;
    }
  };

  while (changed) {
    changed = false;
    ++result.iterations;
    for (const auto& block : function.blocks) {
      if (!result.executable_blocks.contains(block.id)) {
        continue;
      }
      for (const auto& phi : ssa.phis[block.id]) {
        LatticeValue merged;
        for (const auto& [predecessor, version] : phi.arguments) {
          if (executable_edges.contains({predecessor, block.id})) {
            merged = meet(merged, local_value(phi.local, version));
          }
        }
        lower_to(result.local_versions[{phi.local, phi.version}], merged);
      }
      for (std::size_t index = 0; index < block.instructions.size(); ++index) {
        const Instruction& instruction = block.instructions[index];
        const bool tracked = instruction.local < ssa.tracked_local.size() && ssa.tracked_local[instruction.local];
        switch (instruction.kind) {
          case InstructionKind::ConstInt:
            lower_to(result.values[*instruction.result], constant(instruction.int_immediate));
            break;
          case InstructionKind::ConstBool:
            lower_to(result.values[*instruction.result], constant(instruction.bool_immediate ? 1 : 0));
            break;
          case InstructionKind::LoadLocal:
            lower_to(result.values[*instruction.result],
                     tracked ? local_value(instruction.local, ssa.versions[block.id][index]) : kBottom);
            break;
          case InstructionKind::StoreLocal:
            if (tracked) {
              lower_to(result.local_versions[{instruction.local, ssa.versions[block.id][index]}],
                       result.values[instruction.operands.front()]);
            }
            break;
          case InstructionKind::LoadElement:
          case InstructionKind::Call:
            if (instruction.result.has_value()) {
              lower_to(result.values[*instruction.result], kBottom);
            }
            break;
          case InstructionKind::StoreElement:
            break;
          case InstructionKind::Unary: {
            const LatticeValue operand = result.values[instruction.operands.front()];
            LatticeValue computed = operand;
            if (operand.kind == Kind::Constant) {
              computed = constant(instruction.unary_op == ir::UnaryOp::Negate ? wrap(-operand.value)
                                                                              : (operand.value == 0 ? 1 : 0));
            }
            lower_to(result.values[*instruction.result], computed);
            break;
          }
          case InstructionKind::Binary: {
            const LatticeValue lhs = result.values[instruction.operands[0]];
            const LatticeValue rhs = result.values[instruction.operands[1]];
            LatticeValue computed;
            if (lhs.kind == Kind::Bottom || rhs.kind == Kind::Bottom) {
              computed = kBottom;
            } else if (lhs.kind == Kind::Constant && rhs.kind == Kind::Constant) {
              const std::int64_t a = lhs.value;
              const std::int64_t b = rhs.value;
              switch (instruction.binary_op) {
                case ir::BinaryOp::Add: computed = constant(wrap(a + b)); break;
                case ir::BinaryOp::Sub: computed = constant(wrap(a - b)); break;
                case ir::BinaryOp::Mul: computed = constant(wrap(a * b)); break;
                case ir::BinaryOp::Div: computed = b == 0 ? kBottom : constant(wrap(a / b)); break;
                case ir::BinaryOp::Mod: computed = b == 0 ? kBottom : constant(wrap(a % b)); break;
                case ir::BinaryOp::Less: computed = constant(a < b ? 1 : 0); break;
                case ir::BinaryOp::LessEqual: computed = constant(a <= b ? 1 : 0); break;
                case ir::BinaryOp::Greater: computed = constant(a > b ? 1 : 0); break;
                case ir::BinaryOp::GreaterEqual: computed = constant(a >= b ? 1 : 0); break;
                case ir::BinaryOp::Equal: computed = constant(a == b ? 1 : 0); break;
                case ir::BinaryOp::NotEqual: computed = constant(a != b ? 1 : 0); break;
                case ir::BinaryOp::LogicalAnd: computed = constant((a != 0 && b != 0) ? 1 : 0); break;
                case ir::BinaryOp::LogicalOr: computed = constant((a != 0 || b != 0) ? 1 : 0); break;
              }
            }
            lower_to(result.values[*instruction.result], computed);
            break;
          }
        }
      }
      if (!block.terminator.has_value()) {
        continue;
      }
      const auto& terminator = *block.terminator;
      if (terminator.kind == ir::TerminatorKind::Jump) {
        mark_edge(block.id, terminator.true_target);
      } else if (terminator.kind == ir::TerminatorKind::Branch) {
        const LatticeValue condition = result.values[*terminator.condition];
        if (condition.kind == Kind::Constant) {
          mark_edge(block.id, condition.value != 0 ? terminator.true_target : terminator.false_target);
        } else if (condition.kind == Kind::Bottom) {
          mark_edge(block.id, terminator.true_target);
          mark_edge(block.id, terminator.false_target);
        }
      }
    }
  }

  for (const auto& block : function.blocks) {
    if (!result.executable_blocks.contains(block.id)) {
      continue;
    }
    for (const auto& instruction : block.instructions) {
      if (instruction.result.has_value() && instruction.kind != InstructionKind::ConstInt &&
          instruction.kind != InstructionKind::ConstBool &&
          result.values[*instruction.result].kind == Kind::Constant) {
        ++result.constant_values;
      }
    }
    if (block.terminator.has_value() && block.terminator->kind == ir::TerminatorKind::Branch &&
        result.values[*block.terminator->condition].kind == Kind::Constant) {
      ++result.resolved_branches;
    }
  }
  return result;
}

std::string print_sccp(const Function& function, const SsaForm& ssa, const SccpResult& result) {
  std::ostringstream out;
  out << "func " << function.name << " sccp: " << result.constant_values << " computed value(s) constant, "
      << result.resolved_branches << " branch(es) resolved, "
      << (function.blocks.size() - result.executable_blocks.size()) << " unreachable block(s), "
      << result.iterations << " iteration(s)\n";
  for (const auto& block : function.blocks) {
    const bool live = result.executable_blocks.contains(block.id);
    out << "  " << block_name(function, block.id) << (live ? "" : "   ; unreachable") << '\n';
    if (!live) {
      continue;
    }
    for (const auto& phi : ssa.phis[block.id]) {
      const auto found = result.local_versions.find({phi.local, phi.version});
      out << "    " << local_name(function, phi.local, phi.version) << " (phi) = "
          << lattice_text(found == result.local_versions.end() ? LatticeValue{} : found->second) << '\n';
    }
    for (std::size_t index = 0; index < block.instructions.size(); ++index) {
      const auto& instruction = block.instructions[index];
      if (instruction.kind == InstructionKind::StoreLocal && ssa.tracked_local[instruction.local]) {
        const auto found = result.local_versions.find({instruction.local, ssa.versions[block.id][index]});
        out << "    " << local_name(function, instruction.local, ssa.versions[block.id][index]) << " = "
            << lattice_text(found == result.local_versions.end() ? LatticeValue{} : found->second) << '\n';
      }
    }
  }
  return out.str();
}

}  // namespace nexus::compiler::analysis
