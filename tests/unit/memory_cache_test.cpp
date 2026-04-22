#include <cmath>
#include <iostream>

#include "nexus/sim/memory/system.hpp"

int main() {
  using nexus::sim::memory::CacheConfig;
  using nexus::sim::memory::CacheMode;
  using nexus::sim::memory::Config;
  using nexus::sim::memory::System;

  System direct({
      .memory_words = 32,
      .flat_latency = 1,
      .cache =
          CacheConfig{
              .mode = CacheMode::DirectMapped,
              .sets = 1,
              .line_words = 1,
              .ways = 1,
              .hit_latency = 1,
              .miss_penalty = 4,
          },
  });

  const auto direct_w0 = direct.write_word(0, 7, 1);
  const auto direct_w4 = direct.write_word(4, 9, 2);
  const auto direct_r0 = direct.read_word(0, 3);
  if (!direct_w0.success || !direct_w4.success || !direct_r0.success || direct_r0.value != 7 ||
      direct_w0.latency_cycles != 5U || direct_r0.cache_hit) {
    std::cerr << "Direct-mapped cache access behavior is incorrect.\n";
    return 1;
  }

  const auto direct_stats = direct.statistics();
  if (direct_stats.accesses != 3U || direct_stats.cache_hits != 0U || direct_stats.cache_misses != 3U ||
      direct_stats.l1_hits != 0U || direct_stats.l1_misses != 3U ||
      direct_stats.l2_hits != 0U || direct_stats.l2_misses != 0U) {
    std::cerr << "Direct-mapped cache accounting is incorrect.\n";
    return 1;
  }

  System assoc({
      .memory_words = 32,
      .flat_latency = 1,
      .cache =
          CacheConfig{
              .mode = CacheMode::SetAssociative,
              .sets = 1,
              .line_words = 1,
              .ways = 2,
              .hit_latency = 1,
              .miss_penalty = 4,
          },
  });

  const auto assoc_w0 = assoc.write_word(0, 7, 1);
  const auto assoc_w4 = assoc.write_word(4, 9, 2);
  const auto assoc_r0 = assoc.read_word(0, 3);
  if (!assoc_w0.success || !assoc_w4.success || !assoc_r0.success || assoc_r0.value != 7 ||
      !assoc_r0.cache_hit || assoc_r0.latency_cycles != 1U) {
    std::cerr << "Set-associative cache should retain both lines and hit on the third access.\n";
    return 1;
  }

  const auto assoc_stats = assoc.statistics();
  if (assoc_stats.accesses != 3U || assoc_stats.cache_hits != 1U || assoc_stats.cache_misses != 2U ||
      assoc_stats.l1_hits != 1U || assoc_stats.l1_misses != 2U) {
    std::cerr << "Set-associative cache accounting is incorrect.\n";
    return 1;
  }

  const double miss_rate = nexus::sim::memory::cache_miss_rate(assoc_stats);
  if (std::fabs(miss_rate - (2.0 / 3.0)) > 1e-9) {
    std::cerr << "Unexpected cache miss-rate computation.\n";
    return 1;
  }

  System hierarchy({
      .memory_words = 32,
      .flat_latency = 1,
      .cache =
          CacheConfig{
              .mode = CacheMode::DirectMapped,
              .sets = 1,
              .line_words = 1,
              .ways = 1,
              .hit_latency = 1,
              .miss_penalty = 4,
          },
      .l2_cache =
          CacheConfig{
              .mode = CacheMode::SetAssociative,
              .sets = 1,
              .line_words = 1,
              .ways = 2,
              .hit_latency = 3,
              .miss_penalty = 9,
          },
  });

  const auto l1_w0 = hierarchy.write_word(0, 7, 1);
  const auto l1_w4 = hierarchy.write_word(4, 9, 2);
  const auto l1_r0 = hierarchy.read_word(0, 3);
  if (!l1_w0.success || !l1_w4.success || !l1_r0.success || l1_r0.value != 7 || l1_r0.l1_hit ||
      !l1_r0.l2_hit || l1_r0.latency_cycles != 4U) {
    std::cerr << "Expected the bounded hierarchy to miss in L1 and hit in L2 on the third access.\n";
    return 1;
  }

  const auto hierarchy_stats = hierarchy.statistics();
  if (hierarchy_stats.l1_hits != 0U || hierarchy_stats.l1_misses != 3U ||
      hierarchy_stats.l2_hits != 1U || hierarchy_stats.l2_misses != 2U) {
    std::cerr << "Multi-level cache accounting is incorrect.\n";
    return 1;
  }

  return 0;
}
