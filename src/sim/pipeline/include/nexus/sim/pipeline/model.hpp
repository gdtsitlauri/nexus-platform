#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/memory/system.hpp"

namespace nexus::sim::pipeline {

enum class PredictorKind {
  StaticNotTaken,
  StaticTaken,
  StaticBackwardTakenForwardNotTaken,
};

std::string_view predictor_name(PredictorKind predictor);

struct RunOptions {
  bool trace = false;
  bool timeline = false;
  PredictorKind predictor = PredictorKind::StaticNotTaken;
  std::size_t max_cycles = 1000000;
  std::size_t memory_words = 1U << 18;
  memory::CacheMode cache_mode = memory::CacheMode::Off;
  std::size_t cache_sets = 16;
  std::size_t cache_line_words = 4;
  std::size_t cache_ways = 2;
  memory::CacheMode l2_cache_mode = memory::CacheMode::Off;
  std::size_t l2_cache_sets = 32;
  std::size_t l2_cache_line_words = 4;
  std::size_t l2_cache_ways = 4;
  std::size_t memory_latency = 1;
  std::size_t cache_hit_latency = 1;
  std::size_t cache_miss_penalty = 6;
  std::size_t l2_cache_hit_latency = 4;
  std::size_t l2_cache_miss_penalty = 16;
  bool io_demo = false;
  bool interrupt_demo = false;
  bool dma_demo = false;
};

struct RunResult {
  bool success = false;
  std::int32_t exit_code = 0;
  std::string error;
  std::size_t retired_instructions = 0;
  std::size_t cycles = 0;
  std::size_t stall_cycles = 0;
  std::size_t load_use_stalls = 0;
  std::size_t flushes = 0;
  std::size_t forwarding_events = 0;
  std::size_t branch_predictions = 0;
  std::size_t branch_mispredictions = 0;
  std::size_t memory_accesses = 0;
  std::size_t memory_reads = 0;
  std::size_t memory_writes = 0;
  std::size_t cache_hits = 0;
  std::size_t cache_misses = 0;
  std::size_t l1_hits = 0;
  std::size_t l1_misses = 0;
  std::size_t l2_hits = 0;
  std::size_t l2_misses = 0;
  std::size_t io_reads = 0;
  std::size_t io_writes = 0;
  std::size_t interrupts_handled = 0;
  std::size_t dma_words_copied = 0;
  memory::CacheMode cache_mode = memory::CacheMode::Off;
  std::vector<std::string> trace_lines;
  std::vector<std::string> timeline_lines;
  std::vector<std::string> system_lines;
  std::array<std::int32_t, 32> registers{};
};

RunResult run_program(const mips::loader::LoadedProgram& program, const RunOptions& options = {});

}  // namespace nexus::sim::pipeline
