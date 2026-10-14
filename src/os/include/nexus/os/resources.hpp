#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Deadlock, memory management and virtual memory (EY517).
namespace nexus::os {

// ---------------------------------------------------------------- deadlock

using Matrix = std::vector<std::vector<int>>;

struct BankerState {
  std::vector<int> available;
  Matrix max;
  Matrix allocation;
  Matrix need() const;
};

// Safety algorithm: a safe sequence of process indices, or nothing when the state is unsafe.
std::optional<std::vector<std::size_t>> safe_sequence(const BankerState& s);
// Resource-request algorithm: grants the request (updating the state) only if the resulting state is safe.
bool request_resources(BankerState& s, std::size_t process, const std::vector<int>& request, std::string* reason = nullptr);
// Exhaustive check over all process orders (for testing the safety algorithm; processes <= 8).
bool safe_by_enumeration(const BankerState& s);

// Deadlock detection with current requests: the processes that can never finish (deadlocked set).
std::vector<std::size_t> detect_deadlock(const std::vector<int>& available, const Matrix& allocation,
                                         const Matrix& request);

// Single-instance resources: a cycle in the wait-for graph (edge i -> j: process i waits for j) means deadlock.
std::optional<std::vector<std::size_t>> wait_for_cycle(const std::vector<std::vector<std::size_t>>& waits_for);

// ---------------------------------------------------------------- contiguous allocation

enum class Fit { First, Next, Best, Worst };

class PartitionAllocator {
 public:
  PartitionAllocator(std::size_t memory, Fit fit);
  std::optional<std::size_t> allocate(std::size_t size, int owner);  // start address
  void release(int owner);
  void compact();  // move every allocated block to the low end
  std::size_t free_total() const;
  std::size_t largest_hole() const;
  std::size_t holes() const;
  double external_fragmentation() const;  // 1 - largest hole / free memory
  struct Block {
    std::size_t start, size;
    int owner;  // -1 = hole
  };
  const std::vector<Block>& blocks() const { return blocks_; }

 private:
  std::size_t memory_;
  Fit fit_;
  std::size_t next_start_ = 0;
  std::vector<Block> blocks_;
};

class BuddyAllocator {
 public:
  explicit BuddyAllocator(std::size_t order);  // memory of 2^order units
  std::optional<std::size_t> allocate(std::size_t size);
  void release(std::size_t address);
  std::size_t free_units() const;
  bool check_invariants() const;  // free blocks aligned, disjoint, no two free buddies left uncoalesced
  std::size_t internal_fragmentation() const;

 private:
  std::size_t order_;
  std::vector<std::vector<std::size_t>> free_;  // per order: free block addresses
  std::vector<std::pair<std::size_t, std::size_t>> used_;  // address, order
  std::vector<std::pair<std::size_t, std::size_t>> requested_;  // address, requested size
};

// ---------------------------------------------------------------- paging

struct PageTableEntry {
  bool present = false;
  std::uint32_t frame = 0;
};

// Two-level page table for 32-bit virtual addresses (10-bit directory index, 10-bit table index, 12-bit offset)
// with a fully associative LRU TLB.
class TwoLevelPaging {
 public:
  explicit TwoLevelPaging(std::size_t tlb_entries);
  void map(std::uint32_t virtual_page, std::uint32_t frame);
  // Translation; nothing on a page fault.
  std::optional<std::uint32_t> translate(std::uint32_t virtual_address);
  std::size_t tlb_hits() const { return hits_; }
  std::size_t tlb_misses() const { return misses_; }
  std::size_t page_faults() const { return faults_; }
  std::size_t page_table_bytes() const;  // directory plus allocated second-level tables
  // Effective access time with memory access m, TLB lookup t and the measured hit ratio (two-level walk).
  double effective_access_time(double memory_ns, double tlb_ns) const;

 private:
  std::vector<std::vector<PageTableEntry>> tables_;  // 1024 directory slots, tables allocated on demand
  std::size_t tlb_capacity_;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> tlb_;  // (virtual page, frame), most recent last
  std::size_t hits_ = 0, misses_ = 0, faults_ = 0;
};

// ---------------------------------------------------------------- page replacement

enum class Replacement { FIFO, LRU, OPT, Clock };
const char* replacement_name(Replacement r);

struct ReplacementTrace {
  std::size_t faults = 0;
  std::vector<std::vector<int>> frames_after;  // frame contents after every reference (-1 = empty)
  std::vector<bool> fault;
};

ReplacementTrace simulate_replacement(const std::vector<int>& refs, std::size_t frames, Replacement r);
// Working-set size W(t, delta) for every t.
std::vector<std::size_t> working_set_sizes(const std::vector<int>& refs, std::size_t delta);

}  // namespace nexus::os
