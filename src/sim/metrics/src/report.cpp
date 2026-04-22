#include "nexus/sim/metrics/report.hpp"

#include <iomanip>
#include <sstream>

namespace nexus::sim::metrics {

std::string format_summary(const ExecutionSummary& summary) {
  std::ostringstream output;
  output << "Mode: " << summary.mode << '\n';
  if (summary.cores.has_value()) {
    output << "Cores: " << *summary.cores << '\n';
  }
  if (summary.control.has_value()) {
    output << "Control: " << *summary.control << '\n';
  }
  if (summary.scheduler.has_value()) {
    output << "Scheduler: " << *summary.scheduler << '\n';
  }
  if (summary.predictor.has_value()) {
    output << "Predictor: " << *summary.predictor << '\n';
  }
  if (summary.cache.has_value()) {
    output << "Cache: " << *summary.cache << '\n';
  }
  if (summary.coherence.has_value()) {
    output << "Coherence: " << *summary.coherence << '\n';
  }
  if (summary.consistency.has_value()) {
    output << "Consistency: " << *summary.consistency << '\n';
  }
  if (summary.interconnect.has_value()) {
    output << "Interconnect: " << *summary.interconnect << '\n';
  }
  if (summary.issue_width.has_value()) {
    output << "Issue width: " << *summary.issue_width << '\n';
  }
  output << "Program exited with code " << summary.exit_code << '\n';
  output << "Instructions: " << summary.instructions << '\n';
  output << "Cycles: " << summary.cycles << '\n';
  if (summary.cpi.has_value()) {
    output << std::fixed << std::setprecision(2);
    output << "CPI: " << *summary.cpi << '\n';
  }
  if (summary.ipc.has_value()) {
    output << std::fixed << std::setprecision(2);
    output << "IPC: " << *summary.ipc << '\n';
  }
  if (summary.stalls.has_value()) {
    output << "Stalls: " << *summary.stalls << '\n';
  }
  if (summary.load_use_stalls.has_value()) {
    output << "Load-use stalls: " << *summary.load_use_stalls << '\n';
  }
  if (summary.flushes.has_value()) {
    output << "Flushes: " << *summary.flushes << '\n';
  }
  if (summary.forwardings.has_value()) {
    output << "Forwardings: " << *summary.forwardings << '\n';
  }
  if (summary.branch_predictions.has_value()) {
    output << "Branch predictions: " << *summary.branch_predictions << '\n';
  }
  if (summary.branch_mispredictions.has_value()) {
    output << "Branch mispredictions: " << *summary.branch_mispredictions << '\n';
  }
  if (summary.speculative_flush_cycles.has_value()) {
    output << "Speculative flush cycles: " << *summary.speculative_flush_cycles << '\n';
  }
  if (summary.slot_utilization.has_value()) {
    output << std::fixed << std::setprecision(2);
    output << "Slot utilization: " << (*summary.slot_utilization * 100.0) << "%\n";
  }
  if (summary.memory_accesses.has_value()) {
    output << "Memory accesses: " << *summary.memory_accesses << '\n';
  }
  if (summary.memory_reads.has_value()) {
    output << "Memory reads: " << *summary.memory_reads << '\n';
  }
  if (summary.memory_writes.has_value()) {
    output << "Memory writes: " << *summary.memory_writes << '\n';
  }
  if (summary.cache_hits.has_value()) {
    output << "Cache hits: " << *summary.cache_hits << '\n';
  }
  if (summary.cache_misses.has_value()) {
    output << "Cache misses: " << *summary.cache_misses << '\n';
  }
  if (summary.cache_miss_rate.has_value()) {
    output << std::fixed << std::setprecision(2);
    output << "Cache miss rate: " << (*summary.cache_miss_rate * 100.0) << "%\n";
  }
  if (summary.l1_hits.has_value()) {
    output << "L1 hits: " << *summary.l1_hits << '\n';
  }
  if (summary.l1_misses.has_value()) {
    output << "L1 misses: " << *summary.l1_misses << '\n';
  }
  if (summary.l2_hits.has_value()) {
    output << "L2 hits: " << *summary.l2_hits << '\n';
  }
  if (summary.l2_misses.has_value()) {
    output << "L2 misses: " << *summary.l2_misses << '\n';
  }
  if (summary.l2_miss_rate.has_value()) {
    output << std::fixed << std::setprecision(2);
    output << "L2 miss rate: " << (*summary.l2_miss_rate * 100.0) << "%\n";
  }
  if (summary.io_reads.has_value()) {
    output << "I/O reads: " << *summary.io_reads << '\n';
  }
  if (summary.io_writes.has_value()) {
    output << "I/O writes: " << *summary.io_writes << '\n';
  }
  if (summary.interrupts_handled.has_value()) {
    output << "Interrupts handled: " << *summary.interrupts_handled << '\n';
  }
  if (summary.dma_words_copied.has_value()) {
    output << "DMA words copied: " << *summary.dma_words_copied << '\n';
  }
  if (summary.coherence_events.has_value()) {
    output << "Coherence events: " << *summary.coherence_events << '\n';
  }
  if (summary.invalidations.has_value()) {
    output << "Invalidations: " << *summary.invalidations << '\n';
  }
  if (summary.directory_lookups.has_value()) {
    output << "Directory lookups: " << *summary.directory_lookups << '\n';
  }
  if (summary.synchronization_events.has_value()) {
    output << "Synchronization events: " << *summary.synchronization_events << '\n';
  }
  if (summary.lock_acquisitions.has_value()) {
    output << "Lock acquisitions: " << *summary.lock_acquisitions << '\n';
  }
  if (summary.lock_contentions.has_value()) {
    output << "Lock contentions: " << *summary.lock_contentions << '\n';
  }
  if (summary.barrier_wait_cycles.has_value()) {
    output << "Barrier wait cycles: " << *summary.barrier_wait_cycles << '\n';
  }
  if (summary.atomic_operations.has_value()) {
    output << "Atomic operations: " << *summary.atomic_operations << '\n';
  }
  if (summary.store_buffer_flushes.has_value()) {
    output << "Store-buffer flushes: " << *summary.store_buffer_flushes << '\n';
  }
  if (summary.interconnect_messages.has_value()) {
    output << "Interconnect messages: " << *summary.interconnect_messages << '\n';
  }
  if (summary.interconnect_cycles.has_value()) {
    output << "Interconnect cycles: " << *summary.interconnect_cycles << '\n';
  }
  return output.str();
}

}  // namespace nexus::sim::metrics
