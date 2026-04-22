#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/mips/loader/parser.hpp"

namespace nexus::sim::parallel {

enum class CoherenceKind {
  SnoopingLite,
  DirectoryLite,
};

enum class ConsistencyKind {
  Sequential,
  WeakLite,
};

enum class InterconnectKind {
  Bus,
  Switch,
  NoCLite,
};

std::string_view coherence_name(CoherenceKind kind);
std::string_view consistency_name(ConsistencyKind kind);
std::string_view interconnect_name(InterconnectKind kind);

struct RunOptions {
  bool trace = false;
  std::size_t cores = 2;
  std::size_t max_cycles = 100000;
  std::size_t memory_words = 1U << 18;
  std::size_t memory_latency = 2;
  CoherenceKind coherence = CoherenceKind::SnoopingLite;
  ConsistencyKind consistency = ConsistencyKind::Sequential;
  InterconnectKind interconnect = InterconnectKind::Bus;
};

struct RunResult {
  bool success = false;
  std::int32_t exit_code = 0;
  std::string error;
  std::size_t active_cores = 0;
  std::size_t retired_instructions = 0;
  std::size_t cycles = 0;
  std::size_t memory_accesses = 0;
  std::size_t memory_reads = 0;
  std::size_t memory_writes = 0;
  std::size_t cache_hits = 0;
  std::size_t cache_misses = 0;
  std::size_t coherence_events = 0;
  std::size_t invalidations = 0;
  std::size_t directory_lookups = 0;
  std::size_t synchronization_events = 0;
  std::size_t lock_acquisitions = 0;
  std::size_t lock_contentions = 0;
  std::size_t barrier_arrivals = 0;
  std::size_t barrier_wait_cycles = 0;
  std::size_t atomic_operations = 0;
  std::size_t store_buffer_flushes = 0;
  std::size_t interconnect_messages = 0;
  std::size_t interconnect_cycles = 0;
  std::vector<std::int32_t> core_exit_codes;
  std::vector<std::string> trace_lines;
  std::vector<std::string> system_lines;
};

RunResult run_program(const mips::loader::LoadedProgram& program, const RunOptions& options = {});

}  // namespace nexus::sim::parallel
