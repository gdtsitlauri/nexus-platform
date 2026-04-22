#include "nexus/sim/multi_cycle/control.hpp"

#include <sstream>
#include <string_view>

#include "nexus/mips/isa/instruction.hpp"

namespace nexus::sim::multi_cycle {

namespace {

enum class InstructionClass {
  RegisterALU,
  ImmediateALU,
  LoadWord,
  StoreWord,
  Branch,
  Jump,
  JumpAndLink,
  JumpRegister,
  Multiply,
  Divide,
  MoveFromLo,
  MoveFromHi,
};

InstructionClass classify(const mips::loader::LoadedInstruction& instruction) {
  using mips::isa::Opcode;
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
      return InstructionClass::RegisterALU;
    case Opcode::Addiu:
    case Opcode::Ori:
    case Opcode::Xori:
    case Opcode::Sltiu:
    case Opcode::Lui:
      return InstructionClass::ImmediateALU;
    case Opcode::Lw:
      return InstructionClass::LoadWord;
    case Opcode::Sw:
      return InstructionClass::StoreWord;
    case Opcode::Beq:
    case Opcode::Bne:
      return InstructionClass::Branch;
    case Opcode::J:
      return InstructionClass::Jump;
    case Opcode::Jal:
      return InstructionClass::JumpAndLink;
    case Opcode::Jr:
      return InstructionClass::JumpRegister;
    case Opcode::Mult:
      return InstructionClass::Multiply;
    case Opcode::Div:
      return InstructionClass::Divide;
    case Opcode::Mflo:
      return InstructionClass::MoveFromLo;
    case Opcode::Mfhi:
      return InstructionClass::MoveFromHi;
  }

