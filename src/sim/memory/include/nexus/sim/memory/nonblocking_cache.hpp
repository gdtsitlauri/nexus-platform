#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nexus::sim::memory {

// Advanced memory organisation for high-performance processors (NEY606 (g)): a data cache that keeps working
// under misses and hides latency with prefetching.  Trace-driven: every instruction takes one cycle; loads and
// stores look up a set-associative LRU cache.
//   * blocking (mshrs = 0): every miss stalls the processor for the whole miss penalty;
//   * non-blocking (mshrs = K): up to K misses are outstanding in miss status holding registers; a second miss to
//     a line that is already being fetched merges into its MSHR; the processor stalls only when all MSHRs are busy
//     (loads are assumed not to be used immediately, so this measures memory-level parallelism);
//   * prefetching: next-line (on a miss to line L fetch L+1) or stride (a per-instruction table that detects a
//     constant stride and fetches the next address once the stride repeats).

enum class Prefetcher { None, NextLine, Stride };

struct CacheTraceEntry {
  std::size_t pc = 0;
  std::int64_t address = -1;  // -1: not a memory instruction
};

struct NbConfig {
  std::size_t sets = 64;
  std::size_t ways = 2;
  std::size_t line_bytes = 64;
  std::size_t miss_penalty = 40;
  std::size_t mshrs = 0;
  Prefetcher prefetcher = Prefetcher::None;
};

struct NbStats {
  std::size_t instructions = 0;
  std::size_t accesses = 0;
  std::size_t hits = 0;
  std::size_t misses = 0;           // demand misses that started a memory request
  std::size_t merged = 0;           // demand accesses to a line already in flight
  std::size_t prefetches = 0;
  std::size_t useful_prefetches = 0;
  std::size_t stall_cycles = 0;
  std::size_t cycles = 0;
};

NbStats simulate_nonblocking_cache(const std::vector<CacheTraceEntry>& trace, const NbConfig& config);
std::string prefetcher_name(Prefetcher prefetcher);

}  // namespace nexus::sim::memory
