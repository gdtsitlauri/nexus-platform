#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace nexus::sim::metrics {

struct ExecutionSummary {
  std::string mode;
  std::optional<std::size_t> cores;
  std::optional<std::string> control;
  std::optional<std::string> scheduler;
  std::optional<std::string> predictor;
  std::optional<std::string> cache;
  std::optional<std::string> coherence;
  std::optional<std::string> consistency;
  std::optional<std::string> interconnect;
  std::optional<std::size_t> issue_width;
  std::int32_t exit_code = 0;
  std::size_t instructions = 0;
  std::size_t cycles = 0;
  std::optional<double> cpi;
  std::optional<double> ipc;
  std::optional<std::size_t> stalls;
  std::optional<std::size_t> load_use_stalls;
  std::optional<std::size_t> flushes;
  std::optional<std::size_t> forwardings;
  std::optional<std::size_t> branch_predictions;
  std::optional<std::size_t> branch_mispredictions;
  std::optional<std::size_t> speculative_flush_cycles;
  std::optional<double> slot_utilization;
  std::optional<std::size_t> memory_accesses;
  std::optional<std::size_t> memory_reads;
  std::optional<std::size_t> memory_writes;
  std::optional<std::size_t> cache_hits;
  std::optional<std::size_t> cache_misses;
  std::optional<double> cache_miss_rate;
  std::optional<std::size_t> l1_hits;
  std::optional<std::size_t> l1_misses;
  std::optional<std::size_t> l2_hits;
  std::optional<std::size_t> l2_misses;
  std::optional<double> l2_miss_rate;
  std::optional<std::size_t> io_reads;
  std::optional<std::size_t> io_writes;
  std::optional<std::size_t> interrupts_handled;
  std::optional<std::size_t> dma_words_copied;
  std::optional<std::size_t> coherence_events;
  std::optional<std::size_t> invalidations;
  std::optional<std::size_t> directory_lookups;
  std::optional<std::size_t> synchronization_events;
  std::optional<std::size_t> lock_acquisitions;
  std::optional<std::size_t> lock_contentions;
  std::optional<std::size_t> barrier_wait_cycles;
  std::optional<std::size_t> atomic_operations;
  std::optional<std::size_t> store_buffer_flushes;
  std::optional<std::size_t> interconnect_messages;
  std::optional<std::size_t> interconnect_cycles;
};

std::string format_summary(const ExecutionSummary& summary);

}  // namespace nexus::sim::metrics
