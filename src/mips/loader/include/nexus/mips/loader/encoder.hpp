#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "nexus/mips/loader/parser.hpp"

namespace nexus::mips::loader {

// Encodes loaded instructions as standard MIPS32 machine words.  Program counters, branch
// targets and jump targets are instruction-word indices (as in the Nexus simulators), so a
// branch offset is target - (pc + 1) and a jump field is the target index.
struct EncodeResult {
  std::vector<std::uint32_t> words;
  std::vector<std::string> diagnostics;
};

std::optional<std::uint32_t> encode_instruction(const LoadedInstruction& instruction, std::size_t pc, std::string& error);
EncodeResult encode_program(const LoadedProgram& program);
// One 8-digit hex word per line ($readmemh format).
std::string to_hex_image(const std::vector<std::uint32_t>& words);

}  // namespace nexus::mips::loader
