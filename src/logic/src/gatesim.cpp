#include "nexus/logic/gatesim.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace nexus::logic {

namespace {

bool heap_less(const auto& a, const auto& b) {
  // std heap is a max-heap: "less" means later
  return a.time != b.time ? a.time > b.time : a.seq > b.seq;
}

Value v_not(Value a) { return a == Value::X ? Value::X : (a == Value::One ? Value::Zero : Value::One); }

}  // namespace

std::size_t GateNetlist::net(const std::string& name) {
  const auto it = std::find(names_.begin(), names_.end(), name);
  if (it != names_.end()) {
    return static_cast<std::size_t>(it - names_.begin());
  }
  names_.push_back(name);
  values_.push_back(Value::X);
  fanout_.emplace_back();
  history_.push_back({{0, Value::X}});
  return names_.size() - 1;
}

std::size_t GateNetlist::find(const std::string& name) const {
  const auto it = std::find(names_.begin(), names_.end(), name);
  if (it == names_.end()) {
    throw std::runtime_error("gatesim: unknown net " + name);
  }
  return static_cast<std::size_t>(it - names_.begin());
}

void GateNetlist::gate(GateType type, std::vector<std::string> inputs, const std::string& output,
                       std::uint32_t delay) {
  Gate g;
  g.type = type;
  g.delay = delay;
  for (const auto& in : inputs) {
    g.inputs.push_back(net(in));
  }
  g.output = net(output);
  gates_.push_back(g);
  for (const auto in : g.inputs) {
    fanout_[in].push_back(gates_.size() - 1);
  }
}

Value GateNetlist::eval(const Gate& g) const {
  auto all = [&](Value want) {
    return std::all_of(g.inputs.begin(), g.inputs.end(), [&](std::size_t n) { return values_[n] == want; });
  };
  auto any = [&](Value want) {
    return std::any_of(g.inputs.begin(), g.inputs.end(), [&](std::size_t n) { return values_[n] == want; });
  };
  switch (g.type) {
    case GateType::Buf:
      return values_[g.inputs[0]];
    case GateType::Not:
      return v_not(values_[g.inputs[0]]);
    case GateType::And:
    case GateType::Nand: {
      Value r = any(Value::Zero) ? Value::Zero : (all(Value::One) ? Value::One : Value::X);
      return g.type == GateType::Nand ? v_not(r) : r;
    }
    case GateType::Or:
    case GateType::Nor: {
      Value r = any(Value::One) ? Value::One : (all(Value::Zero) ? Value::Zero : Value::X);
      return g.type == GateType::Nor ? v_not(r) : r;
    }
    case GateType::Xor: {
      if (any(Value::X)) {
        return Value::X;
      }
      bool parity = false;
      for (const auto n : g.inputs) {
        parity ^= values_[n] == Value::One;
      }
      return parity ? Value::One : Value::Zero;
    }
  }
  return Value::X;
}

void GateNetlist::schedule(std::uint64_t time, std::size_t n, Value v) {
  queue_.push_back(Event{time, n, v, seq_++});
  std::push_heap(queue_.begin(), queue_.end(), [](const Event& a, const Event& b) { return heap_less(a, b); });
}

void GateNetlist::drive(const std::string& name, Value v, std::uint64_t time) { schedule(time, net(name), v); }

bool GateNetlist::run(std::uint64_t until, std::size_t max_events) {
  const auto cmp = [](const Event& a, const Event& b) { return heap_less(a, b); };
  std::size_t budget = max_events;
  while (!queue_.empty()) {
    std::pop_heap(queue_.begin(), queue_.end(), cmp);
    const Event e = queue_.back();
    if (e.time > until) {
      std::push_heap(queue_.begin(), queue_.end(), cmp);
      now_ = until;
      return true;
    }
    queue_.pop_back();
    now_ = e.time;
    if (values_[e.net] == e.value) {
      continue;
    }
    if (budget-- == 0) {
      return false;
    }
    ++events_;
    values_[e.net] = e.value;
    history_[e.net].emplace_back(e.time, e.value);
    for (const auto gi : fanout_[e.net]) {
      const Gate& g = gates_[gi];
      schedule(now_ + g.delay, g.output, eval(g));
    }
  }
  now_ = std::max(now_, until);
  return true;
}

Value GateNetlist::value(const std::string& name) const { return values_[find(name)]; }

const std::vector<std::pair<std::uint64_t, Value>>& GateNetlist::history(const std::string& name) const {
  return history_[find(name)];
}

std::string GateNetlist::waveform(const std::vector<std::string>& nets, std::uint64_t until) const {
  std::ostringstream out;
  std::size_t width = 0;
  for (const auto& n : nets) {
    width = std::max(width, n.size());
  }
  for (const auto& n : nets) {
    out << n << std::string(width - n.size(), ' ') << " ";
    const auto& h = history(n);
    for (std::uint64_t t = 0; t <= until; ++t) {
      Value v = Value::X;
      for (const auto& [time, val] : h) {
        if (time <= t) {
          v = val;
        }
      }
      out << (v == Value::One ? '#' : (v == Value::Zero ? '_' : 'x'));
    }
    out << '\n';
  }
  return out.str();
}

GateNetlist sr_nor_latch() {
  GateNetlist g;
  g.gate(GateType::Nor, {"R", "QN"}, "Q");
  g.gate(GateType::Nor, {"S", "Q"}, "QN");
  return g;
}

GateNetlist gated_d_latch() {
  GateNetlist g;
  g.gate(GateType::Not, {"D"}, "DN");
  g.gate(GateType::Nand, {"D", "EN"}, "SN");
  g.gate(GateType::Nand, {"DN", "EN"}, "RN");
  g.gate(GateType::Nand, {"SN", "QN"}, "Q");
  g.gate(GateType::Nand, {"RN", "Q"}, "QN");
  return g;
}

GateNetlist master_slave_d_flip_flop() {
  GateNetlist g;
  g.gate(GateType::Not, {"CLK"}, "CLKN");
  // master latch: transparent while CLK = 0
  g.gate(GateType::Not, {"D"}, "DN");
  g.gate(GateType::Nand, {"D", "CLKN"}, "MSN");
  g.gate(GateType::Nand, {"DN", "CLKN"}, "MRN");
  g.gate(GateType::Nand, {"MSN", "MQN"}, "MQ");
  g.gate(GateType::Nand, {"MRN", "MQ"}, "MQN");
  // slave latch: transparent while CLK = 1
  g.gate(GateType::Nand, {"MQ", "CLK"}, "SSN");
  g.gate(GateType::Nand, {"MQN", "CLK"}, "SRN");
  g.gate(GateType::Nand, {"SSN", "QN"}, "Q");
  g.gate(GateType::Nand, {"SRN", "Q"}, "QN");
  return g;
}

}  // namespace nexus::logic
