#include "nexus/sim/single_cycle/control.hpp"

#include <sstream>

#include "nexus/mips/isa/instruction.hpp"

namespace nexus::sim::single_cycle {

namespace {

void append_flag(std::ostringstream& output, bool& first, std::string_view name, bool enabled) {
  if (!enabled) {
    return;
  }
  if (!first) {
    output << ", ";
  }
  output << name;
  first = false;
}

}  // namespace

HardwiredControlSignals decode_control(const mips::loader::LoadedInstruction& instruction) {
  using mips::isa::Opcode;

  HardwiredControlSignals signals;
  switch (instruction.opcode) {
    case Opcode::Add:
    case Opcode::Addu:
      signals.reg_write = true;
      signals.writes_rd = true;
      signals.alu_control = ALUControl::Add;
      return signals;
    case Opcode::Sub:
      signals.reg_write = true;
      signals.writes_rd = true;
      signals.alu_control = ALUControl::Subtract;
      return signals;
    case Opcode::And:
      signals.reg_write = true;
      signals.writes_rd = true;
      signals.alu_control = ALUControl::And;
      return signals;
    case Opcode::Or:
      signals.reg_write = true;
      signals.writes_rd = true;
      signals.alu_control = ALUControl::Or;
      return signals;
    case Opcode::Ori:
      signals.reg_write = true;
      signals.uses_immediate = true;
      signals.alu_control = ALUControl::Or;
      return signals;
    case Opcode::Xor:
      signals.reg_write = true;
      signals.writes_rd = true;
      signals.alu_control = ALUControl::Xor;
      return signals;
    case Opcode::Xori:
      signals.reg_write = true;
      signals.uses_immediate = true;
      signals.alu_control = ALUControl::Xor;
      return signals;
    case Opcode::Slt:
      signals.reg_write = true;
      signals.writes_rd = true;
      signals.alu_control = ALUControl::SetLessThanSigned;
      return signals;
    case Opcode::Sltu:
      signals.reg_write = true;
      signals.writes_rd = true;
      signals.alu_control = ALUControl::SetLessThanUnsigned;
      return signals;
    case Opcode::Sltiu:
      signals.reg_write = true;
      signals.uses_immediate = true;
      signals.alu_control = ALUControl::SetLessThanUnsigned;
      return signals;
    case Opcode::Addiu:
      signals.reg_write = true;
      signals.uses_immediate = true;
      signals.alu_control = ALUControl::Add;
      return signals;
    case Opcode::Sll:
      signals.reg_write = true;
      signals.writes_rd = true;
      signals.uses_shift_amount = true;
      signals.alu_control = ALUControl::ShiftLeftLogical;
      return signals;
    case Opcode::Lui:
      signals.reg_write = true;
      signals.uses_immediate = true;
      signals.alu_control = ALUControl::LoadUpperImmediate;
      return signals;
    case Opcode::Lw:
      signals.reg_write = true;
      signals.mem_read = true;
      signals.mem_to_reg = true;
      signals.uses_immediate = true;
      signals.alu_control = ALUControl::Add;
      return signals;
    case Opcode::Sw:
      signals.mem_write = true;
      signals.uses_immediate = true;
      signals.alu_control = ALUControl::Add;
      return signals;
    case Opcode::Beq:
      signals.branch_eq = true;
      signals.alu_control = ALUControl::Subtract;
      return signals;
    case Opcode::Bne:
      signals.branch_ne = true;
      signals.alu_control = ALUControl::Subtract;
      return signals;
    case Opcode::J:
      signals.jump = true;
      return signals;
    case Opcode::Jal:
      signals.jump = true;
      signals.link = true;
      signals.reg_write = true;
      return signals;
    case Opcode::Jr:
      signals.jump_register = true;
      return signals;
    case Opcode::Mult:
    case Opcode::Div:
      signals.write_hi_lo = true;
      return signals;
    case Opcode::Mflo:
      signals.reg_write = true;
      signals.writes_rd = true;
      signals.read_lo = true;
      return signals;
    case Opcode::Mfhi:
      signals.reg_write = true;
      signals.writes_rd = true;
      signals.read_hi = true;
      return signals;
  }

  return signals;
}

std::string_view alu_control_name(ALUControl control) {
  switch (control) {
    case ALUControl::Add:
      return "add";
    case ALUControl::Subtract:
      return "sub";
    case ALUControl::And:
      return "and";
    case ALUControl::Or:
      return "or";
    case ALUControl::Xor:
      return "xor";
    case ALUControl::SetLessThanSigned:
      return "slt";
    case ALUControl::SetLessThanUnsigned:
      return "sltu";
    case ALUControl::ShiftLeftLogical:
      return "sll";
    case ALUControl::LoadUpperImmediate:
      return "lui";
    case ALUControl::None:
      return "none";
  }

  return "unknown";
}

std::string format_control_signals(const HardwiredControlSignals& signals) {
  std::ostringstream output;
  bool first = true;
  append_flag(output, first, "reg_write", signals.reg_write);
  append_flag(output, first, "mem_read", signals.mem_read);
  append_flag(output, first, "mem_write", signals.mem_write);
  append_flag(output, first, "mem_to_reg", signals.mem_to_reg);
  append_flag(output, first, "imm", signals.uses_immediate);
  append_flag(output, first, "rd_dst", signals.writes_rd);
  append_flag(output, first, "beq", signals.branch_eq);
  append_flag(output, first, "bne", signals.branch_ne);
  append_flag(output, first, "jump", signals.jump);
  append_flag(output, first, "jr", signals.jump_register);
  append_flag(output, first, "link", signals.link);
  append_flag(output, first, "hi_lo", signals.write_hi_lo);
  append_flag(output, first, "read_hi", signals.read_hi);
  append_flag(output, first, "read_lo", signals.read_lo);
  append_flag(output, first, "shamt", signals.uses_shift_amount);
  if (!first) {
    output << ", ";
  }
  output << "alu=" << alu_control_name(signals.alu_control);
  return output.str();
}

}  // namespace nexus::sim::single_cycle
