#pragma once

#include <string>
#include <vector>

#include "nexus/mips/loader/parser.hpp"

namespace nexus::sim::multi_cycle {

enum class ControlStyle {
  Hardwired,
  Microcode,
};

enum class State {
  Fetch,
  Decode,
  ExecuteALU,
  ExecuteImmediate,
  ExecuteAddress,
  MemoryRead,
  MemoryWrite,
  WritebackReg,
  WritebackImmediate,
  WritebackLoad,
  ExecuteBranch,
  ExecuteJump,
  ExecuteJumpRegister,
  ExecuteMultiply,
  ExecuteDivide,
  WritebackFromLo,
  WritebackFromHi,
};

struct ControlSignals {
  bool ir_write = false;
  bool reg_read = false;
  bool alu_compute = false;
  bool mem_read = false;
  bool mem_write = false;
  bool reg_write = false;
  bool pc_write = false;
  bool pc_conditional = false;
  bool hi_lo_write = false;
  bool mdr_write = false;
  bool link = false;
};

struct ControlStep {
  State state = State::Fetch;
  ControlSignals signals{};
  std::string description;
};

using ControlPlan = std::vector<ControlStep>;

ControlPlan build_hardwired_plan(const mips::loader::LoadedInstruction& instruction);
ControlPlan build_microcode_plan(const mips::loader::LoadedInstruction& instruction);

std::string format_control_style(ControlStyle style);
std::string format_state(State state);
std::string format_control_signals(const ControlSignals& signals);

}  // namespace nexus::sim::multi_cycle
