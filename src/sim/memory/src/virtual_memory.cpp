#include "nexus/sim/memory/virtual_memory.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>

namespace nexus::sim::memory {

namespace {

struct Frame {
  std::uint64_t page = 0;
  bool dirty = false;
  bool referenced = false;
  std::size_t loaded = 0;     // FIFO
  std::size_t last_use = 0;   // LRU
};

}  // namespace

std::string page_policy_name(PagePolicy policy) {
  switch (policy) {
    case PagePolicy::Fifo: return "FIFO";
    case PagePolicy::Lru: return "LRU";
    case PagePolicy::Clock: return "Clock";
    case PagePolicy::Opt: return "OPT";
  }
  return "?";
}

VmStats simulate_virtual_memory(const std::vector<VmAccess>& trace, const VmConfig& config) {
  VmStats stats;
  const std::size_t page_bytes = std::max<std::size_t>(config.page_bytes, 1);
  const std::size_t frames = std::max<std::size_t>(config.frames, 1);
  std::vector<std::uint64_t> pages;
  pages.reserve(trace.size());
  for (const auto& access : trace) pages.push_back(access.address / page_bytes);
  // OPT needs the next use of every reference
  std::vector<std::size_t> next_use(pages.size(), std::numeric_limits<std::size_t>::max());
  if (config.policy == PagePolicy::Opt) {
    std::unordered_map<std::uint64_t, std::size_t> seen;
    for (std::size_t i = pages.size(); i-- > 0;) {
      if (const auto it = seen.find(pages[i]); it != seen.end()) next_use[i] = it->second;
      seen[pages[i]] = i;
    }
  }
  std::vector<Frame> memory;
  std::unordered_map<std::uint64_t, std::size_t> resident;  // page -> frame index
  std::unordered_map<std::uint64_t, std::size_t> upcoming;  // OPT: page -> next use after the current access
  std::vector<std::pair<std::uint64_t, std::size_t>> tlb;    // page, last use
  std::set<std::uint64_t> distinct;
  std::size_t hand = 0;
  for (std::size_t i = 0; i < trace.size(); ++i) {
    const auto page = pages[i];
    ++stats.accesses;
    distinct.insert(page);
    if (config.policy == PagePolicy::Opt) upcoming[page] = next_use[i];
    // TLB
    const auto tlb_it = std::find_if(tlb.begin(), tlb.end(), [&](const auto& e) { return e.first == page; });
    if (tlb_it != tlb.end()) {
      ++stats.tlb_hits;
      tlb_it->second = i;
      stats.cycles += config.tlb_hit_cycles;
    } else {
      ++stats.tlb_misses;
      stats.cycles += config.tlb_hit_cycles + config.page_walk_cycles;
      if (config.tlb_entries > 0) {
        if (tlb.size() >= config.tlb_entries) {
          tlb.erase(std::min_element(tlb.begin(), tlb.end(), [](const auto& a, const auto& b) { return a.second < b.second; }));
        }
        tlb.emplace_back(page, i);
      }
    }
    // page table / main memory
    auto frame_it = resident.find(page);
    if (frame_it == resident.end()) {
      ++stats.page_faults;
      stats.cycles += config.disk_cycles;
      std::size_t slot;
      if (memory.size() < frames) {
        slot = memory.size();
        memory.push_back(Frame{});
      } else {
        switch (config.policy) {
          case PagePolicy::Fifo:
            slot = static_cast<std::size_t>(std::min_element(memory.begin(), memory.end(), [](const Frame& a, const Frame& b) {
                     return a.loaded < b.loaded; }) - memory.begin());
            break;
          case PagePolicy::Lru:
            slot = static_cast<std::size_t>(std::min_element(memory.begin(), memory.end(), [](const Frame& a, const Frame& b) {
                     return a.last_use < b.last_use; }) - memory.begin());
            break;
          case PagePolicy::Clock:
            while (memory[hand].referenced) {
              memory[hand].referenced = false;
              hand = (hand + 1) % frames;
            }
            slot = hand;
            hand = (hand + 1) % frames;
            break;
          case PagePolicy::Opt:
          default:
            slot = static_cast<std::size_t>(std::max_element(memory.begin(), memory.end(), [&](const Frame& a, const Frame& b) {
                     return upcoming[a.page] < upcoming[b.page]; }) - memory.begin());
            break;
        }
        const auto victim = memory[slot].page;
        if (memory[slot].dirty) {
          ++stats.write_backs;
          stats.cycles += config.disk_cycles;
        }
        resident.erase(victim);
        tlb.erase(std::remove_if(tlb.begin(), tlb.end(), [&](const auto& e) { return e.first == victim; }), tlb.end());
      }
      memory[slot] = Frame{.page = page, .dirty = false, .referenced = true, .loaded = i, .last_use = i};
      frame_it = resident.emplace(page, slot).first;
    }
    auto& frame = memory[frame_it->second];
    frame.last_use = i;
    frame.referenced = true;
    frame.dirty = frame.dirty || trace[i].write;
  }
  stats.distinct_pages = distinct.size();
  return stats;
}

std::size_t count_page_faults(const std::vector<std::uint64_t>& pages, std::size_t frames, PagePolicy policy) {
  std::vector<VmAccess> trace;
  for (const auto page : pages) trace.push_back(VmAccess{.address = page, .write = false});
  VmConfig config;
  config.page_bytes = 1;
  config.frames = frames;
  config.tlb_entries = 0;
  config.policy = policy;
  return simulate_virtual_memory(trace, config).page_faults;
}

}  // namespace nexus::sim::memory
