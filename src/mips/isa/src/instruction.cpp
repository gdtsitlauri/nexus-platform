#include "nexus/mips/isa/instruction.hpp"

#include <array>
#include <string>

namespace nexus::mips::isa {

namespace {

struct RegisterEntry {
  Register reg;
  std::string_view name;
};

constexpr std::array<RegisterEntry, 32> kRegisters = {{
    {Register::Zero, "$zero"},
    {Register::AT, "$at"},
    {Register::V0, "$v0"},
    {Register::V1, "$v1"},
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
    {Register::S0, "$s0"},
    {Register::S1, "$s1"},
    {Register::S2, "$s2"},
    {Register::S3, "$s3"},
    {Register::S4, "$s4"},
    {Register::S5, "$s5"},
    {Register::S6, "$s6"},
    {Register::S7, "$s7"},
    {Register::T8, "$t8"},
    {Register::T9, "$t9"},
    {Register::K0, "$k0"},
    {Register::K1, "$k1"},
    {Register::GP, "$gp"},
    {Register::SP, "$sp"},
    {Register::FP, "$fp"},
    {Register::RA, "$ra"},
}};

}  // namespace

std::string_view register_name(Register reg) {
  for (const auto& entry : kRegisters) {
    if (entry.reg == reg) {
      return entry.name;
    }
  }
  return "$invalid";
}

std::optional<Register> parse_register(std::string_view name) {
  for (const auto& entry : kRegisters) {
    if (entry.name == name) {
      return entry.reg;
    }
  }
  if (name == "$s8") {
    return Register::FP;
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
