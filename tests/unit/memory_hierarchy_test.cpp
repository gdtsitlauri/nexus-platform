// Virtual memory with secondary storage (EY321) and non-blocking caches with prefetching (NEY606).
#include <algorithm>
#include <iostream>
#include <list>
#include <random>
#include <string>
#include <vector>

#include "nexus/sim/memory/nonblocking_cache.hpp"
#include "nexus/sim/memory/virtual_memory.hpp"

namespace mem = nexus::sim::memory;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::size_t reference_lru(const std::vector<std::uint64_t>& pages, std::size_t frames) {
  std::list<std::uint64_t> stack;  // most recent first
  std::size_t faults = 0;
  for (const auto p : pages) {
    const auto it = std::find(stack.begin(), stack.end(), p);
    if (it == stack.end()) {
      ++faults;
      if (stack.size() == frames) stack.pop_back();
    } else {
      stack.erase(it);
    }
    stack.push_front(p);
  }
  return faults;
}

void virtual_memory() {
  using P = mem::PagePolicy;
  const std::vector<std::uint64_t> belady{1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
  expect(mem::count_page_faults(belady, 3, P::Fifo) == 9 && mem::count_page_faults(belady, 4, P::Fifo) == 10,
         "Belady's anomaly: FIFO has 9 faults with 3 frames and 10 with 4");
  expect(mem::count_page_faults(belady, 3, P::Lru) == 10 && mem::count_page_faults(belady, 4, P::Lru) == 8, "LRU 10 / 8");
  expect(mem::count_page_faults(belady, 3, P::Opt) == 7 && mem::count_page_faults(belady, 4, P::Opt) == 6, "OPT 7 / 6");

  std::mt19937 rng(23);
  for (int round = 0; round < 300; ++round) {
    std::vector<std::uint64_t> pages(20 + rng() % 60);
    for (auto& p : pages) p = rng() % (3 + rng() % 9);
    const std::size_t frames = 1 + rng() % 5;
    const auto opt = mem::count_page_faults(pages, frames, P::Opt);
    const auto lru = mem::count_page_faults(pages, frames, P::Lru);
    expect(lru == reference_lru(pages, frames), "LRU agrees with a reference stack implementation");
    expect(opt <= lru && opt <= mem::count_page_faults(pages, frames, P::Fifo) &&
               opt <= mem::count_page_faults(pages, frames, P::Clock),
           "OPT is a lower bound");
    expect(mem::count_page_faults(pages, frames + 1, P::Lru) <= lru &&
               mem::count_page_faults(pages, frames + 1, P::Opt) <= opt,
           "stack algorithms never fault more with more frames");
  }

  // Writing 12 pages sequentially with 4 frames: every page faults once, 8 dirty pages go back to disk, the TLB
  // misses once per page, and the cycle count is the sum of the costs.
  mem::VmConfig config;
  config.page_bytes = 4096;
  config.frames = 4;
  config.tlb_entries = 8;
  std::vector<mem::VmAccess> trace;
  for (std::uint64_t a = 0; a < 12 * 4096; a += 4) trace.push_back({a, true});
  const auto s = mem::simulate_virtual_memory(trace, config);
  expect(s.page_faults == 12 && s.write_backs == 8 && s.tlb_misses == 12 && s.distinct_pages == 12,
         "sequential writes: 12 faults, 8 write-backs, 12 TLB misses");
  expect(s.cycles == s.accesses * config.tlb_hit_cycles + s.tlb_misses * config.page_walk_cycles +
                         (s.page_faults + s.write_backs) * config.disk_cycles,
         "cycles = TLB hits + page walks + disk transfers");
}

std::vector<mem::CacheTraceEntry> stream(std::size_t elements, std::int64_t stride, std::size_t work) {
  std::vector<mem::CacheTraceEntry> trace;
  for (std::size_t i = 0; i < elements; ++i) {
    trace.push_back({10, static_cast<std::int64_t>(i) * stride});
    for (std::size_t k = 0; k < work; ++k) trace.push_back({11 + k, -1});
  }
  return trace;
}

mem::NbStats run(const std::vector<mem::CacheTraceEntry>& trace, std::size_t mshrs, mem::Prefetcher prefetcher) {
  mem::NbConfig config;
  config.mshrs = mshrs;
  config.prefetcher = prefetcher;
  return mem::simulate_nonblocking_cache(trace, config);
}

void caches() {
  using F = mem::Prefetcher;
  const auto trace = stream(1000, 64, 3);  // one new line per element, three instructions of work in between
  const auto blocking = run(trace, 0, F::None);
  expect(blocking.misses == 1000 && blocking.hits + blocking.misses + blocking.merged == blocking.accesses,
         "blocking cache: every element misses");
  expect(blocking.cycles == blocking.instructions + blocking.misses * 40, "blocking cycles = instructions + misses x penalty");

  const auto next = run(trace, 0, F::NextLine);
  expect(next.misses == 500 && next.useful_prefetches == 500, "next-line prefetching halves the misses of a stream");
  expect(next.cycles < blocking.cycles, "next-line prefetching saves cycles");
  const auto stride = run(trace, 0, F::Stride);
  expect(stride.misses <= 3 && stride.cycles < next.cycles, "the stride prefetcher removes almost every miss");

  std::size_t previous = blocking.cycles;
  for (const std::size_t k : {1, 2, 4, 8}) {
    const auto r = run(trace, k, F::None);
    expect(r.cycles <= previous, "more MSHRs never cost cycles (" + std::to_string(k) + ")");
    expect(r.hits + r.misses + r.merged == r.accesses && r.instructions == blocking.instructions, "accounting");
    previous = r.cycles;
  }
  expect(run(trace, 8, F::None).cycles * 4 < blocking.cycles, "8 MSHRs overlap misses: more than 4x fewer cycles");

  // A small working set hits after the first pass, whatever the configuration.
  std::vector<mem::CacheTraceEntry> loop;
  for (int pass = 0; pass < 10; ++pass) {
    for (int i = 0; i < 32; ++i) loop.push_back({5, i * 64});
  }
  expect(run(loop, 0, F::None).misses == 32 && run(loop, 4, F::None).misses == 32, "32-line working set: cold misses only");
}

}  // namespace

int main() {
  virtual_memory();
  caches();
  if (failures == 0) {
    std::cout << "memory hierarchy: page replacement, TLB and disk; non-blocking caches and prefetching OK\n";
  }
  return failures == 0 ? 0 : 1;
}
