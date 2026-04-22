#include "nexus/sim/multi_cycle/model.hpp"

#include <limits>
#include <sstream>
#include <string>

#include "nexus/common/arithmetic.hpp"
#include "nexus/mips/isa/instruction.hpp"

namespace nexus::sim::multi_cycle {

namespace {

using mips::isa::Opcode;
using mips::isa::Register;
using mips::loader::LoadedInstruction;
using mips::loader::LoadedProgram;

struct MachineState {
  std::array<std::int32_t, 32> regs{};
  std::int32_t hi = 0;
  std::int32_t lo = 0;
  std::vector<std::int32_t> memory;
  std::size_t pc = 0;
  std::int32_t a = 0;
  std::int32_t b = 0;
  std::int32_t alu_out = 0;
  std::int32_t mdr = 0;
};

constexpr std::int32_t kHaltReturnAddress = std::numeric_limits<std::int32_t>::min();
constexpr std::size_t kStackGuardWords = 16;

std::int32_t& reg(MachineState& state, Register reg_name) {
  return state.regs[mips::isa::register_index(reg_name)];
}

std::int32_t& checked_word_at(MachineState& state, std::int32_t address, std::string& error) {
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

std::string make_trace_line(
    std::size_t cycle,
    ControlStyle style,
    State micro_state,
    std::size_t pc,
    const LoadedInstruction& instruction,
    const ControlStep& step) {
  std::ostringstream output;
  output << "trace[multi-cycle]: cycle=" << cycle << " control=" << format_control_style(style)
         << " state=" << format_state(micro_state) << " pc=" << pc << " opcode="
         << mips::isa::opcode_name(instruction.opcode) << " signals={"
         << format_control_signals(step.signals) << "} action=" << step.description;
  return output.str();
}

std::optional<Register> destination_register(const LoadedInstruction& instruction) {
  switch (instruction.opcode) {
    case Opcode::Add:
    case Opcode::Addu:
    case Opcode::Sub:
    case Opcode::And:
    case Opcode::Or:
    case Opcode::Xor:
    case Opcode::Slt:
    case Opcode::Sltu:
    case Opcode::Sll:
    case Opcode::Mflo:
    case Opcode::Mfhi:
      return instruction.rd;
    case Opcode::Addiu:
    case Opcode::Ori:
    case Opcode::Xori:
    case Opcode::Sltiu:
    case Opcode::Lui:
    case Opcode::Lw:
      return instruction.rt;
    default:
      return std::nullopt;
  }
}

bool execute_step(
    const LoadedInstruction& instruction,
    const ControlStep& step,
    MachineState& state,
    RunResult& result) {
  switch (step.state) {
    case State::Fetch:
      state.pc += 1;
      return false;
    case State::Decode:
      state.a = reg(state, instruction.rs);
      state.b = reg(state, instruction.rt);
      return false;
    case State::ExecuteALU: {
      switch (instruction.opcode) {
        case Opcode::Add:
        case Opcode::Addu:
          state.alu_out =
              common::alu_compute(
                  common::ALUOperation::Add,
                  state.a,
                  state.b,
                  0,
                  common::AdderStyle::RippleCarry)
                  .value;
          return false;
        case Opcode::Sub:
          state.alu_out =
              common::alu_compute(
                  common::ALUOperation::Subtract,
                  state.a,
                  state.b,
                  0,
                  common::AdderStyle::RippleCarry)
                  .value;
          return false;
        case Opcode::And:
          state.alu_out = common::alu_compute(common::ALUOperation::And, state.a, state.b).value;
          return false;
        case Opcode::Or:
          state.alu_out = common::alu_compute(common::ALUOperation::Or, state.a, state.b).value;
          return false;
        case Opcode::Xor:
          state.alu_out = common::alu_compute(common::ALUOperation::Xor, state.a, state.b).value;
          return false;
        case Opcode::Slt:
          state.alu_out =
              common::alu_compute(common::ALUOperation::SetLessThanSigned, state.a, state.b).value;
          return false;
        case Opcode::Sltu:
          state.alu_out =
              common::alu_compute(common::ALUOperation::SetLessThanUnsigned, state.a, state.b).value;
          return false;
        case Opcode::Sll:
          state.alu_out = common::alu_compute(
                              common::ALUOperation::ShiftLeftLogical,
                              0,
                              state.b,
                              static_cast<std::uint32_t>(instruction.immediate))
                              .value;
          return false;
        default:
          result.error = "unexpected ALU state for opcode '" +
              std::string(mips::isa::opcode_name(instruction.opcode)) + "'";
          return true;
      }
    }
    case State::ExecuteImmediate: {
      switch (instruction.opcode) {
        case Opcode::Addiu:
          state.alu_out =
              common::alu_compute(
                  common::ALUOperation::Add,
                  state.a,
                  instruction.immediate,
                  0,
                  common::AdderStyle::RippleCarry)
                  .value;
          return false;
        case Opcode::Ori:
          state.alu_out = common::alu_compute(common::ALUOperation::Or, state.a, instruction.immediate).value;
          return false;
        case Opcode::Xori:
          state.alu_out = common::alu_compute(common::ALUOperation::Xor, state.a, instruction.immediate).value;
          return false;
        case Opcode::Sltiu:
          state.alu_out =
              common::alu_compute(common::ALUOperation::SetLessThanUnsigned, state.a, instruction.immediate).value;
          return false;
        case Opcode::Lui:
          state.alu_out =
              common::alu_compute(common::ALUOperation::LoadUpperImmediate, 0, instruction.immediate).value;
          return false;
        default:
          result.error = "unexpected immediate-execute state";
          return true;
      }
    }
    case State::ExecuteAddress:
      state.alu_out =
          common::alu_compute(
              common::ALUOperation::Add,
              state.a,
              instruction.immediate,
              0,
              common::AdderStyle::RippleCarry)
              .value;
      return false;
    case State::MemoryRead: {
      std::string error;
      state.mdr = checked_word_at(state, state.alu_out, error);
      if (!error.empty()) {
        result.error = error;
        return true;
      }
      return false;
    }
    case State::MemoryWrite: {
      std::string error;
      checked_word_at(state, state.alu_out, error) = state.b;
      if (!error.empty()) {
        result.error = error;
        return true;
      }
      return false;
    }
    case State::WritebackReg: {
      const auto destination = destination_register(instruction);
      if (!destination.has_value()) {
        result.error = "missing destination register in writeback-reg state";
        return true;
      }
      reg(state, *destination) = state.alu_out;
      state.regs[0] = 0;
      return false;
    }
    case State::WritebackImmediate: {
      const auto destination = destination_register(instruction);
      reg(state, *destination) = state.alu_out;
      state.regs[0] = 0;
      return false;
    }
    case State::WritebackLoad:
      reg(state, instruction.rt) = state.mdr;
      state.regs[0] = 0;
      return false;
    case State::ExecuteBranch: {
      const bool taken = instruction.opcode == Opcode::Beq ? (state.a == state.b) : (state.a != state.b);
      if (taken) {
        state.pc = instruction.target;
      }
      return false;
    }
    case State::ExecuteJump:
      if (instruction.opcode == Opcode::Jal) {
        reg(state, Register::RA) = static_cast<std::int32_t>(state.pc);
      }
      state.pc = instruction.target;
      state.regs[0] = 0;
      return false;
    case State::ExecuteJumpRegister: {
      const std::int32_t target = state.a;
      if (instruction.rs == Register::RA && target == kHaltReturnAddress) {
        result.success = true;
        result.exit_code = reg(state, Register::V0);
        return true;
      }
      if (target < 0) {
        result.error = "jr target out of range: " + std::to_string(target);
        return true;
      }
      state.pc = static_cast<std::size_t>(target);
      return false;
    }
    case State::ExecuteMultiply: {
      const auto mult =
          common::multiply_signed(state.a, state.b, common::MultiplierStyle::ShiftAdd);
      state.hi = mult.hi;
      state.lo = mult.lo;
      return false;
    }
    case State::ExecuteDivide: {
      const auto div =
          common::divide_signed(state.a, state.b, common::DividerStyle::Restoring);
      if (div.divide_by_zero) {
        result.error = "division by zero";
        return true;
      }
      state.lo = div.quotient;
      state.hi = div.remainder;
      return false;
    }
    case State::WritebackFromLo:
      reg(state, instruction.rd) = state.lo;
      state.regs[0] = 0;
      return false;
    case State::WritebackFromHi:
      reg(state, instruction.rd) = state.hi;
      state.regs[0] = 0;
      return false;
  }

  return false;
}

}  // namespace

RunResult run_program(const LoadedProgram& program, const RunOptions& options) {
  RunResult result;
  MachineState state;
  state.memory.assign(options.memory_words, 0);
  state.pc = program.entry_point;
  const std::size_t stack_top_words =
      state.memory.size() > kStackGuardWords ? state.memory.size() - kStackGuardWords : state.memory.size();
  reg(state, Register::SP) = static_cast<std::int32_t>(stack_top_words * sizeof(std::int32_t));
  reg(state, Register::RA) = kHaltReturnAddress;

  while (state.pc < program.instructions.size()) {
    const std::size_t instruction_pc = state.pc;
    const LoadedInstruction instruction = program.instructions[instruction_pc];
    const ControlPlan plan =
        options.control == ControlStyle::Hardwired ? build_hardwired_plan(instruction)
                                                   : build_microcode_plan(instruction);

    bool halted = false;
    for (const auto& step : plan) {
      if (result.cycles >= options.max_cycles) {
        result.error = "multi-cycle machine exceeded cycle limit";
        result.registers = state.regs;
        return result;
      }

      result.cycles += 1;
      if (options.trace) {
        result.trace_lines.push_back(
            make_trace_line(
                result.cycles,
                options.control,
                step.state,
                instruction_pc,
                instruction,
                step));
      }

      halted = execute_step(instruction, step, state, result);
      if (!result.error.empty()) {
        result.registers = state.regs;
        return result;
      }

      if (step.state == State::ExecuteJumpRegister && !result.success) {
        if (state.pc >= program.instructions.size()) {
          result.error = "jr target out of range: " + std::to_string(state.pc);
          result.registers = state.regs;
          return result;
        }
      }

      if (halted && result.success) {
        result.executed_instructions += 1;
        result.registers = state.regs;
        return result;
      }
    }

    result.executed_instructions += 1;
  }

  result.error = "program counter ran past loaded instructions";
  result.registers = state.regs;
  return result;
}

}  // namespace nexus::sim::multi_cycle
