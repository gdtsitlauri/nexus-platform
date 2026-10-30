#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "nexus/mips/loader/parser.hpp"

namespace nexus::sim::advanced {

// Multithreaded processors (NEY704 (c): parallel architectures with multiple threads and simultaneous
// multithreading).  Each thread is a MIPS program; its executed instructions (with the addresses of its loads
// and stores, from the functional simulator) drive an in-order core with a shared direct-mapped cache:
//   * single  one thread at a time, to completion (the baseline);
//   * coarse  switch threads only on a cache miss, paying `switch_penalty` cycles to refill the pipeline;
//   * fine    pick a different ready thread every cycle (round robin), no switch cost;
//   * smt     issue up to `issue_width` instructions per cycle from different ready threads.
// Latencies: ALU 1, mult 4, div 12, loads/stores 1 on a hit and 1 + miss_penalty on a miss.

enum class MtPolicy { Single, Coarse, Fine, Smt };

struct MtOptions {
  MtPolicy policy = MtPolicy::Fine;
  std::size_t issue_width = 2;     // smt only
  std::size_t miss_penalty = 20;
  std::size_t switch_penalty = 3;  // coarse only
  std::size_t cache_lines = 64;
  std::size_t line_words = 4;
  std::size_t max_instructions = 2'000'000;
};

struct MtThread {
  std::int32_t exit_code = 0;
  std::size_t instructions = 0;
  std::size_t misses = 0;
  std::size_t finish_cycle = 0;
};

struct MtResult {
  bool success = false;
  std::string error;
  std::size_t cycles = 0;
  std::size_t issued = 0;
  std::size_t switches = 0;
  std::size_t cache_hits = 0;
  std::size_t cache_misses = 0;
  std::vector<MtThread> threads;
  [[nodiscard]] double utilization(std::size_t width) const {
    return cycles == 0 ? 0.0 : static_cast<double>(issued) / static_cast<double>(cycles * width);
  }
};

MtResult run_multithreaded(const std::vector<const mips::loader::LoadedProgram*>& programs, const MtOptions& options);
std::string policy_name(MtPolicy policy);

}  // namespace nexus::sim::advanced
