#include "nexus/mips/isa/instruction.hpp"

#include <array>
#include <string>

namespace nexus::mips::isa {

namespace {

struct RegisterEntry {
  Register reg;
  std::string_view name;
};

constexpr std::array<RegisterEntry, 15> kRegisters = {{
    {Register::Zero, "$zero"},
    {Register::V0, "$v0"},
    {Register::A0, "$a0"},
    {Register::A1, "$a1"},
    {Register::A2, "$a2"},
    {Register::A3, "$a3"},
    {Register::T0, "$t0"},
    {Register::T1, "$t1"},
    {Register::T2, "$t2"},
    {Register::T3, "$t3"},
    {Register::T4, "$t4"},
    {Register::T5, "$t5"},
    {Register::T6, "$t6"},
    {Register::T7, "$t7"},
    {Register::SP, "$sp"},
}};

}  // namespace

std::string_view register_name(Register reg) {
  switch (reg) {
    case Register::Zero:
      return "$zero";
    case Register::V0:
      return "$v0";
    case Register::A0:
      return "$a0";
    case Register::A1:
      return "$a1";
    case Register::A2:
      return "$a2";
    case Register::A3:
      return "$a3";
    case Register::T0:
      return "$t0";
    case Register::T1:
      return "$t1";
    case Register::T2:
      return "$t2";
    case Register::T3:
      return "$t3";
    case Register::T4:
      return "$t4";
    case Register::T5:
      return "$t5";
    case Register::T6:
      return "$t6";
    case Register::T7:
      return "$t7";
    case Register::SP:
      return "$sp";
    case Register::FP:
      return "$fp";
    case Register::RA:
      return "$ra";
  }

  return "$invalid";
}

std::optional<Register> parse_register(std::string_view name) {
  for (const auto& entry : kRegisters) {
    if (entry.name == name) {
      return entry.reg;
    }
  }
  if (name == "$fp") {
    return Register::FP;
  }
  if (name == "$ra") {
    return Register::RA;
  }
  return std::nullopt;
}

std::size_t register_index(Register reg) {
  return static_cast<std::size_t>(reg);
}

std::string_view opcode_name(Opcode opcode) {
  switch (opcode) {
    case Opcode::Add:
      return "add";
    case Opcode::Addu:
      return "addu";
    case Opcode::Addiu:
      return "addiu";
    case Opcode::Sub:
      return "sub";
    case Opcode::And:
      return "and";
    case Opcode::Or:
      return "or";
    case Opcode::Ori:
      return "ori";
    case Opcode::Xor:
      return "xor";
    case Opcode::Xori:
      return "xori";
    case Opcode::Slt:
      return "slt";
    case Opcode::Sltu:
      return "sltu";
    case Opcode::Sltiu:
      return "sltiu";
    case Opcode::Sll:
      return "sll";
    case Opcode::Lui:
      return "lui";
    case Opcode::Lw:
      return "lw";
    case Opcode::Sw:
      return "sw";
    case Opcode::Beq:
      return "beq";
    case Opcode::Bne:
      return "bne";
    case Opcode::J:
      return "j";
    case Opcode::Jal:
      return "jal";
    case Opcode::Jr:
      return "jr";
    case Opcode::Mult:
      return "mult";
    case Opcode::Div:
      return "div";
    case Opcode::Mflo:
      return "mflo";
    case Opcode::Mfhi:
      return "mfhi";
  }

  return "invalid";
}

std::optional<Opcode> parse_opcode(std::string_view text) {
  constexpr std::array<Opcode, 25> kOpcodes = {
      Opcode::Add,   Opcode::Addu,  Opcode::Addiu, Opcode::Sub,   Opcode::And,
      Opcode::Or,    Opcode::Ori,   Opcode::Xor,   Opcode::Xori,  Opcode::Slt,
      Opcode::Sltu,  Opcode::Sltiu, Opcode::Sll,   Opcode::Lui,   Opcode::Lw,
      Opcode::Sw,    Opcode::Beq,   Opcode::Bne,   Opcode::J,     Opcode::Jal,
      Opcode::Jr,    Opcode::Mult,  Opcode::Div,   Opcode::Mflo,  Opcode::Mfhi,
  };

  for (const Opcode opcode : kOpcodes) {
    if (opcode_name(opcode) == text) {
      return opcode;
    }
  }
  return std::nullopt;
}

}  // namespace nexus::mips::isa
