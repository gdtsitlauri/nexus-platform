#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nexus::sim::parallel {

// Distributed-memory systems (NEY704 (d)): P nodes with private memories that communicate only by messages.
// Every node runs a list of operations (send, receive, compute); a receive blocks until the matching message has
// arrived.  Cost model (per message of m words over h hops, cut-through routing):
//   sender busy  o + beta * m (overhead and injection of every word),   network  L = alpha + h * hop_cycles,
//   receiver busy  r = recv_overhead
// Topologies: fully connected, ring, 2-D mesh (P must be a square) and hypercube (P a power of two).
// Collective operations built on point-to-point messages: linear and binomial-tree broadcast, binomial-tree reduce,
// allreduce by reduce + broadcast and by recursive doubling.  The simulation computes real data, so results are
// checked, and it reports a deadlock when no node can make progress.

enum class Topology { Full, Ring, Mesh, Hypercube };
enum class Collective { BroadcastLinear, BroadcastTree, ReduceTree, AllreduceReduceBroadcast, AllreduceRecursiveDoubling };

struct NetworkConfig {
  std::size_t nodes = 8;
  Topology topology = Topology::Full;
  std::size_t send_overhead = 10;
  std::size_t recv_overhead = 10;
  std::size_t alpha = 50;
  std::size_t hop_cycles = 5;
  std::size_t beta = 1;  // cycles per word
};

enum class OpKind { Send, RecvReplace, RecvAdd, Compute };

struct Operation {
  OpKind kind = OpKind::Compute;
  std::size_t peer = 0;
  std::size_t cycles = 0;  // Compute
};

struct MpResult {
  bool success = false;
  std::string error;            // e.g. a deadlock
  std::size_t cycles = 0;       // completion time of the last node
  std::size_t messages = 0;
  std::size_t words = 0;
  std::vector<std::size_t> node_finish;
  std::vector<std::vector<std::int64_t>> data;  // final buffer of every node
};

std::size_t hops(const NetworkConfig& config, std::size_t a, std::size_t b);
// Runs per-node operation lists; `data` holds the initial buffer of every node.
MpResult run_message_passing(const NetworkConfig& config, const std::vector<std::vector<Operation>>& programs,
                             std::vector<std::vector<std::int64_t>> data);
// The operation lists of a collective (root 0); every node starts with `words` words.
std::vector<std::vector<Operation>> collective_program(Collective collective, std::size_t nodes);
MpResult run_collective(const NetworkConfig& config, Collective collective, std::size_t words);
std::string collective_name(Collective collective);
std::string topology_name(Topology topology);

}  // namespace nexus::sim::parallel
