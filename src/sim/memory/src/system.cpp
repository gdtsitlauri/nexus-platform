#include "nexus/sim/memory/system.hpp"

#include <algorithm>
#include <limits>
#include <sstream>

namespace nexus::sim::memory {

namespace {

std::size_t normalized_ways(const CacheConfig& cache) {
  if (cache.mode == CacheMode::Off) {
    return 0;
  }
  if (cache.mode == CacheMode::DirectMapped) {
    return 1;
  }
  return std::max<std::size_t>(2, cache.ways);
}

System::CacheLevelState make_cache_level(CacheConfig config) {
  config.sets = std::max<std::size_t>(1, config.sets);
  config.line_words = std::max<std::size_t>(1, config.line_words);
  config.ways = normalized_ways(config);
  config.hit_latency = std::max<std::size_t>(1, config.hit_latency);

  System::CacheLevelState level;
  level.config = config;
  level.sets.resize(level.config.sets);
  for (auto& set : level.sets) {
    set.resize(level.config.ways);
    for (auto& line : set) {
      line.words.assign(level.config.line_words, 0);
    }
  }
  return level;
}

struct CacheAddress {
  std::size_t block_number = 0;
  std::size_t set_index = 0;
  std::size_t tag = 0;
  std::size_t block_base = 0;
  std::size_t offset = 0;
};

CacheAddress decode_cache_address(const CacheConfig& config, std::size_t word_index) {
  const std::size_t block_number = word_index / config.line_words;
  return CacheAddress{
      .block_number = block_number,
      .set_index = block_number % config.sets,
      .tag = block_number / config.sets,
      .block_base = block_number * config.line_words,
      .offset = word_index % config.line_words,
  };
}

System::CacheLine* find_line(System::CacheLevelState& level, const CacheAddress& address) {
  auto& set = level.sets[address.set_index];
  auto hit = std::find_if(set.begin(), set.end(), [&address](const System::CacheLine& line) {
    return line.valid && line.tag == address.tag;
  });
  return hit == set.end() ? nullptr : &(*hit);
}

System::CacheLine& select_victim(
    System::CacheLevelState& level,
    const CacheAddress& address,
    std::size_t& use_clock) {
  auto& set = level.sets[address.set_index];
  auto victim = std::find_if(set.begin(), set.end(), [](const System::CacheLine& line) { return !line.valid; });
  if (victim == set.end()) {
    victim = std::min_element(
        set.begin(), set.end(), [](const System::CacheLine& lhs, const System::CacheLine& rhs) {
          return lhs.last_used < rhs.last_used;
        });
  }
  victim->valid = true;
  victim->tag = address.tag;
  victim->last_used = ++use_clock;
  return *victim;
}

void populate_from_memory(
    System::CacheLevelState& level,
    System::CacheLine& line,
    const CacheAddress& address,
    const std::vector<std::int32_t>& memory) {
  for (std::size_t word = 0; word < level.config.line_words; ++word) {
    const std::size_t backing_index = address.block_base + word;
    line.words[word] = backing_index < memory.size() ? memory[backing_index] : 0;
  }
}

void update_if_present(
    std::optional<System::CacheLevelState>& level,
    std::size_t word_index,
    std::int32_t value,
    std::size_t& use_clock) {
  if (!level.has_value()) {
    return;
  }
  const auto address = decode_cache_address(level->config, word_index);
  if (auto* line = find_line(*level, address)) {
    line->words[address.offset] = value;
    line->last_used = ++use_clock;
  }
}

}  // namespace

std::string_view cache_mode_name(CacheMode mode) {
  switch (mode) {
    case CacheMode::Off:
      return "off";
    case CacheMode::DirectMapped:
      return "direct-mapped";
    case CacheMode::SetAssociative:
      return "set-associative";
  }

  return "unknown";
}

double cache_miss_rate(const Statistics& statistics) {
  const std::size_t cache_accesses = statistics.cache_hits + statistics.cache_misses;
  if (cache_accesses == 0) {
    return 0.0;
  }
  return static_cast<double>(statistics.cache_misses) / static_cast<double>(cache_accesses);
}

System::System(const Config& config) : config_(config), memory_(config.memory_words, 0), io_(config.io) {
  if (config_.cache.mode != CacheMode::Off) {
    l1_cache_ = make_cache_level(config_.cache);
    config_.cache = l1_cache_->config;
  }

  if (config_.l2_cache.has_value() && config_.l2_cache->mode != CacheMode::Off) {
    l2_cache_ = make_cache_level(*config_.l2_cache);
    config_.l2_cache = l2_cache_->config;
  }
}

AccessResult System::read_word(std::int32_t address, std::size_t cycle) {
  std::int32_t value = 0;
  return access(false, address, &value, cycle);
}

AccessResult System::write_word(std::int32_t address, std::int32_t value, std::size_t cycle) {
  return access(true, address, &value, cycle);
}

io::TickResult System::tick(std::size_t cycle) {
  auto tick_result = io_.tick(
      cycle,
      [this](std::int32_t address) -> std::optional<std::int32_t> {
        std::int32_t value = 0;
        if (!direct_read_word(address, value)) {
          return std::nullopt;
        }
        return value;
      },
      [this](std::int32_t address, std::int32_t value) { return direct_write_word(address, value); });
  statistics_.dma_words += tick_result.dma_words_copied;
  if (tick_result.dma_completed) {
    statistics_.dma_completions += 1;
  }
  return tick_result;
}

bool System::direct_read_word(std::int32_t address, std::int32_t& value) {
  std::size_t index = 0;
  std::string error;
  if (!validate_word_address(address, index, error) || io_.handles(address)) {
    return false;
  }
  value = memory_[index];
  return true;
}

bool System::direct_write_word(std::int32_t address, std::int32_t value) {
  std::size_t index = 0;
  std::string error;
  if (!validate_word_address(address, index, error) || io_.handles(address)) {
    return false;
  }
  memory_[index] = value;
  update_if_present(l1_cache_, index, value, use_clock_);
  update_if_present(l2_cache_, index, value, use_clock_);
  return true;
}

const Statistics& System::statistics() const { return statistics_; }

io::System& System::io_system() { return io_; }

AccessResult System::access(bool write, std::int32_t address, std::int32_t* value, std::size_t cycle) {
  AccessResult result;

  if (io_.handles(address)) {
    const auto io_result =
        write ? io_.write_word(address, *value, cycle) : io_.read_word(address, cycle);
    result.success = io_result.success;
    result.value = io_result.value;
    result.latency_cycles = io_result.latency_cycles;
    result.error = io_result.error;
    result.from_io = true;
    result.events = io_result.events;
    statistics_.total_latency_cycles += result.latency_cycles;
    if (write) {
      ++statistics_.io_writes;
    } else {
      ++statistics_.io_reads;
    }
    statistics_.console_writes = io_.console_write_count();
    return result;
  }

  std::size_t word_index = 0;
  std::string error;
  if (!validate_word_address(address, word_index, error)) {
    result.success = false;
    result.error = error;
    return result;
  }

  ++statistics_.accesses;
  if (write) {
    ++statistics_.writes;
  } else {
    ++statistics_.reads;
  }

  if (!l1_cache_.has_value() && !l2_cache_.has_value()) {
    result.latency_cycles = std::max<std::size_t>(1, config_.flat_latency);
    if (write) {
      memory_[word_index] = *value;
    } else {
      result.value = memory_[word_index];
    }
    statistics_.total_latency_cycles += result.latency_cycles;
    return result;
  }

  const bool primary_is_l1 = l1_cache_.has_value();
  auto& primary = primary_is_l1 ? l1_cache_ : l2_cache_;
  auto& secondary = primary_is_l1 ? l2_cache_ : l1_cache_;
  const std::string primary_name = primary_is_l1 ? "L1" : "L2";
  const std::string secondary_name = primary_is_l1 ? "L2" : "L1";

  const auto primary_address = decode_cache_address(primary->config, word_index);
  result.cache_accessed = primary_is_l1;
  result.l1_accessed = primary_is_l1;
  result.l2_accessed = !primary_is_l1;

  if (auto* primary_line = find_line(*primary, primary_address)) {
    primary_line->last_used = ++use_clock_;
    result.latency_cycles = primary->config.hit_latency;
    if (primary_is_l1) {
      result.cache_hit = true;
      result.l1_hit = true;
      ++statistics_.cache_hits;
      ++statistics_.l1_hits;
    } else {
      result.l2_hit = true;
      ++statistics_.l2_hits;
    }

    if (write) {
      primary_line->words[primary_address.offset] = *value;
      memory_[word_index] = *value;
      update_if_present(secondary, word_index, *value, use_clock_);
    } else {
      result.value = primary_line->words[primary_address.offset];
    }

    statistics_.total_latency_cycles += result.latency_cycles;
    return result;
  }

  if (primary_is_l1) {
    result.cache_hit = false;
    ++statistics_.cache_misses;
    ++statistics_.l1_misses;
  } else {
    ++statistics_.l2_misses;
  }

  result.latency_cycles = primary->config.hit_latency;
  result.events.push_back(primary_name + " miss @" + std::to_string(address));

  System::CacheLine* secondary_line = nullptr;
  std::optional<CacheAddress> secondary_address;

  if (secondary.has_value()) {
    secondary_address = decode_cache_address(secondary->config, word_index);
    if (primary_is_l1) {
      result.l2_accessed = true;
    } else {
      result.l1_accessed = true;
    }
    secondary_line = find_line(*secondary, *secondary_address);
    if (secondary_line != nullptr) {
      secondary_line->last_used = ++use_clock_;
      result.latency_cycles += secondary->config.hit_latency;
      result.events.push_back(secondary_name + " hit @" + std::to_string(address));
      if (primary_is_l1) {
        result.l2_hit = true;
        ++statistics_.l2_hits;
      } else {
        result.l1_hit = true;
        ++statistics_.l1_hits;
        ++statistics_.cache_hits;
      }
    } else {
      result.latency_cycles += secondary->config.hit_latency + secondary->config.miss_penalty;
      result.events.push_back(secondary_name + " miss @" + std::to_string(address));
      if (primary_is_l1) {
        ++statistics_.l2_misses;
      } else {
        ++statistics_.l1_misses;
        ++statistics_.cache_misses;
      }

      auto& victim = select_victim(*secondary, *secondary_address, use_clock_);
      populate_from_memory(*secondary, victim, *secondary_address, memory_);
      secondary_line = &victim;
    }
  } else {
    result.latency_cycles += primary->config.miss_penalty;
  }

  auto& primary_victim = select_victim(*primary, primary_address, use_clock_);
  populate_from_memory(*primary, primary_victim, primary_address, memory_);

  if (secondary_line != nullptr) {
    const std::size_t copied_words = std::min(primary->config.line_words, secondary->config.line_words);
    for (std::size_t word = 0; word < copied_words; ++word) {
      primary_victim.words[word] = secondary_line->words[word];
    }
  }

  if (write) {
    primary_victim.words[primary_address.offset] = *value;
    memory_[word_index] = *value;
    if (secondary_line != nullptr && secondary_address.has_value()) {
      secondary_line->words[secondary_address->offset] = *value;
      secondary_line->last_used = ++use_clock_;
    }
  } else {
    result.value = primary_victim.words[primary_address.offset];
  }

  statistics_.total_latency_cycles += result.latency_cycles;
  return result;
}

bool System::validate_word_address(std::int32_t address, std::size_t& index, std::string& error) const {
  if (address < 0 || (address % 4) != 0) {
    error = "invalid word address " + std::to_string(address);
    return false;
  }

  index = static_cast<std::size_t>(address / 4);
  if (index >= memory_.size()) {
    error = "memory access out of bounds at address " + std::to_string(address);
    return false;
  }

  return true;
}

}  // namespace nexus::sim::memory
