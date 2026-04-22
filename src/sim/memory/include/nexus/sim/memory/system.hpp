#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/sim/io/system.hpp"

namespace nexus::sim::memory {

enum class CacheMode {
  Off,
  DirectMapped,
  SetAssociative,
};

std::string_view cache_mode_name(CacheMode mode);

struct CacheConfig {
  CacheMode mode = CacheMode::Off;
  std::size_t sets = 16;
  std::size_t line_words = 4;
  std::size_t ways = 1;
  std::size_t hit_latency = 1;
  std::size_t miss_penalty = 6;
};

struct Config {
  std::size_t memory_words = 1U << 18;
  std::size_t flat_latency = 1;
  CacheConfig cache{};
  std::optional<CacheConfig> l2_cache;
  io::Config io{};
};

struct AccessResult {
  bool success = true;
  std::int32_t value = 0;
  std::size_t latency_cycles = 1;
  std::string error;
  bool from_io = false;
  bool cache_accessed = false;
  bool cache_hit = false;
  bool l1_accessed = false;
  bool l1_hit = false;
  bool l2_accessed = false;
  bool l2_hit = false;
  std::vector<std::string> events;
};

struct Statistics {
  std::size_t accesses = 0;
  std::size_t reads = 0;
  std::size_t writes = 0;
  std::size_t total_latency_cycles = 0;
  std::size_t cache_hits = 0;
  std::size_t cache_misses = 0;
  std::size_t l1_hits = 0;
  std::size_t l1_misses = 0;
  std::size_t l2_hits = 0;
  std::size_t l2_misses = 0;
  std::size_t io_reads = 0;
  std::size_t io_writes = 0;
  std::size_t console_writes = 0;
  std::size_t dma_words = 0;
  std::size_t dma_completions = 0;
};

double cache_miss_rate(const Statistics& statistics);

class System {
 public:
  explicit System(const Config& config = {});

  AccessResult read_word(std::int32_t address, std::size_t cycle = 0);
  AccessResult write_word(std::int32_t address, std::int32_t value, std::size_t cycle = 0);

  io::TickResult tick(std::size_t cycle);

  bool direct_read_word(std::int32_t address, std::int32_t& value);
  bool direct_write_word(std::int32_t address, std::int32_t value);

  const Statistics& statistics() const;
  io::System& io_system();

  struct CacheLine {
    bool valid = false;
    std::size_t tag = 0;
    std::size_t last_used = 0;
    std::vector<std::int32_t> words;
  };

  struct CacheLevelState {
    CacheConfig config{};
    std::vector<std::vector<CacheLine>> sets;
  };

 private:

  AccessResult access(bool write, std::int32_t address, std::int32_t* value, std::size_t cycle);
  bool validate_word_address(std::int32_t address, std::size_t& index, std::string& error) const;

  Config config_;
  Statistics statistics_;
  std::vector<std::int32_t> memory_;
  io::System io_;
  std::optional<CacheLevelState> l1_cache_;
  std::optional<CacheLevelState> l2_cache_;
  std::size_t use_clock_ = 0;
};

}  // namespace nexus::sim::memory
