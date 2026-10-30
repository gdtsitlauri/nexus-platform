#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "nexus/sim/parallel/message_passing.hpp"

namespace mp = nexus::sim::parallel;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::size_t log2(std::size_t n) {
  std::size_t k = 0;
  while ((std::size_t{1} << k) < n) ++k;
  return k;
}

}  // namespace

int main() {
  mp::NetworkConfig net;  // fully connected: o = 10, r = 10, L = alpha + hop = 55, beta = 1 cycle per word
  const std::size_t latency = net.alpha + net.hop_cycles;
  for (const std::size_t words : {std::size_t{1}, std::size_t{1000}}) {
    const std::size_t send = net.send_overhead + net.beta * words;
    for (std::size_t p = 2; p <= 16; p *= 2) {
      net.nodes = p;
      const auto linear = mp::run_collective(net, mp::Collective::BroadcastLinear, words);
      const auto tree = mp::run_collective(net, mp::Collective::BroadcastTree, words);
      const auto rd = mp::run_collective(net, mp::Collective::AllreduceRecursiveDoubling, words);
      const auto rb = mp::run_collective(net, mp::Collective::AllreduceReduceBroadcast, words);
      expect(linear.success && tree.success && rd.success && rb.success, "collectives run");
      expect(linear.cycles == (p - 1) * send + latency + net.recv_overhead,
             "linear broadcast = (P-1)(o + beta m) + L + r for P = " + std::to_string(p));
      expect(tree.cycles == log2(p) * (send + latency + net.recv_overhead),
             "tree broadcast = log2 P (o + beta m + L + r) for P = " + std::to_string(p));
      expect(rd.cycles == log2(p) * (send + latency + net.recv_overhead), "recursive doubling = log2 P rounds");
      expect(rb.cycles == 2 * rd.cycles, "reduce + broadcast takes twice as long as recursive doubling");
      // data
      std::vector<std::int64_t> sum(words, 0);
      for (std::size_t node = 0; node < p; ++node) {
        for (std::size_t k = 0; k < words; ++k) sum[k] += static_cast<std::int64_t>(node * 10 + k + 1);
      }
      bool broadcast_ok = true;
      bool allreduce_ok = true;
      for (std::size_t node = 0; node < p; ++node) {
        broadcast_ok = broadcast_ok && linear.data[node] == linear.data[0] && tree.data[node] == linear.data[0] &&
                       tree.data[node][0] == 1;
        allreduce_ok = allreduce_ok && rd.data[node] == sum && rb.data[node] == sum;
      }
      expect(broadcast_ok, "every node holds the root's data after a broadcast");
      expect(allreduce_ok, "every node holds the sum after an allreduce");
      expect(mp::run_collective(net, mp::Collective::ReduceTree, words).data[0] == sum, "the root holds the sum after a reduce");
    }
  }
  // Latency against bandwidth: for one-word messages the linear broadcast wins at 16 nodes (the sender's overhead is
  // small), for 1000-word messages the tree wins (the root would inject 15 long messages).
  net.nodes = 16;
  expect(mp::run_collective(net, mp::Collective::BroadcastLinear, 1).cycles <
             mp::run_collective(net, mp::Collective::BroadcastTree, 1).cycles,
         "short messages: linear broadcast faster");
  expect(mp::run_collective(net, mp::Collective::BroadcastTree, 1000).cycles <
             mp::run_collective(net, mp::Collective::BroadcastLinear, 1000).cycles,
         "long messages: tree broadcast faster");

  // Topology: more hops cost more.
  net.topology = mp::Topology::Ring;
  const auto ring = mp::run_collective(net, mp::Collective::BroadcastTree, 8).cycles;
  net.topology = mp::Topology::Hypercube;
  const auto cube = mp::run_collective(net, mp::Collective::BroadcastTree, 8).cycles;
  net.topology = mp::Topology::Mesh;
  const auto mesh = mp::run_collective(net, mp::Collective::BroadcastTree, 8).cycles;
  expect(cube <= mesh && mesh <= ring && cube < ring, "tree broadcast: hypercube <= mesh <= ring");
  expect(mp::hops(net, 0, 15) == 6, "4x4 mesh: corner to corner is 6 hops");
  net.nodes = 12;
  expect(!mp::run_collective(net, mp::Collective::BroadcastTree, 1).success, "a 12-node mesh is rejected");

  // Deadlock: both nodes receive first.
  mp::NetworkConfig two;
  two.nodes = 2;
  const std::vector<std::vector<mp::Operation>> programs{
      {{mp::OpKind::RecvReplace, 1, 0}, {mp::OpKind::Send, 1, 0}},
      {{mp::OpKind::RecvReplace, 0, 0}, {mp::OpKind::Send, 0, 0}}};
  const auto dead = mp::run_message_passing(two, programs, {{1}, {2}});
  expect(!dead.success && dead.error.find("deadlock") != std::string::npos, "receive-receive deadlock detected");

  if (failures == 0) {
    std::cout << "message passing: broadcast, reduce and allreduce on 2-16 nodes match their cost formulas; "
                 "topologies, data and deadlock checked\n";
  }
  return failures == 0 ? 0 : 1;
}
