#include "nexus/compiler/backend_mips/regalloc.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <sstream>

namespace nexus::compiler::backend_mips {

namespace {

using ir::BlockId;
using ir::Function;
using ir::Instruction;
using ir::InstructionKind;
using mips::isa::Register;

constexpr std::array<Register, 6> kCallerSaved = {
    Register::T4, Register::T5, Register::T6, Register::T7, Register::T8, Register::T9};
constexpr std::array<Register, 8> kCalleeSaved = {
    Register::S0, Register::S1, Register::S2, Register::S3,
    Register::S4, Register::S5, Register::S6, Register::S7};

bool is_callee_saved(Register reg) {
  return std::find(kCalleeSaved.begin(), kCalleeSaved.end(), reg) != kCalleeSaved.end();
}

// Entity numbering: values occupy [0, V), locals occupy [V, V + L).
struct Entities {
  std::size_t value_count = 0;
  std::size_t local_count = 0;
  std::vector<bool> candidate;

  [[nodiscard]] std::size_t size() const { return value_count + local_count; }
  [[nodiscard]] std::size_t local(std::size_t id) const { return value_count + id; }
};

Entities collect_candidates(const Function& function) {
  Entities entities;
  entities.value_count = function.values.size();
  entities.local_count = function.locals.size();
  entities.candidate.assign(entities.size(), false);
  for (std::size_t value = 0; value < entities.value_count; ++value) {
    entities.candidate[value] = true;
  }
  for (const auto& local : function.locals) {
    entities.candidate[entities.local(local.id)] = local.type.is_scalar();
  }
  // A local whose address escapes into a call must stay in memory.
  for (const auto& block : function.blocks) {
    for (const auto& instruction : block.instructions) {
      if (instruction.kind != InstructionKind::Call) {
        continue;
      }
      for (const auto& argument : instruction.call_arguments) {
        if (argument.kind == ir::CallArgumentKind::LocalRef && argument.local != ir::kInvalidId) {
          entities.candidate[entities.local(argument.local)] = false;
        }
      }
    }
  }
  return entities;
}

void instruction_effects(
    const Instruction& instruction,
    const Entities& entities,
    std::vector<std::size_t>& uses,
    std::vector<std::size_t>& defs) {
  uses.clear();
  defs.clear();
  for (const ir::ValueId value : ir::instruction_uses(instruction)) {
    uses.push_back(value);
  }
  if (const auto def = ir::instruction_def(instruction); def.has_value()) {
    defs.push_back(*def);
  }
  if (instruction.kind == InstructionKind::LoadLocal) {
    uses.push_back(entities.local(instruction.local));
  }
  if (instruction.kind == InstructionKind::StoreLocal) {
    defs.push_back(entities.local(instruction.local));
  }
}

std::vector<BlockId> successors(const ir::BasicBlock& block) {
  if (!block.terminator.has_value()) {
    return {};
  }
  switch (block.terminator->kind) {
    case ir::TerminatorKind::Jump:
      return {block.terminator->true_target};
    case ir::TerminatorKind::Branch:
      return {block.terminator->true_target, block.terminator->false_target};
    case ir::TerminatorKind::Return:
      return {};
  }
  return {};
}

}  // namespace

RegisterAllocation allocate_registers_linear_scan(const Function& function) {
  RegisterAllocation allocation;
  allocation.value_regs.assign(function.values.size(), std::nullopt);
  allocation.local_regs.assign(function.locals.size(), std::nullopt);

  const Entities entities = collect_candidates(function);
  const std::size_t entity_count = entities.size();
  const std::size_t block_count = function.blocks.size();

  // Index blocks by id (ids are dense but not necessarily in vector order).
  std::vector<std::size_t> block_index(block_count, 0);
  for (std::size_t index = 0; index < block_count; ++index) {
    block_index[function.blocks[index].id] = index;
  }

  // Linear positions: 0 = prologue (parameters arrive), each block gets its own label position.
  std::vector<int> block_start(block_count, 0);
  std::vector<int> block_end(block_count, 0);
  std::vector<int> call_positions;
  std::vector<int> first(entity_count, std::numeric_limits<int>::max());
  std::vector<int> last(entity_count, std::numeric_limits<int>::min());
  auto touch = [&](std::size_t entity, int position) {
    first[entity] = std::min(first[entity], position);
    last[entity] = std::max(last[entity], position);
  };

  // Per-block upward-exposed uses and definitions.
  std::vector<std::vector<bool>> use_set(block_count, std::vector<bool>(entity_count, false));
  std::vector<std::vector<bool>> def_set(block_count, std::vector<bool>(entity_count, false));

  int position = 1;
  std::vector<std::size_t> uses;
  std::vector<std::size_t> defs;
  for (std::size_t index = 0; index < block_count; ++index) {
    const auto& block = function.blocks[index];
    block_start[index] = position++;
    for (const auto& instruction : block.instructions) {
      instruction_effects(instruction, entities, uses, defs);
      for (const std::size_t entity : uses) {
        touch(entity, position);
        if (!def_set[index][entity]) {
          use_set[index][entity] = true;
        }
      }
      for (const std::size_t entity : defs) {
        touch(entity, position);
        def_set[index][entity] = true;
      }
      if (instruction.kind == InstructionKind::Call) {
        call_positions.push_back(position);
      }
      ++position;
    }
    if (block.terminator.has_value()) {
      for (const ir::ValueId value : ir::terminator_uses(*block.terminator)) {
        touch(value, position);
        if (!def_set[index][value]) {
          use_set[index][value] = true;
        }
      }
    }
    block_end[index] = position++;
  }
  for (const auto& parameter : function.parameters) {
    if (parameter.local != ir::kInvalidId) {
      touch(entities.local(parameter.local), 0);
    }
  }

  // Backward iterative liveness: in = use U (out - def), out = U in(succ).
  std::vector<std::vector<bool>> live_in(block_count, std::vector<bool>(entity_count, false));
  std::vector<std::vector<bool>> live_out(block_count, std::vector<bool>(entity_count, false));
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t reverse = block_count; reverse-- > 0;) {
      std::vector<bool> out(entity_count, false);
      for (const BlockId successor : successors(function.blocks[reverse])) {
        const auto& successor_in = live_in[block_index[successor]];
        for (std::size_t entity = 0; entity < entity_count; ++entity) {
          out[entity] = out[entity] || successor_in[entity];
        }
      }
      std::vector<bool> in(entity_count, false);
      for (std::size_t entity = 0; entity < entity_count; ++entity) {
        in[entity] = use_set[reverse][entity] || (out[entity] && !def_set[reverse][entity]);
      }
      if (out != live_out[reverse] || in != live_in[reverse]) {
        live_out[reverse] = std::move(out);
        live_in[reverse] = std::move(in);
        changed = true;
      }
    }
  }
  for (std::size_t index = 0; index < block_count; ++index) {
    for (std::size_t entity = 0; entity < entity_count; ++entity) {
      if (live_in[index][entity]) {
        touch(entity, block_start[index]);
      }
      if (live_out[index][entity]) {
        touch(entity, block_end[index]);
      }
    }
  }

  // Build intervals for candidates that actually occur.
  std::vector<LiveInterval> intervals;
  for (std::size_t entity = 0; entity < entity_count; ++entity) {
    if (!entities.candidate[entity] || first[entity] > last[entity]) {
      continue;
    }
    LiveInterval interval;
    interval.owner = entity < entities.value_count ? IntervalOwner::Value : IntervalOwner::Local;
    interval.id = entity < entities.value_count ? entity : entity - entities.value_count;
    interval.start = first[entity];
    interval.end = last[entity];
    interval.crosses_call = std::any_of(call_positions.begin(), call_positions.end(), [&](int call) {
      return interval.start < call && call < interval.end;
    });
    intervals.push_back(interval);
  }
  std::sort(intervals.begin(), intervals.end(), [](const LiveInterval& lhs, const LiveInterval& rhs) {
    return lhs.start != rhs.start ? lhs.start < rhs.start : lhs.end < rhs.end;
  });

  // Linear scan with the "spill the interval that ends furthest" heuristic.
  std::vector<Register> free_caller(kCallerSaved.rbegin(), kCallerSaved.rend());
  std::vector<Register> free_callee(kCalleeSaved.rbegin(), kCalleeSaved.rend());
  std::vector<std::size_t> active;
  auto release = [&](Register reg) {
    (is_callee_saved(reg) ? free_callee : free_caller).push_back(reg);
  };

  for (std::size_t current = 0; current < intervals.size(); ++current) {
    LiveInterval& interval = intervals[current];
    std::erase_if(active, [&](std::size_t other) {
      if (intervals[other].end < interval.start) {
        release(*intervals[other].reg);
        return true;
      }
      return false;
    });

    std::optional<Register> chosen;
    if (!interval.crosses_call && !free_caller.empty()) {
      chosen = free_caller.back();
      free_caller.pop_back();
    } else if (!free_callee.empty()) {
      chosen = free_callee.back();
      free_callee.pop_back();
    }

    if (!chosen.has_value()) {
      // Steal from the active interval ending last whose register this interval may use.
      std::optional<std::size_t> victim;
      for (const std::size_t other : active) {
        if (interval.crosses_call && !is_callee_saved(*intervals[other].reg)) {
          continue;
        }
        if (!victim.has_value() || intervals[other].end > intervals[*victim].end) {
          victim = other;
        }
      }
      if (victim.has_value() && intervals[*victim].end > interval.end) {
        chosen = intervals[*victim].reg;
        intervals[*victim].reg.reset();
        std::erase(active, *victim);
        ++allocation.spilled;
      } else {
        ++allocation.spilled;
        continue;
      }
    }

    interval.reg = chosen;
    active.push_back(current);
  }

  for (const auto& interval : intervals) {
    if (!interval.reg.has_value()) {
      continue;
    }
    if (interval.owner == IntervalOwner::Value) {
      allocation.value_regs[interval.id] = interval.reg;
    } else {
      allocation.local_regs[interval.id] = interval.reg;
    }
    if (is_callee_saved(*interval.reg) &&
        std::find(allocation.used_callee_saved.begin(), allocation.used_callee_saved.end(), *interval.reg) ==
            allocation.used_callee_saved.end()) {
      allocation.used_callee_saved.push_back(*interval.reg);
    }
  }
  std::sort(allocation.used_callee_saved.begin(), allocation.used_callee_saved.end());
  allocation.intervals = std::move(intervals);
  return allocation;
}

std::string print_register_allocation(const Function& function, const RegisterAllocation& allocation) {
  std::ostringstream out;
  std::size_t in_registers = 0;
  for (const auto& interval : allocation.intervals) {
    in_registers += interval.reg.has_value() ? 1U : 0U;
  }
  out << "func " << function.name << " linear-scan:\n";
  out << "  intervals: " << allocation.intervals.size() << ", in registers: " << in_registers
      << ", spilled: " << allocation.spilled << ", callee-saved used: " << allocation.used_callee_saved.size()
      << '\n';
  for (const auto& interval : allocation.intervals) {
    out << "  ";
    if (interval.owner == IntervalOwner::Value) {
      out << ir::value_name(interval.id);
    } else {
      out << function.locals[interval.id].name;
    }
    out << " [" << interval.start << ", " << interval.end << "]" << (interval.crosses_call ? " crosses-call" : "")
        << " -> " << (interval.reg.has_value() ? std::string(mips::isa::register_name(*interval.reg)) : "stack")
        << '\n';
  }
  return out.str();
}

}  // namespace nexus::compiler::backend_mips
