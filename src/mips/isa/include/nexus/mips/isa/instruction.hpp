#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace nexus::mips::isa {

enum class Register : std::size_t {
  Zero = 0,
  V0 = 2,
  A0 = 4,
  A1 = 5,
  A2 = 6,
  A3 = 7,
  T0 = 8,
  T1 = 9,
  T2 = 10,
  T3 = 11,
  T4 = 12,
  T5 = 13,
  T6 = 14,
  T7 = 15,
  SP = 29,
  FP = 30,
  RA = 31,
};

enum class Opcode {
  Add,
  Addu,
  Addiu,
  Sub,
  And,
  Or,
  Ori,
  Xor,
  Xori,
  Slt,
  Sltu,
  Sltiu,
  Sll,
  Lui,
  Lw,
  Sw,
  Beq,
  Bne,
  J,
  Jal,
  Jr,
  Mult,
  Div,
  Mflo,
  Mfhi,
};

std::string_view register_name(Register reg);
std::optional<Register> parse_register(std::string_view name);
std::size_t register_index(Register reg);

std::string_view opcode_name(Opcode opcode);
std::optional<Opcode> parse_opcode(std::string_view text);

}  // namespace nexus::mips::isa
