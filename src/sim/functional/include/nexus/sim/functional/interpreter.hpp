#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "nexus/mips/loader/parser.hpp"

namespace nexus::sim::functional {

struct RunOptions {
  bool trace = false;
  std::size_t max_instructions = 100000;
  std::size_t memory_words = 1U << 18;
};

struct RunResult {
  bool success = false;
  std::int32_t exit_code = 0;
  std::string error;
  std::size_t executed_instructions = 0;
  std::vector<std::string> trace_lines;
  std::array<std::int32_t, 32> registers{};
};

RunResult run_program(const mips::loader::LoadedProgram& program, const RunOptions& options = {});

}  // namespace nexus::sim::functional
