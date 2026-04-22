#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/advanced/predictor.hpp"

namespace nexus::sim::advanced {

enum class SchedulerKind {
  InOrder,
  VliwLite,
  Scoreboard,
};

std::string_view scheduler_name(SchedulerKind scheduler);

struct RunOptions {
  bool trace = false;
  PredictorKind predictor = PredictorKind::StaticNotTaken;
  SchedulerKind scheduler = SchedulerKind::InOrder;
  std::size_t issue_width = 1;
  std::size_t max_instructions = 100000;
  std::size_t mispredict_penalty = 2;
  std::size_t memory_words = 1U << 18;
};

struct RunResult {
  bool success = false;
  std::int32_t exit_code = 0;
  std::string error;
  std::size_t executed_instructions = 0;
  std::size_t cycles = 0;
  std::size_t issued_packets = 0;
  std::size_t issued_slots = 0;
  std::size_t branch_predictions = 0;
  std::size_t branch_mispredictions = 0;
  std::size_t speculative_flush_cycles = 0;
  std::vector<std::string> trace_lines;
  std::array<std::int32_t, 32> registers{};
};

RunResult run_program(const mips::loader::LoadedProgram& program, const RunOptions& options = {});

}  // namespace nexus::sim::advanced
