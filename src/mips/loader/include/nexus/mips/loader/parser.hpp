#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "nexus/mips/isa/instruction.hpp"

namespace nexus::mips::loader {

struct LoadedInstruction {
  isa::Opcode opcode = isa::Opcode::Addiu;
  isa::Register rd = isa::Register::Zero;
  isa::Register rs = isa::Register::Zero;
  isa::Register rt = isa::Register::Zero;
  std::int32_t immediate = 0;
  std::size_t target = 0;
  std::size_t source_line = 0;
  std::string text;
};

struct LoadedProgram {
  std::vector<LoadedInstruction> instructions;
  std::size_t entry_point = 0;
  std::unordered_map<std::string, std::size_t> labels;
};

struct ParseResult {
  std::optional<LoadedProgram> program;
  std::vector<std::string> diagnostics;
};

ParseResult load_program_from_text(const std::string& text);
ParseResult load_program_from_file(const std::string& file_name);

}  // namespace nexus::mips::loader
