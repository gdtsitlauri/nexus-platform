#include "nexus/sim/memory/nonblocking_cache.hpp"

#include <algorithm>
#include <map>
#include <vector>

namespace nexus::sim::memory {

namespace {

struct Line {
  std::int64_t block = -1;
  std::size_t ready = 0;      // cycle at which the data arrives
  std::size_t last_use = 0;
  bool prefetched = false;    // brought by a prefetch and not yet used by a demand access
};

struct StrideEntry {
  std::int64_t last = -1;
  std::int64_t stride = 0;
  int confidence = 0;
};

}  // namespace

std::string prefetcher_name(Prefetcher prefetcher) {
  switch (prefetcher) {
    case Prefetcher::None: return "none";
    case Prefetcher::NextLine: return "next-line";
    case Prefetcher::Stride: return "stride";
  }
  return "?";
}

NbStats simulate_nonblocking_cache(const std::vector<CacheTraceEntry>& trace, const NbConfig& config) {
  NbStats stats;
  const std::size_t sets = std::max<std::size_t>(config.sets, 1);
  const std::size_t ways = std::max<std::size_t>(config.ways, 1);
  const auto line_bytes = static_cast<std::int64_t>(std::max<std::size_t>(config.line_bytes, 1));
  std::vector<std::vector<Line>> cache(sets, std::vector<Line>(ways));
  std::vector<std::size_t> mshr_free_at;  // completion cycles of outstanding demand misses
  std::map<std::size_t, StrideEntry> stride_table;
  std::size_t t = 0;

  auto find = [&](std::int64_t block) -> Line* {
    auto& set = cache[static_cast<std::size_t>(block) % sets];
    for (auto& line : set) {
      if (line.block == block) return &line;
    }
    return nullptr;
  };
  auto install = [&](std::int64_t block, std::size_t ready, bool prefetched) {
    auto& set = cache[static_cast<std::size_t>(block) % sets];
    auto victim = std::min_element(set.begin(), set.end(), [](const Line& a, const Line& b) {
      if ((a.block < 0) != (b.block < 0)) return a.block < 0;
      return a.last_use < b.last_use;
    });
    *victim = Line{.block = block, .ready = ready, .last_use = t, .prefetched = prefetched};
  };
  auto retire_mshrs = [&] {
    mshr_free_at.erase(std::remove_if(mshr_free_at.begin(), mshr_free_at.end(), [&](std::size_t done) { return done <= t; }),
                       mshr_free_at.end());
  };
  auto prefetch = [&](std::int64_t block) {
    if (block < 0 || find(block) != nullptr) return;
    if (config.mshrs > 0) {
      retire_mshrs();
      if (mshr_free_at.size() >= config.mshrs) return;  // no MSHR left: drop the prefetch
      mshr_free_at.push_back(t + config.miss_penalty);
    }
    install(block, t + config.miss_penalty, true);
    ++stats.prefetches;
  };

  for (const auto& entry : trace) {
    ++stats.instructions;
    if (entry.address < 0) {
      ++t;
      continue;
    }
    ++stats.accesses;
    const std::int64_t block = entry.address / line_bytes;
    Line* line = find(block);
    bool miss = false;
    if (line != nullptr && line->ready <= t) {
      ++stats.hits;
      ++t;
    } else if (line != nullptr) {
      // the line is on its way (a demand miss or a prefetch in flight)
      ++stats.merged;
      if (config.mshrs == 0) {
        stats.stall_cycles += line->ready - t;
        t = line->ready;
      }
      ++t;
    } else {
      miss = true;
      ++stats.misses;
      if (config.mshrs == 0) {
        install(block, t + config.miss_penalty, false);
        stats.stall_cycles += config.miss_penalty;
        t += config.miss_penalty + 1;
      } else {
        retire_mshrs();
        if (mshr_free_at.size() >= config.mshrs) {
          const auto earliest = *std::min_element(mshr_free_at.begin(), mshr_free_at.end());
          stats.stall_cycles += earliest - t;
          t = earliest;
          retire_mshrs();
        }
        mshr_free_at.push_back(t + config.miss_penalty);
        install(block, t + config.miss_penalty, false);
        ++t;
      }
      line = find(block);
    }
    if (line != nullptr) {
      if (line->prefetched) {
        ++stats.useful_prefetches;
        line->prefetched = false;
      }
      line->last_use = t;
    }
    if (config.prefetcher == Prefetcher::NextLine && miss) {
      prefetch(block + 1);
    } else if (config.prefetcher == Prefetcher::Stride) {
      auto& s = stride_table[entry.pc];
      if (s.last >= 0) {
        const auto stride = entry.address - s.last;
        if (stride == s.stride && stride != 0) {
          s.confidence = std::min(s.confidence + 1, 3);
        } else {
          s.stride = stride;
          s.confidence = 0;
        }
        if (s.confidence >= 1) prefetch((entry.address + s.stride) / line_bytes);
      }
      s.last = entry.address;
    }
  }
  stats.cycles = t;
  return stats;
}

}  // namespace nexus::sim::memory
