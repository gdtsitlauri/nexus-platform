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
  bool record_events = false;  // fill RunResult::events (one per executed instruction)
};

// One executed instruction, for trace-driven timing models: the effective byte address of loads and stores.
struct Event {
  std::size_t pc = 0;
  mips::isa::Opcode opcode{};
  std::int32_t address = -1;
};

struct RunResult {
  bool success = false;
  std::int32_t exit_code = 0;
  std::string error;
  std::size_t executed_instructions = 0;
  std::vector<std::string> trace_lines;
  std::vector<std::int32_t> console_bytes;  // words stored to the console/UART data register (0x10000)
  std::array<std::int32_t, 32> registers{};
  std::vector<Event> events;
};

RunResult run_program(const mips::loader::LoadedProgram& program, const RunOptions& options = {});

}  // namespace nexus::sim::functional
