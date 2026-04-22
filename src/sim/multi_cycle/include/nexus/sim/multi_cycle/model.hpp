#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/multi_cycle/control.hpp"

namespace nexus::sim::multi_cycle {

struct RunOptions {
  bool trace = false;
  ControlStyle control = ControlStyle::Hardwired;
  std::size_t max_cycles = 1000000;
  std::size_t memory_words = 1U << 18;
};

struct RunResult {
  bool success = false;
  std::int32_t exit_code = 0;
  std::string error;
  std::size_t executed_instructions = 0;
  std::size_t cycles = 0;
  std::vector<std::string> trace_lines;
  std::array<std::int32_t, 32> registers{};
};

RunResult run_program(const mips::loader::LoadedProgram& program, const RunOptions& options = {});

}  // namespace nexus::sim::multi_cycle
