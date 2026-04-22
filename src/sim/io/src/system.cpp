#include "nexus/sim/io/system.hpp"

#include <cctype>
#include <sstream>
#include <utility>

namespace nexus::sim::io {

namespace {

bool is_device_address(std::int32_t address) {
  return address == kConsoleDataAddress || address == kConsoleStatusAddress ||
      address == kTimerCompareAddress || address == kTimerControlAddress ||
      address == kTimerStatusAddress || address == kDmaSourceAddress ||
      address == kDmaDestinationAddress || address == kDmaLengthAddress ||
      address == kDmaControlAddress || address == kDmaStatusAddress;
}

std::string maybe_printable_char(std::int32_t value) {
  const unsigned char ch = static_cast<unsigned char>(value & 0xff);
  if (!std::isprint(ch) || ch == '\'' || ch == '\\') {
    return {};
  }

  return std::string(1, static_cast<char>(ch));
}

}  // namespace

std::string_view interrupt_source_name(InterruptSource source) {
  switch (source) {
    case InterruptSource::None:
      return "none";
    case InterruptSource::Timer:
      return "timer";
    case InterruptSource::DMA:
      return "dma";
  }

  return "unknown";
}

System::System(const Config& config) : config_(config) {}

bool System::handles(std::int32_t address) const { return is_device_address(address); }

AccessResult System::disabled_access(std::string_view device_name, std::size_t cycle) const {
  AccessResult result;
  result.handled = true;
  result.success = false;
  result.error = "device '" + std::string(device_name) + "' is disabled at cycle " + std::to_string(cycle);
  return result;
}

AccessResult System::read_word(std::int32_t address, std::size_t cycle) {
  AccessResult result;
  result.handled = true;
  result.latency_cycles = 1;

  switch (address) {
    case kConsoleDataAddress:
    case kConsoleStatusAddress:
      if (!config_.enable_console) {
        return disabled_access("console", cycle);
      }
      ++io_reads_;
      result.value = address == kConsoleStatusAddress ? 1 : 0;
      return result;
    case kTimerCompareAddress:
    case kTimerControlAddress:
    case kTimerStatusAddress:
      if (!config_.enable_timer) {
        return disabled_access("timer", cycle);
      }
      ++io_reads_;
      if (address == kTimerCompareAddress) {
        result.value = timer_compare_;
      } else if (address == kTimerControlAddress) {
        result.value = timer_enabled_ ? 1 : 0;
      } else {
        result.value = timer_pending_ ? 1 : 0;
      }
      return result;
    case kDmaSourceAddress:
    case kDmaDestinationAddress:
    case kDmaLengthAddress:
    case kDmaControlAddress:
    case kDmaStatusAddress:
      if (!config_.enable_dma) {
        return disabled_access("dma", cycle);
      }
      ++io_reads_;
      if (address == kDmaSourceAddress) {
        result.value = dma_source_;
      } else if (address == kDmaDestinationAddress) {
        result.value = dma_destination_;
      } else if (address == kDmaLengthAddress) {
        result.value = dma_length_;
      } else if (address == kDmaControlAddress) {
        result.value = dma_active_ ? 1 : (dma_irq_on_done_ ? 2 : 0);
      } else {
        result.value = dma_active_ ? 2 : (dma_done_ ? 1 : 0);
      }
      return result;
    default:
      result.handled = false;
      return result;
  }
}

AccessResult System::write_word(std::int32_t address, std::int32_t value, std::size_t cycle) {
  AccessResult result;
  result.handled = true;
  result.latency_cycles = 1;

  switch (address) {
    case kConsoleDataAddress:
    case kConsoleStatusAddress:
      if (!config_.enable_console) {
        return disabled_access("console", cycle);
      }
      ++io_writes_;
      if (address == kConsoleDataAddress) {
        ++console_writes_;
        std::ostringstream line;
        line << "io[console]: value=" << value;
        const std::string printable = maybe_printable_char(value);
        if (!printable.empty()) {
          line << " char='" << printable << "'";
        }
        output_lines_.push_back(line.str());
        result.events.push_back("io(console<-" + std::to_string(value) + ")");
      }
      return result;
    case kTimerCompareAddress:
    case kTimerControlAddress:
    case kTimerStatusAddress:
      if (!config_.enable_timer) {
        return disabled_access("timer", cycle);
      }
      ++io_writes_;
      if (address == kTimerCompareAddress) {
        timer_compare_ = value;
      } else if (address == kTimerControlAddress) {
        timer_enabled_ = (value & 1) != 0;
        if (timer_enabled_) {
          timer_pending_ = false;
        }
      } else {
        timer_pending_ = false;
      }
      return result;
    case kDmaSourceAddress:
    case kDmaDestinationAddress:
    case kDmaLengthAddress:
    case kDmaControlAddress:
    case kDmaStatusAddress:
      if (!config_.enable_dma) {
        return disabled_access("dma", cycle);
      }
      ++io_writes_;
      if (address == kDmaSourceAddress) {
        dma_source_ = value;
      } else if (address == kDmaDestinationAddress) {
        dma_destination_ = value;
      } else if (address == kDmaLengthAddress) {
        dma_length_ = value;
      } else if (address == kDmaControlAddress) {
        dma_irq_on_done_ = (value & 2) != 0;
        dma_done_ = false;
        if ((value & 1) != 0) {
          dma_active_ = dma_length_ > 0;
          dma_remaining_ = dma_length_;
        } else {
          dma_active_ = false;
          dma_remaining_ = 0;
        }
      } else {
        dma_done_ = false;
      }
      return result;
    default:
      result.handled = false;
      return result;
  }
}

TickResult System::tick(
    std::size_t cycle,
    const std::function<std::optional<std::int32_t>(std::int32_t)>& read_mem,
    const std::function<bool(std::int32_t, std::int32_t)>& write_mem) {
  TickResult result;

  if (config_.enable_timer && timer_enabled_ && timer_compare_ > 0) {
    --timer_compare_;
    if (timer_compare_ == 0) {
      timer_enabled_ = false;
      timer_pending_ = true;
      queue_interrupt(InterruptSource::Timer);
      result.interrupt_raised = true;
      result.source = InterruptSource::Timer;
      result.events.push_back("timer(interrupt-pending @ cycle " + std::to_string(cycle) + ")");
    }
  }

  if (config_.enable_dma && dma_active_ && dma_remaining_ > 0) {
    const std::int32_t current_source = dma_source_;
    const std::int32_t current_destination = dma_destination_;
    const auto value = read_mem(current_source);
    if (!value.has_value()) {
      result.error = "dma read failed at address " + std::to_string(current_source);
      return result;
    }
    if (!write_mem(current_destination, *value)) {
      result.error = "dma write failed at address " + std::to_string(current_destination);
      return result;
    }

    dma_source_ += 4;
    dma_destination_ += 4;
    --dma_remaining_;
    ++dma_words_copied_;
    result.dma_words_copied += 1;
    result.events.push_back(
        "dma(copy " + std::to_string(current_source) + " -> " + std::to_string(current_destination) + ")");

    if (dma_remaining_ == 0) {
      dma_active_ = false;
      dma_done_ = true;
      ++dma_completion_count_;
      result.dma_completed = true;
      result.events.push_back("dma(complete @ cycle " + std::to_string(cycle) + ")");
      if (dma_irq_on_done_) {
        queue_interrupt(InterruptSource::DMA);
        result.interrupt_raised = true;
        result.source = InterruptSource::DMA;
      }
    }
  }

  return result;
}

bool System::has_pending_interrupt() const { return pending_interrupt_ != InterruptSource::None; }

InterruptSource System::pending_interrupt() const { return pending_interrupt_; }

void System::acknowledge_interrupt() {
  if (pending_interrupt_ == InterruptSource::Timer) {
    timer_pending_ = false;
  }
  pending_interrupt_ = InterruptSource::None;
}

std::vector<std::string> System::take_output_lines() {
  auto lines = std::move(output_lines_);
  output_lines_.clear();
  return lines;
}

std::size_t System::io_read_count() const { return io_reads_; }

std::size_t System::io_write_count() const { return io_writes_; }

std::size_t System::console_write_count() const { return console_writes_; }

std::size_t System::dma_words_copied() const { return dma_words_copied_; }

std::size_t System::dma_completion_count() const { return dma_completion_count_; }

void System::queue_interrupt(InterruptSource source) {
  if (pending_interrupt_ == InterruptSource::None) {
    pending_interrupt_ = source;
  }
}

}  // namespace nexus::sim::io
