#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "nexus/compiler/ir/ir.hpp"
#include "nexus/mips/isa/instruction.hpp"

namespace nexus::compiler::backend_mips {

// Linear-scan register allocation (Poletto & Sarkar, TOPLAS 1999) over the non-SSA Nexus IR.
//
// Candidates are all IR values plus every scalar local whose address is never taken.  Live
// intervals are the hull of the block-level liveness solution in the linear block order, so a
// value that is live around a loop back edge covers the whole loop.  Intervals that cross a call
// may only use callee-saved registers ($s0-$s7, saved in the prologue); the others prefer the
// caller-saved pool ($t4-$t9).  $t0-$t3 stay reserved as code-generation scratch registers.

enum class IntervalOwner {
  Value,
  Local,
};

struct LiveInterval {
  IntervalOwner owner = IntervalOwner::Value;
  std::size_t id = 0;
  int start = 0;
  int end = 0;
  bool crosses_call = false;
  std::optional<mips::isa::Register> reg;
};

struct RegisterAllocation {
  std::vector<std::optional<mips::isa::Register>> value_regs;
  std::vector<std::optional<mips::isa::Register>> local_regs;
  std::vector<mips::isa::Register> used_callee_saved;
  std::vector<LiveInterval> intervals;
  std::size_t spilled = 0;
};

[[nodiscard]] RegisterAllocation allocate_registers_linear_scan(const ir::Function& function);
[[nodiscard]] std::string print_register_allocation(
    const ir::Function& function,
    const RegisterAllocation& allocation);

}  // namespace nexus::compiler::backend_mips
