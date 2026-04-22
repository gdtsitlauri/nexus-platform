#include "nexus/sim/single_cycle/model.hpp"

#include <limits>
#include <sstream>
#include <string>

#include "nexus/common/arithmetic.hpp"
#include "nexus/mips/isa/instruction.hpp"
#include "nexus/sim/single_cycle/control.hpp"

namespace nexus::sim::single_cycle {

namespace {

using common::ALUOperation;
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

common::ALUOperation map_alu_operation(ALUControl control) {
  switch (control) {
    case ALUControl::Add:
      return ALUOperation::Add;
    case ALUControl::Subtract:
      return ALUOperation::Subtract;
    case ALUControl::And:
      return ALUOperation::And;
    case ALUControl::Or:
      return ALUOperation::Or;
    case ALUControl::Xor:
      return ALUOperation::Xor;
    case ALUControl::SetLessThanSigned:
      return ALUOperation::SetLessThanSigned;
    case ALUControl::SetLessThanUnsigned:
      return ALUOperation::SetLessThanUnsigned;
    case ALUControl::ShiftLeftLogical:
      return ALUOperation::ShiftLeftLogical;
    case ALUControl::LoadUpperImmediate:
      return ALUOperation::LoadUpperImmediate;
    case ALUControl::None:
      return ALUOperation::PassThrough;
  }

  return ALUOperation::PassThrough;
}

std::optional<Register> destination_register(
    const LoadedInstruction& instruction,
    const HardwiredControlSignals& control) {
  if (!control.reg_write) {
    return std::nullopt;
  }
  if (control.link) {
    return Register::RA;
  }
  if (control.read_hi || control.read_lo || control.writes_rd) {
    return instruction.rd;
  }
  return instruction.rt;
}

std::string format_trace(
    std::size_t cycle,
    std::size_t pc,
    const LoadedInstruction& instruction,
    const HardwiredControlSignals& control,
    std::size_t next_pc,
    std::optional<Register> destination,
    std::optional<std::int32_t> write_value,
    std::optional<std::int32_t> memory_address) {
  std::ostringstream output;
  output << "trace[single-cycle]: cycle=" << cycle << " pc=" << pc << " opcode="
         << mips::isa::opcode_name(instruction.opcode) << " control={" << format_control_signals(control)
         << "} next_pc=" << next_pc;
  if (destination.has_value() && write_value.has_value()) {
    output << " write=" << mips::isa::register_name(*destination) << "<-" << *write_value;
  }
  if (memory_address.has_value()) {
    output << " addr=" << *memory_address;
  }
  return output.str();
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
    if (result.cycles >= options.max_cycles) {
      result.error = "single-cycle instruction limit exceeded";
      result.registers = state.regs;
      return result;
    }

    const std::size_t pc_before = state.pc;
    const LoadedInstruction& instruction = program.instructions[pc_before];
    const HardwiredControlSignals control = decode_control(instruction);
    const std::int32_t rs_value = reg(state, instruction.rs);
    const std::int32_t rt_value = reg(state, instruction.rt);
    std::size_t next_pc = pc_before + 1;
    std::optional<Register> destination = destination_register(instruction, control);
    std::optional<std::int32_t> write_value;
    std::optional<std::int32_t> memory_address;
    std::string error;

    switch (instruction.opcode) {
      case Opcode::Add:
      case Opcode::Addu:
      case Opcode::Sub:
      case Opcode::And:
      case Opcode::Or:
      case Opcode::Xor:
      case Opcode::Slt:
      case Opcode::Sltu: {
        const auto alu = common::alu_compute(
            map_alu_operation(control.alu_control),
            rs_value,
            rt_value,
            0,
            common::AdderStyle::Native);
        write_value = alu.value;
        break;
      }
      case Opcode::Addiu:
      case Opcode::Ori:
      case Opcode::Xori:
      case Opcode::Sltiu:
      case Opcode::Lui: {
        const auto alu = common::alu_compute(
            map_alu_operation(control.alu_control),
            rs_value,
            instruction.immediate,
            0,
            common::AdderStyle::Native);
        write_value = alu.value;
        break;
      }
      case Opcode::Sll: {
        const auto alu = common::alu_compute(
            common::ALUOperation::ShiftLeftLogical,
            0,
            rt_value,
            static_cast<std::uint32_t>(instruction.immediate),
            common::AdderStyle::Native);
        write_value = alu.value;
        break;
      }
      case Opcode::Lw: {
        const auto addr = common::alu_compute(
            common::ALUOperation::Add,
            rs_value,
            instruction.immediate,
            0,
            common::AdderStyle::Native);
        memory_address = addr.value;
        write_value = checked_word_at(state, addr.value, error);
        if (!error.empty()) {
          result.error = error;
          result.registers = state.regs;
          return result;
        }
        break;
      }
      case Opcode::Sw: {
        const auto addr = common::alu_compute(
            common::ALUOperation::Add,
            rs_value,
            instruction.immediate,
            0,
            common::AdderStyle::Native);
        memory_address = addr.value;
        checked_word_at(state, addr.value, error) = rt_value;
        if (!error.empty()) {
          result.error = error;
          result.registers = state.regs;
          return result;
        }
        break;
      }
      case Opcode::Beq:
        if (rs_value == rt_value) {
          next_pc = instruction.target;
        }
        break;
      case Opcode::Bne:
        if (rs_value != rt_value) {
          next_pc = instruction.target;
        }
        break;
      case Opcode::J:
        next_pc = instruction.target;
        break;
      case Opcode::Jal:
        write_value = static_cast<std::int32_t>(pc_before + 1);
        next_pc = instruction.target;
        break;
      case Opcode::Jr: {
        const std::int32_t target = rs_value;
        if (instruction.rs == Register::RA && target == kHaltReturnAddress) {
          result.success = true;
          result.exit_code = reg(state, Register::V0);
          result.executed_instructions += 1;
          result.cycles += 1;
          state.regs[0] = 0;
          result.registers = state.regs;
          if (options.trace) {
            result.trace_lines.push_back(
                format_trace(result.cycles, pc_before, instruction, control, pc_before + 1, std::nullopt, std::nullopt, std::nullopt));
          }
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
        const auto mult =
            common::multiply_signed(rs_value, rt_value, common::MultiplierStyle::Native);
        state.hi = mult.hi;
        state.lo = mult.lo;
        break;
      }
      case Opcode::Div: {
        const auto div =
            common::divide_signed(rs_value, rt_value, common::DividerStyle::Native);
        if (div.divide_by_zero) {
          result.error = "division by zero";
          result.registers = state.regs;
          return result;
        }
        state.lo = div.quotient;
        state.hi = div.remainder;
        break;
      }
      case Opcode::Mflo:
        write_value = state.lo;
        break;
      case Opcode::Mfhi:
        write_value = state.hi;
        break;
    }

    if (destination.has_value() && write_value.has_value()) {
      reg(state, *destination) = *write_value;
    }

    state.regs[0] = 0;
    state.pc = next_pc;
    result.executed_instructions += 1;
    result.cycles += 1;
    if (options.trace) {
      result.trace_lines.push_back(
          format_trace(result.cycles, pc_before, instruction, control, next_pc, destination, write_value, memory_address));
    }
  }

  result.error = "program counter ran past loaded instructions";
  result.registers = state.regs;
  return result;
}

}  // namespace nexus::sim::single_cycle
