#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Event-driven gate-level simulation with transport delays and feedback (EY121 sequential elements): latches and
// flip-flops built from gates, races and oscillation. Values are 0, 1 and X (unknown).
namespace nexus::logic {

enum class Value : std::uint8_t { Zero = 0, One = 1, X = 2 };

enum class GateType { Buf, Not, And, Or, Nand, Nor, Xor };

struct Gate {
  GateType type = GateType::Buf;
  std::vector<std::size_t> inputs;
  std::size_t output = 0;
  std::uint32_t delay = 1;
};

class GateNetlist {
 public:
  std::size_t net(const std::string& name);  // creates on first use
  std::size_t find(const std::string& name) const;
  void gate(GateType type, std::vector<std::string> inputs, const std::string& output, std::uint32_t delay = 1);

  // Schedules an input change at an absolute time.
  void drive(const std::string& net, Value v, std::uint64_t time);
  // Runs until no events remain or `until`; returns false if the event budget is exhausted (oscillation).
  bool run(std::uint64_t until, std::size_t max_events = 100000);

  Value value(const std::string& net) const;
  std::uint64_t now() const { return now_; }
  std::size_t events_processed() const { return events_; }
  // History of (time, value) changes of a net, for waveforms.
  const std::vector<std::pair<std::uint64_t, Value>>& history(const std::string& net) const;
  std::string waveform(const std::vector<std::string>& nets, std::uint64_t until) const;

 private:
  struct Event {
    std::uint64_t time;
    std::size_t net;
    Value value;
    std::uint64_t seq;
  };
  Value eval(const Gate& g) const;
  void schedule(std::uint64_t time, std::size_t net, Value v);

  std::vector<std::string> names_;
  std::vector<Value> values_;
  std::vector<std::vector<std::size_t>> fanout_;  // net -> gates
  std::vector<std::vector<std::pair<std::uint64_t, Value>>> history_;
  std::vector<Gate> gates_;
  std::vector<Event> queue_;  // binary heap on (time, seq)
  std::uint64_t now_ = 0;
  std::uint64_t seq_ = 0;
  std::size_t events_ = 0;
};

// Ready-made sequential elements (gate delay 1 unless stated):
// SR latch from two cross-coupled NOR gates: inputs S, R; outputs Q, QN.
GateNetlist sr_nor_latch();
// Gated D latch from NAND gates: inputs D, EN; outputs Q, QN.
GateNetlist gated_d_latch();
// Positive-edge master-slave D flip-flop from two gated D latches: inputs D, CLK; outputs Q, QN.
GateNetlist master_slave_d_flip_flop();

}  // namespace nexus::logic