  return InstructionClass::RegisterALU;
}

ControlStep step(State state, ControlSignals signals, std::string description) {
  return ControlStep{.state = state, .signals = signals, .description = std::move(description)};
}

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

ControlPlan build_hardwired_plan(const mips::loader::LoadedInstruction& instruction) {
  ControlPlan plan;
  plan.push_back(step(
      State::Fetch,
      {.ir_write = true, .mem_read = true, .pc_write = true},
      "hardwired fetch: read instruction memory and increment PC"));
  plan.push_back(step(
      State::Decode,
      {.reg_read = true},
      "hardwired decode: latch register operands and classify instruction"));

  switch (classify(instruction)) {
    case InstructionClass::RegisterALU:
      plan.push_back(step(
          State::ExecuteALU,
          {.alu_compute = true},
          "hardwired execute: ALU computes register-register result"));
      plan.push_back(step(
          State::WritebackReg,
          {.reg_write = true},
          "hardwired writeback: commit ALU result to destination register"));
      return plan;
    case InstructionClass::ImmediateALU:
      plan.push_back(step(
          State::ExecuteImmediate,
          {.alu_compute = true},
          "hardwired execute: ALU computes immediate result"));
      plan.push_back(step(
          State::WritebackImmediate,
          {.reg_write = true},
          "hardwired writeback: commit immediate result to target register"));
      return plan;
    case InstructionClass::LoadWord:
      plan.push_back(step(
          State::ExecuteAddress,
          {.alu_compute = true},
          "hardwired execute: adder computes effective address"));
      plan.push_back(step(
          State::MemoryRead,
          {.mem_read = true, .mdr_write = true},
          "hardwired memory: read data word into MDR"));
      plan.push_back(step(
          State::WritebackLoad,
          {.reg_write = true},
          "hardwired writeback: load MDR into target register"));
      return plan;
    case InstructionClass::StoreWord:
      plan.push_back(step(
          State::ExecuteAddress,
          {.alu_compute = true},
          "hardwired execute: adder computes store address"));
      plan.push_back(step(
          State::MemoryWrite,
          {.mem_write = true},
          "hardwired memory: write register value to data memory"));
      return plan;
    case InstructionClass::Branch:
      plan.push_back(step(
          State::ExecuteBranch,
          {.alu_compute = true, .pc_conditional = true},
          "hardwired branch: compare register operands and update PC if taken"));
      return plan;
    case InstructionClass::Jump:
    case InstructionClass::JumpAndLink:
      plan.push_back(step(
          State::ExecuteJump,
          {
              .reg_write = instruction.opcode == mips::isa::Opcode::Jal,
              .pc_write = true,
              .link = instruction.opcode == mips::isa::Opcode::Jal,
          },
          "hardwired jump: update PC and optionally write return address"));
      return plan;
    case InstructionClass::JumpRegister:
      plan.push_back(step(
          State::ExecuteJumpRegister,
          {.pc_write = true},
          "hardwired jump-register: load PC from register operand"));
      return plan;
    case InstructionClass::Multiply:
      plan.push_back(step(
          State::ExecuteMultiply,
          {.hi_lo_write = true},
          "hardwired execute: multiplier writes HI/LO"));
      return plan;
    case InstructionClass::Divide:
      plan.push_back(step(
          State::ExecuteDivide,
          {.hi_lo_write = true},
          "hardwired execute: divider writes HI/LO"));
      return plan;
    case InstructionClass::MoveFromLo:
      plan.push_back(step(
          State::WritebackFromLo,
          {.reg_write = true},
          "hardwired writeback: copy LO into destination register"));
      return plan;
    case InstructionClass::MoveFromHi:
      plan.push_back(step(
          State::WritebackFromHi,
          {.reg_write = true},
          "hardwired writeback: copy HI into destination register"));
      return plan;
  }

  return plan;
}

ControlPlan build_microcode_plan(const mips::loader::LoadedInstruction& instruction) {
  using IC = InstructionClass;
  static const ControlPlan kRegisterALU = {
      step(
          State::Fetch,
          {.ir_write = true, .mem_read = true, .pc_write = true},
          "microcode IF: IR <- IMem[PC], PC <- PC + 1"),
      step(
          State::Decode,
          {.reg_read = true},
          "microcode ID: A <- Reg[rs], B <- Reg[rt]"),
      step(
          State::ExecuteALU,
          {.alu_compute = true},
          "microcode EX: ALUOut <- f(A, B)"),
      step(
          State::WritebackReg,
          {.reg_write = true},
          "microcode WB: Reg[rd] <- ALUOut"),
  };
  static const ControlPlan kImmediateALU = {
      step(State::Fetch, {.ir_write = true, .mem_read = true, .pc_write = true}, "microcode IF"),
      step(State::Decode, {.reg_read = true}, "microcode ID"),
      step(State::ExecuteImmediate, {.alu_compute = true}, "microcode EXI: ALUOut <- f(A, imm)"),
      step(State::WritebackImmediate, {.reg_write = true}, "microcode WBI: Reg[rt] <- ALUOut"),
  };
  static const ControlPlan kLoadWord = {
      step(State::Fetch, {.ir_write = true, .mem_read = true, .pc_write = true}, "microcode IF"),
      step(State::Decode, {.reg_read = true}, "microcode ID"),
      step(State::ExecuteAddress, {.alu_compute = true}, "microcode EXA: ALUOut <- A + imm"),
      step(State::MemoryRead, {.mem_read = true, .mdr_write = true}, "microcode MEMR: MDR <- DMem[ALUOut]"),
      step(State::WritebackLoad, {.reg_write = true}, "microcode WBL: Reg[rt] <- MDR"),
  };
  static const ControlPlan kStoreWord = {
      step(State::Fetch, {.ir_write = true, .mem_read = true, .pc_write = true}, "microcode IF"),
      step(State::Decode, {.reg_read = true}, "microcode ID"),
      step(State::ExecuteAddress, {.alu_compute = true}, "microcode EXA: ALUOut <- A + imm"),
      step(State::MemoryWrite, {.mem_write = true}, "microcode MEMW: DMem[ALUOut] <- B"),
  };
  static const ControlPlan kBranch = {
      step(State::Fetch, {.ir_write = true, .mem_read = true, .pc_write = true}, "microcode IF"),
      step(State::Decode, {.reg_read = true}, "microcode ID"),
      step(
          State::ExecuteBranch,
          {.alu_compute = true, .pc_conditional = true},
          "microcode BR: compare operands and redirect PC if condition is true"),
  };
  static const ControlPlan kJump = {
      step(State::Fetch, {.ir_write = true, .mem_read = true, .pc_write = true}, "microcode IF"),
      step(State::Decode, {.reg_read = true}, "microcode ID"),
      step(State::ExecuteJump, {.pc_write = true}, "microcode JMP: PC <- target"),
  };
  static const ControlPlan kJumpAndLink = {
      step(State::Fetch, {.ir_write = true, .mem_read = true, .pc_write = true}, "microcode IF"),
      step(State::Decode, {.reg_read = true}, "microcode ID"),
      step(
          State::ExecuteJump,
          {.reg_write = true, .pc_write = true, .link = true},
          "microcode JAL: Reg[$ra] <- PC, PC <- target"),
  };
  static const ControlPlan kJumpRegister = {
      step(State::Fetch, {.ir_write = true, .mem_read = true, .pc_write = true}, "microcode IF"),
      step(State::Decode, {.reg_read = true}, "microcode ID"),
      step(State::ExecuteJumpRegister, {.pc_write = true}, "microcode JR: PC <- A"),
  };
  static const ControlPlan kMultiply = {
      step(State::Fetch, {.ir_write = true, .mem_read = true, .pc_write = true}, "microcode IF"),
      step(State::Decode, {.reg_read = true}, "microcode ID"),
      step(State::ExecuteMultiply, {.hi_lo_write = true}, "microcode MUL: HI/LO <- A * B"),
  };
  static const ControlPlan kDivide = {
      step(State::Fetch, {.ir_write = true, .mem_read = true, .pc_write = true}, "microcode IF"),
      step(State::Decode, {.reg_read = true}, "microcode ID"),
      step(State::ExecuteDivide, {.hi_lo_write = true}, "microcode DIV: LO <- q, HI <- r"),
  };
  static const ControlPlan kMoveFromLo = {
      step(State::Fetch, {.ir_write = true, .mem_read = true, .pc_write = true}, "microcode IF"),
      step(State::Decode, {.reg_read = true}, "microcode ID"),
      step(State::WritebackFromLo, {.reg_write = true}, "microcode MFLO: Reg[rd] <- LO"),
  };
  static const ControlPlan kMoveFromHi = {
      step(State::Fetch, {.ir_write = true, .mem_read = true, .pc_write = true}, "microcode IF"),
      step(State::Decode, {.reg_read = true}, "microcode ID"),
      step(State::WritebackFromHi, {.reg_write = true}, "microcode MFHI: Reg[rd] <- HI"),
  };

  switch (classify(instruction)) {
    case IC::RegisterALU:
      return kRegisterALU;
    case IC::ImmediateALU:
      return kImmediateALU;
    case IC::LoadWord:
      return kLoadWord;
    case IC::StoreWord:
      return kStoreWord;
    case IC::Branch:
      return kBranch;
    case IC::Jump:
      return kJump;
    case IC::JumpAndLink:
      return kJumpAndLink;
    case IC::JumpRegister:
      return kJumpRegister;
    case IC::Multiply:
      return kMultiply;
    case IC::Divide:
      return kDivide;
    case IC::MoveFromLo:
      return kMoveFromLo;
    case IC::MoveFromHi:
      return kMoveFromHi;
  }

  return kRegisterALU;
}

std::string format_control_style(ControlStyle style) {
  switch (style) {
    case ControlStyle::Hardwired:
      return "hardwired";
    case ControlStyle::Microcode:
      return "microcode";
  }
  return "unknown";
}

std::string format_state(State state) {
  switch (state) {
    case State::Fetch:
      return "fetch";
    case State::Decode:
      return "decode";
    case State::ExecuteALU:
      return "execute-alu";
    case State::ExecuteImmediate:
      return "execute-imm";
    case State::ExecuteAddress:
      return "execute-address";
    case State::MemoryRead:
      return "memory-read";
    case State::MemoryWrite:
      return "memory-write";
    case State::WritebackReg:
      return "writeback-reg";
    case State::WritebackImmediate:
      return "writeback-imm";
    case State::WritebackLoad:
      return "writeback-load";
    case State::ExecuteBranch:
      return "execute-branch";
    case State::ExecuteJump:
      return "execute-jump";
    case State::ExecuteJumpRegister:
      return "execute-jump-register";
    case State::ExecuteMultiply:
      return "execute-multiply";
    case State::ExecuteDivide:
      return "execute-divide";
    case State::WritebackFromLo:
      return "writeback-lo";
    case State::WritebackFromHi:
      return "writeback-hi";
  }
  return "unknown";
}

std::string format_control_signals(const ControlSignals& signals) {
  std::ostringstream output;
  bool first = true;
  append_flag(output, first, "ir_write", signals.ir_write);
  append_flag(output, first, "reg_read", signals.reg_read);
  append_flag(output, first, "alu", signals.alu_compute);
  append_flag(output, first, "mem_read", signals.mem_read);
  append_flag(output, first, "mem_write", signals.mem_write);
  append_flag(output, first, "reg_write", signals.reg_write);
  append_flag(output, first, "pc_write", signals.pc_write);
  append_flag(output, first, "pc_cond", signals.pc_conditional);
  append_flag(output, first, "hi_lo", signals.hi_lo_write);
  append_flag(output, first, "mdr_write", signals.mdr_write);
  append_flag(output, first, "link", signals.link);
  return first ? "-" : output.str();
}

}  // namespace nexus::sim::multi_cycle
