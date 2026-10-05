#include "nexus/mips/loader/encoder.hpp"

#include <cstdio>

namespace nexus::mips::loader {

namespace {

using isa::Opcode;

std::uint32_t reg(isa::Register value) {
  return static_cast<std::uint32_t>(isa::register_index(value)) & 0x1fU;
}

std::uint32_t r_type(std::uint32_t rs, std::uint32_t rt, std::uint32_t rd, std::uint32_t shamt, std::uint32_t funct) {
  return (rs << 21U) | (rt << 16U) | (rd << 11U) | ((shamt & 0x1fU) << 6U) | funct;
}

std::uint32_t i_type(std::uint32_t opcode, std::uint32_t rs, std::uint32_t rt, std::uint32_t immediate) {
  return (opcode << 26U) | (rs << 21U) | (rt << 16U) | (immediate & 0xffffU);
}

bool fits_signed16(std::int64_t value) {
  return value >= -32768 && value <= 32767;
}

bool fits_unsigned16(std::int64_t value) {
  return value >= 0 && value <= 0xffff;
}

}  // namespace

std::optional<std::uint32_t> encode_instruction(const LoadedInstruction& in, std::size_t pc, std::string& error) {
  const std::uint32_t rs = reg(in.rs);
  const std::uint32_t rt = reg(in.rt);
  const std::uint32_t rd = reg(in.rd);
  const auto immediate = static_cast<std::int64_t>(in.immediate);
  auto signed_immediate = [&](std::uint32_t opcode) -> std::optional<std::uint32_t> {
    if (!fits_signed16(immediate)) {
      error = "immediate " + std::to_string(immediate) + " does not fit 16 signed bits";
      return std::nullopt;
    }
    return i_type(opcode, rs, rt, static_cast<std::uint32_t>(in.immediate));
  };
  auto unsigned_immediate = [&](std::uint32_t opcode) -> std::optional<std::uint32_t> {
    if (!fits_unsigned16(immediate)) {
      error = "immediate " + std::to_string(immediate) + " does not fit 16 unsigned bits";
      return std::nullopt;
    }
    return i_type(opcode, rs, rt, static_cast<std::uint32_t>(in.immediate));
  };
  switch (in.opcode) {
    case Opcode::Add: return r_type(rs, rt, rd, 0, 0x20);
    case Opcode::Addu: return r_type(rs, rt, rd, 0, 0x21);
    case Opcode::Sub: return r_type(rs, rt, rd, 0, 0x22);
    case Opcode::And: return r_type(rs, rt, rd, 0, 0x24);
    case Opcode::Or: return r_type(rs, rt, rd, 0, 0x25);
    case Opcode::Xor: return r_type(rs, rt, rd, 0, 0x26);
    case Opcode::Slt: return r_type(rs, rt, rd, 0, 0x2a);
    case Opcode::Sltu: return r_type(rs, rt, rd, 0, 0x2b);
    case Opcode::Sll:
      if (immediate < 0 || immediate > 31) {
        error = "shift amount out of range";
        return std::nullopt;
      }
      return r_type(0, rt, rd, static_cast<std::uint32_t>(in.immediate), 0x00);
    case Opcode::Jr: return r_type(rs, 0, 0, 0, 0x08);
    case Opcode::Mult: return r_type(rs, rt, 0, 0, 0x18);
    case Opcode::Div: return r_type(rs, rt, 0, 0, 0x1a);
    case Opcode::Mfhi: return r_type(0, 0, rd, 0, 0x10);
    case Opcode::Mflo: return r_type(0, 0, rd, 0, 0x12);
    case Opcode::Addiu: return signed_immediate(0x09);
    case Opcode::Sltiu: return signed_immediate(0x0b);
    case Opcode::Ori: return unsigned_immediate(0x0d);
    case Opcode::Xori: return unsigned_immediate(0x0e);
    case Opcode::Lui: return unsigned_immediate(0x0f);
    case Opcode::Lw: return signed_immediate(0x23);
    case Opcode::Sw: return signed_immediate(0x2b);
    case Opcode::Beq:
    case Opcode::Bne: {
      const std::int64_t offset = static_cast<std::int64_t>(in.target) - static_cast<std::int64_t>(pc) - 1;
      if (!fits_signed16(offset)) {
        error = "branch offset out of range";
        return std::nullopt;
      }
      return i_type(in.opcode == Opcode::Beq ? 0x04U : 0x05U, rs, rt, static_cast<std::uint32_t>(offset));
    }
    case Opcode::J:
    case Opcode::Jal:
      if (in.target >= (1U << 26U)) {
        error = "jump target out of range";
        return std::nullopt;
      }
      return ((in.opcode == Opcode::J ? 0x02U : 0x03U) << 26U) | static_cast<std::uint32_t>(in.target);
  }
  error = "unsupported opcode";
  return std::nullopt;
}

EncodeResult encode_program(const LoadedProgram& program) {
  EncodeResult result;
  for (std::size_t pc = 0; pc < program.instructions.size(); ++pc) {
    std::string error;
    const auto word = encode_instruction(program.instructions[pc], pc, error);
    if (!word.has_value()) {
      result.diagnostics.push_back("line " + std::to_string(program.instructions[pc].source_line) + ": " + error);
      result.words.push_back(0);
      continue;
    }
    result.words.push_back(*word);
  }
  return result;
}

std::string to_hex_image(const std::vector<std::uint32_t>& words) {
  std::string text;
  char buffer[16];
  for (const auto word : words) {
    std::snprintf(buffer, sizeof(buffer), "%08x\n", word);
    text += buffer;
  }
  return text;
}

}  // namespace nexus::mips::loader
