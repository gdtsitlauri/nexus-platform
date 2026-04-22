#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nexus::sim::io {

constexpr std::int32_t kDeviceBaseAddress = 0x00010000;

constexpr std::int32_t kConsoleDataAddress = 0x00010000;
constexpr std::int32_t kConsoleStatusAddress = 0x00010004;

constexpr std::int32_t kTimerCompareAddress = 0x00010100;
constexpr std::int32_t kTimerControlAddress = 0x00010104;
constexpr std::int32_t kTimerStatusAddress = 0x00010108;

constexpr std::int32_t kDmaSourceAddress = 0x00010200;
constexpr std::int32_t kDmaDestinationAddress = 0x00010204;
constexpr std::int32_t kDmaLengthAddress = 0x00010208;
constexpr std::int32_t kDmaControlAddress = 0x0001020C;
constexpr std::int32_t kDmaStatusAddress = 0x00010210;

enum class InterruptSource {
  None,
  Timer,
  DMA,
};

std::string_view interrupt_source_name(InterruptSource source);

struct Config {
  bool enable_console = false;
  bool enable_timer = false;
  bool enable_dma = false;
};

struct AccessResult {
  bool handled = false;
  bool success = true;
  std::int32_t value = 0;
  std::size_t latency_cycles = 1;
  std::string error;
  std::vector<std::string> events;
};

struct TickResult {
  std::vector<std::string> events;
  bool interrupt_raised = false;
  InterruptSource source = InterruptSource::None;
  std::size_t dma_words_copied = 0;
  bool dma_completed = false;
  std::string error;
};

class System {
 public:
  explicit System(const Config& config = {});

  bool handles(std::int32_t address) const;

  AccessResult read_word(std::int32_t address, std::size_t cycle);
  AccessResult write_word(std::int32_t address, std::int32_t value, std::size_t cycle);

  TickResult tick(
      std::size_t cycle,
      const std::function<std::optional<std::int32_t>(std::int32_t)>& read_mem,
      const std::function<bool(std::int32_t, std::int32_t)>& write_mem);

  bool has_pending_interrupt() const;
  InterruptSource pending_interrupt() const;
  void acknowledge_interrupt();

  std::vector<std::string> take_output_lines();

  std::size_t io_read_count() const;
  std::size_t io_write_count() const;
  std::size_t console_write_count() const;
  std::size_t dma_words_copied() const;
  std::size_t dma_completion_count() const;

 private:
  void queue_interrupt(InterruptSource source);
  AccessResult disabled_access(std::string_view device_name, std::size_t cycle) const;

  Config config_;
  std::vector<std::string> output_lines_;

  std::size_t io_reads_ = 0;
  std::size_t io_writes_ = 0;
  std::size_t console_writes_ = 0;
  std::size_t dma_words_copied_ = 0;
  std::size_t dma_completion_count_ = 0;

  std::int32_t timer_compare_ = 0;
  bool timer_enabled_ = false;
  bool timer_pending_ = false;

  std::int32_t dma_source_ = 0;
  std::int32_t dma_destination_ = 0;
  std::int32_t dma_length_ = 0;
  std::int32_t dma_remaining_ = 0;
  bool dma_active_ = false;
  bool dma_done_ = false;
  bool dma_irq_on_done_ = false;

  InterruptSource pending_interrupt_ = InterruptSource::None;
};

}  // namespace nexus::sim::io
