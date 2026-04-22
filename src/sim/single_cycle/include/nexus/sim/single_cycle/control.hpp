#pragma once

#include <string>
#include <string_view>

#include "nexus/mips/loader/parser.hpp"

namespace nexus::sim::single_cycle {

enum class ALUControl {
  Add,
  Subtract,
  And,
  Or,
  Xor,
  SetLessThanSigned,
  SetLessThanUnsigned,
  ShiftLeftLogical,
  LoadUpperImmediate,
  None,
};

struct HardwiredControlSignals {
  bool reg_write = false;
  bool mem_read = false;
  bool mem_write = false;
  bool mem_to_reg = false;
  bool uses_immediate = false;
  bool writes_rd = false;
  bool branch_eq = false;
  bool branch_ne = false;
  bool jump = false;
  bool jump_register = false;
  bool link = false;
  bool write_hi_lo = false;
  bool read_hi = false;
  bool read_lo = false;
  bool uses_shift_amount = false;
  ALUControl alu_control = ALUControl::None;
};

HardwiredControlSignals decode_control(const mips::loader::LoadedInstruction& instruction);
std::string_view alu_control_name(ALUControl control);
std::string format_control_signals(const HardwiredControlSignals& signals);

}  // namespace nexus::sim::single_cycle
