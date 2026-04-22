#include "nexus/sim/functional/interpreter.hpp"

#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "nexus/mips/isa/instruction.hpp"

namespace nexus::sim::functional {

namespace {

using mips::isa::Opcode;
using mips::isa::Register;
using mips::loader::LoadedInstruction;
using mips::loader::LoadedProgram;

struct State {
  std::array<std::int32_t, 32> regs{};
  std::int32_t hi = 0;
  std::int32_t lo = 0;
  std::vector<std::int32_t> memory;
  std::size_t pc = 0;
};

constexpr std::int32_t kHaltReturnAddress = std::numeric_limits<std::int32_t>::min();
constexpr std::size_t kStackGuardWords = 16;

std::int32_t& reg(State& state, Register reg) {
  return state.regs[mips::isa::register_index(reg)];
}

std::uint32_t as_u32(std::int32_t value) {
  return static_cast<std::uint32_t>(value);
}

std::int32_t& checked_word_at(State& state, std::int32_t address, std::string& error) {
  static std::int32_t dummy = 0;
  if (address < 0 || (address % 4) != 0) {
    error = "invalid word address " + std::to_string(address);
    return dummy;
  }

  const std::size_t index = static_cast<std::size_t>(address / 4);
  if (index >= state.memory.size()) {
    error = "memory access out of bounds at address " + std::to_string(address);
    return dummy;
  }

  return state.memory[index];
}

void note_trace(const LoadedInstruction& instruction, std::size_t pc, RunResult& result) {
  std::ostringstream line;
  line << "trace: pc=" << pc << ' ' << instruction.text;
  result.trace_lines.push_back(line.str());
}

}  // namespace

RunResult run_program(const LoadedProgram& program, const RunOptions& options) {
  RunResult result;
  State state;
  state.memory.assign(options.memory_words, 0);
  state.pc = program.entry_point;
  const std::size_t stack_top_words =
      state.memory.size() > kStackGuardWords ? state.memory.size() - kStackGuardWords : state.memory.size();
  reg(state, Register::SP) = static_cast<std::int32_t>(stack_top_words * sizeof(std::int32_t));
  reg(state, Register::RA) = kHaltReturnAddress;

  while (state.pc < program.instructions.size()) {
    if (result.executed_instructions >= options.max_instructions) {
      result.error = "instruction limit exceeded";
      result.registers = state.regs;
      return result;
    }

    const LoadedInstruction& instruction = program.instructions[state.pc];
    if (options.trace) {
      note_trace(instruction, state.pc, result);
    }

    std::size_t next_pc = state.pc + 1;
    std::string error;

    switch (instruction.opcode) {
      case Opcode::Add:
      case Opcode::Addu:
        reg(state, instruction.rd) = reg(state, instruction.rs) + reg(state, instruction.rt);
        break;
      case Opcode::Addiu:
        reg(state, instruction.rt) = reg(state, instruction.rs) + instruction.immediate;
        break;
      case Opcode::Sub:
        reg(state, instruction.rd) = reg(state, instruction.rs) - reg(state, instruction.rt);
        break;
      case Opcode::And:
        reg(state, instruction.rd) = reg(state, instruction.rs) & reg(state, instruction.rt);
        break;
      case Opcode::Or:
        reg(state, instruction.rd) = reg(state, instruction.rs) | reg(state, instruction.rt);
        break;
      case Opcode::Ori:
        reg(state, instruction.rt) =
            static_cast<std::int32_t>(as_u32(reg(state, instruction.rs)) |
                                      (static_cast<std::uint32_t>(instruction.immediate) & 0xffffU));
        break;
      case Opcode::Xor:
        reg(state, instruction.rd) = reg(state, instruction.rs) ^ reg(state, instruction.rt);
        break;
      case Opcode::Xori:
        reg(state, instruction.rt) =
            static_cast<std::int32_t>(as_u32(reg(state, instruction.rs)) ^
                                      (static_cast<std::uint32_t>(instruction.immediate) & 0xffffU));
        break;
      case Opcode::Slt:
        reg(state, instruction.rd) = reg(state, instruction.rs) < reg(state, instruction.rt) ? 1 : 0;
        break;
      case Opcode::Sltu:
        reg(state, instruction.rd) =
            as_u32(reg(state, instruction.rs)) < as_u32(reg(state, instruction.rt)) ? 1 : 0;
        break;
      case Opcode::Sltiu:
        reg(state, instruction.rt) =
            as_u32(reg(state, instruction.rs)) < as_u32(instruction.immediate) ? 1 : 0;
        break;
      case Opcode::Sll:
        reg(state, instruction.rd) =
            static_cast<std::int32_t>(as_u32(reg(state, instruction.rt)) << instruction.immediate);
        break;
      case Opcode::Lui:
        reg(state, instruction.rt) =
            static_cast<std::int32_t>((static_cast<std::uint32_t>(instruction.immediate) & 0xffffU) << 16U);
        break;
      case Opcode::Lw: {
        const std::int32_t address = reg(state, instruction.rs) + instruction.immediate;
        reg(state, instruction.rt) = checked_word_at(state, address, error);
        if (!error.empty()) {
          result.error = error;
          result.registers = state.regs;
          return result;
        }
        break;
      }
      case Opcode::Sw: {
        const std::int32_t address = reg(state, instruction.rs) + instruction.immediate;
        checked_word_at(state, address, error) = reg(state, instruction.rt);
        if (!error.empty()) {
          result.error = error;
          result.registers = state.regs;
          return result;
        }
        break;
      }
      case Opcode::Beq:
        if (reg(state, instruction.rs) == reg(state, instruction.rt)) {
          next_pc = instruction.target;
        }
        break;
      case Opcode::Bne:
        if (reg(state, instruction.rs) != reg(state, instruction.rt)) {
          next_pc = instruction.target;
        }
        break;
      case Opcode::J:
        next_pc = instruction.target;
        break;
      case Opcode::Jal:
        reg(state, Register::RA) = static_cast<std::int32_t>(state.pc + 1);
        next_pc = instruction.target;
        break;
      case Opcode::Jr: {
        const std::int32_t target = reg(state, instruction.rs);
        if (instruction.rs == Register::RA && target == kHaltReturnAddress) {
          result.success = true;
          result.exit_code = reg(state, Register::V0);
          result.executed_instructions += 1;
          result.registers = state.regs;
          return result;
        }
        if (target < 0 || static_cast<std::size_t>(target) >= program.instructions.size()) {
          result.error = "jr target out of range: " + std::to_string(target);
          result.registers = state.regs;
          return result;
        }
        next_pc = static_cast<std::size_t>(target);
        break;
      }
      case Opcode::Mult: {
        const std::int64_t wide = static_cast<std::int64_t>(reg(state, instruction.rs)) *
            static_cast<std::int64_t>(reg(state, instruction.rt));
        state.lo = static_cast<std::int32_t>(wide & 0xffffffffLL);
        state.hi = static_cast<std::int32_t>((wide >> 32) & 0xffffffffLL);
        break;
      }
      case Opcode::Div:
        if (reg(state, instruction.rt) == 0) {
          result.error = "division by zero";
          result.registers = state.regs;
          return result;
        }
        state.lo = reg(state, instruction.rs) / reg(state, instruction.rt);
        state.hi = reg(state, instruction.rs) % reg(state, instruction.rt);
        break;
      case Opcode::Mflo:
        reg(state, instruction.rd) = state.lo;
        break;
      case Opcode::Mfhi:
        reg(state, instruction.rd) = state.hi;
        break;
    }

    state.regs[0] = 0;
    state.pc = next_pc;
    result.executed_instructions += 1;
  }

  result.error = "program counter ran past loaded instructions";
  result.registers = state.regs;
  return result;
}

}  // namespace nexus::sim::functional
