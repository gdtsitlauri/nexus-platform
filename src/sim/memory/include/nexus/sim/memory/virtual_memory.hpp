#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nexus::sim::memory {

// Virtual memory down to secondary storage (EY321 (f): the memory system from multi-level caches to main and
// secondary memory).  A trace of virtual byte addresses goes through a fully associative TLB (LRU), a page table
// and a main memory of `frames` page frames backed by a disk; a page fault reads the page from disk and evicts a
// victim chosen by FIFO, LRU, Clock (second chance) or OPT (Belady, needs the whole trace), writing it back to
// disk when it is dirty.

enum class PagePolicy { Fifo, Lru, Clock, Opt };

struct VmConfig {
  std::size_t page_bytes = 4096;
  std::size_t frames = 8;
  std::size_t tlb_entries = 16;
  PagePolicy policy = PagePolicy::Lru;
  std::size_t tlb_hit_cycles = 1;
  std::size_t page_walk_cycles = 20;     // TLB miss: page-table walk in main memory
  std::size_t disk_cycles = 100000;      // page fault: one page from or to the disk
};

struct VmAccess {
  std::uint64_t address = 0;
  bool write = false;
};

struct VmStats {
  std::size_t accesses = 0;
  std::size_t tlb_hits = 0;
  std::size_t tlb_misses = 0;
  std::size_t page_faults = 0;
  std::size_t write_backs = 0;      // dirty pages written to disk on eviction
  std::size_t distinct_pages = 0;
  std::size_t cycles = 0;
  [[nodiscard]] double average_access_cycles() const {
    return accesses == 0 ? 0.0 : static_cast<double>(cycles) / static_cast<double>(accesses);
  }
};

VmStats simulate_virtual_memory(const std::vector<VmAccess>& trace, const VmConfig& config);
// Page faults of a page-reference string (page numbers) with `frames` frames, without TLB or timing.
std::size_t count_page_faults(const std::vector<std::uint64_t>& pages, std::size_t frames, PagePolicy policy);
std::string page_policy_name(PagePolicy policy);

}  // namespace nexus::sim::memory
