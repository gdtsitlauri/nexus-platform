#include <iostream>
#include <optional>
#include <vector>

#include "nexus/sim/io/system.hpp"

int main() {
  using nexus::sim::io::Config;
  using nexus::sim::io::InterruptSource;
  using nexus::sim::io::System;

  System io({.enable_console = true, .enable_timer = true, .enable_dma = true});

  const auto console_write = io.write_word(nexus::sim::io::kConsoleDataAddress, 65, 1);
  if (!console_write.success) {
    std::cerr << "Console device write failed.\n";
    return 1;
  }

  const auto console_lines = io.take_output_lines();
  if (console_lines.size() != 1U || console_lines.front() != "io[console]: value=65 char='A'") {
    std::cerr << "Console device output is incorrect.\n";
    return 1;
  }

  (void)io.write_word(nexus::sim::io::kTimerCompareAddress, 2, 2);
  (void)io.write_word(nexus::sim::io::kTimerControlAddress, 1, 2);
  const auto timer_tick1 = io.tick(1, [](std::int32_t) { return std::optional<std::int32_t>{0}; },
      [](std::int32_t, std::int32_t) { return true; });
  const auto timer_tick2 = io.tick(2, [](std::int32_t) { return std::optional<std::int32_t>{0}; },
      [](std::int32_t, std::int32_t) { return true; });
  if (timer_tick1.interrupt_raised || !timer_tick2.interrupt_raised ||
      timer_tick2.source != InterruptSource::Timer || !io.has_pending_interrupt()) {
    std::cerr << "Timer interrupt behavior is incorrect.\n";
    return 1;
  }
  io.acknowledge_interrupt();

  std::vector<std::int32_t> backing(32, 0);
  backing[0] = 21;
  backing[1] = 22;

  (void)io.write_word(nexus::sim::io::kDmaSourceAddress, 0, 3);
  (void)io.write_word(nexus::sim::io::kDmaDestinationAddress, 16, 3);
  (void)io.write_word(nexus::sim::io::kDmaLengthAddress, 2, 3);
  (void)io.write_word(nexus::sim::io::kDmaControlAddress, 3, 3);

  auto read_mem = [&backing](std::int32_t address) -> std::optional<std::int32_t> {
    if (address < 0 || (address % 4) != 0) {
      return std::nullopt;
    }
    const std::size_t index = static_cast<std::size_t>(address / 4);
    if (index >= backing.size()) {
      return std::nullopt;
    }
    return backing[index];
  };

  auto write_mem = [&backing](std::int32_t address, std::int32_t value) -> bool {
    if (address < 0 || (address % 4) != 0) {
      return false;
    }
    const std::size_t index = static_cast<std::size_t>(address / 4);
    if (index >= backing.size()) {
      return false;
    }
    backing[index] = value;
    return true;
  };

  const auto dma_tick1 = io.tick(3, read_mem, write_mem);
  const auto dma_tick2 = io.tick(4, read_mem, write_mem);
  if (!dma_tick2.interrupt_raised || dma_tick2.source != InterruptSource::DMA || backing[4] != 21 ||
      backing[5] != 22 || io.dma_words_copied() != 2U || io.dma_completion_count() != 1U ||
      dma_tick1.dma_words_copied != 1U || dma_tick2.dma_words_copied != 1U) {
    std::cerr << "DMA device behavior is incorrect.\n";
    return 1;
  }

  return 0;
}
