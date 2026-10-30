#include "nexus/sim/parallel/message_passing.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <deque>
#include <map>

namespace nexus::sim::parallel {

std::string collective_name(Collective collective) {
  switch (collective) {
    case Collective::BroadcastLinear: return "broadcast (linear)";
    case Collective::BroadcastTree: return "broadcast (binomial tree)";
    case Collective::ReduceTree: return "reduce (binomial tree)";
    case Collective::AllreduceReduceBroadcast: return "allreduce (reduce + broadcast)";
    case Collective::AllreduceRecursiveDoubling: return "allreduce (recursive doubling)";
  }
  return "?";
}

std::string topology_name(Topology topology) {
  switch (topology) {
    case Topology::Full: return "fully connected";
    case Topology::Ring: return "ring";
    case Topology::Mesh: return "2-D mesh";
    case Topology::Hypercube: return "hypercube";
  }
  return "?";
}

std::size_t hops(const NetworkConfig& config, std::size_t a, std::size_t b) {
  if (a == b) return 0;
  switch (config.topology) {
    case Topology::Full: return 1;
    case Topology::Ring: {
      const std::size_t d = a > b ? a - b : b - a;
      return std::min(d, config.nodes - d);
    }
    case Topology::Mesh: {
      const auto side = static_cast<std::size_t>(std::llround(std::sqrt(static_cast<double>(config.nodes))));
      const auto dx = static_cast<std::size_t>(std::llabs(static_cast<long long>(a % side) - static_cast<long long>(b % side)));
      const auto dy = static_cast<std::size_t>(std::llabs(static_cast<long long>(a / side) - static_cast<long long>(b / side)));
      return dx + dy;
    }
    case Topology::Hypercube: return static_cast<std::size_t>(std::popcount(a ^ b));
  }
  return 1;
}

MpResult run_message_passing(const NetworkConfig& config, const std::vector<std::vector<Operation>>& programs,
                             std::vector<std::vector<std::int64_t>> data) {
  MpResult result;
  const std::size_t n = programs.size();
  if (n != config.nodes || data.size() != n) {
    result.error = "one program and one buffer per node are needed";
    return result;
  }
  if (config.topology == Topology::Mesh) {
    const auto side = static_cast<std::size_t>(std::llround(std::sqrt(static_cast<double>(n))));
    if (side * side != n) {
      result.error = "a 2-D mesh needs a square number of nodes";
      return result;
    }
  }
  if (config.topology == Topology::Hypercube && !std::has_single_bit(n)) {
    result.error = "a hypercube needs a power-of-two number of nodes";
    return result;
  }
  struct Message {
    std::size_t arrival = 0;
    std::vector<std::int64_t> payload;
  };
  std::map<std::pair<std::size_t, std::size_t>, std::deque<Message>> channels;  // (from, to) in send order
  std::vector<std::size_t> next(n, 0);
  std::vector<std::size_t> clock(n, 0);
  bool progress = true;
  while (progress) {
    progress = false;
    for (std::size_t node = 0; node < n; ++node) {
      while (next[node] < programs[node].size()) {
        const auto& op = programs[node][next[node]];
        if (op.kind == OpKind::Compute) {
          clock[node] += op.cycles;
        } else if (op.kind == OpKind::Send) {
          const auto words = data[node].size();
          const auto latency = config.alpha + hops(config, node, op.peer) * config.hop_cycles;
          clock[node] += config.send_overhead + config.beta * words;  // the sender injects every word
          channels[{node, op.peer}].push_back(Message{clock[node] + latency, data[node]});
          ++result.messages;
          result.words += words;
        } else {
          auto& channel = channels[{op.peer, node}];
          if (channel.empty()) break;  // blocked
          const auto message = std::move(channel.front());
          channel.pop_front();
          clock[node] = std::max(clock[node], message.arrival) + config.recv_overhead;
          if (op.kind == OpKind::RecvReplace) {
            data[node] = message.payload;
          } else {
            for (std::size_t k = 0; k < data[node].size() && k < message.payload.size(); ++k) data[node][k] += message.payload[k];
          }
        }
        ++next[node];
        progress = true;
      }
    }
  }
  for (std::size_t node = 0; node < n; ++node) {
    if (next[node] < programs[node].size()) {
      result.error = "deadlock: node " + std::to_string(node) + " waits for a message from node " +
                     std::to_string(programs[node][next[node]].peer) + " that is never sent";
      return result;
    }
  }
  result.node_finish = clock;
  result.cycles = *std::max_element(clock.begin(), clock.end());
  result.data = std::move(data);
  result.success = true;
  return result;
}

namespace {

void tree_broadcast(std::vector<std::vector<Operation>>& p, std::size_t n) {
  // binomial tree rooted at 0: in round k (mask = 2^k, high to low) node i < mask sends to i + mask
  std::size_t top = 1;
  while (top < n) top <<= 1;
  for (std::size_t mask = top >> 1; mask >= 1; mask >>= 1) {
    for (std::size_t i = 0; i < n; ++i) {
      if (i % (2 * mask) == 0 && i + mask < n) {
        p[i].push_back({OpKind::Send, i + mask, 0});
        p[i + mask].push_back({OpKind::RecvReplace, i, 0});
      }
    }
    if (mask == 1) break;
  }
}

void tree_reduce(std::vector<std::vector<Operation>>& p, std::size_t n) {
  for (std::size_t mask = 1; mask < n; mask <<= 1) {
    for (std::size_t i = 0; i < n; ++i) {
      if (i % (2 * mask) == 0 && i + mask < n) {
        p[i + mask].push_back({OpKind::Send, i, 0});
        p[i].push_back({OpKind::RecvAdd, i + mask, 0});
      }
    }
  }
}

}  // namespace

std::vector<std::vector<Operation>> collective_program(Collective collective, std::size_t n) {
  std::vector<std::vector<Operation>> p(n);
  switch (collective) {
    case Collective::BroadcastLinear:
      for (std::size_t i = 1; i < n; ++i) {
        p[0].push_back({OpKind::Send, i, 0});
        p[i].push_back({OpKind::RecvReplace, 0, 0});
      }
      break;
    case Collective::BroadcastTree:
      tree_broadcast(p, n);
      break;
    case Collective::ReduceTree:
      tree_reduce(p, n);
      break;
    case Collective::AllreduceReduceBroadcast:
      tree_reduce(p, n);
      tree_broadcast(p, n);
      break;
    case Collective::AllreduceRecursiveDoubling:
      // exchange with the partner across every dimension of the hypercube (n a power of two)
      for (std::size_t mask = 1; mask < n; mask <<= 1) {
        for (std::size_t i = 0; i < n; ++i) {
          p[i].push_back({OpKind::Send, i ^ mask, 0});
          p[i].push_back({OpKind::RecvAdd, i ^ mask, 0});
        }
      }
      break;
  }
  return p;
}

MpResult run_collective(const NetworkConfig& config, Collective collective, std::size_t words) {
  std::vector<std::vector<std::int64_t>> data(config.nodes, std::vector<std::int64_t>(words, 0));
  for (std::size_t node = 0; node < config.nodes; ++node) {
    for (std::size_t k = 0; k < words; ++k) data[node][k] = static_cast<std::int64_t>(node * 10 + k + 1);
  }
  if (collective == Collective::AllreduceRecursiveDoubling && !std::has_single_bit(config.nodes)) {
    MpResult bad;
    bad.error = "recursive doubling needs a power-of-two number of nodes";
    return bad;
  }
  return run_message_passing(config, collective_program(collective, config.nodes), std::move(data));
}

}  // namespace nexus::sim::parallel
