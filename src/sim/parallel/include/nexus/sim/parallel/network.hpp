#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nexus::sim::parallel {

// Interconnection networks with link contention (NEY704 (i)).
//
// Every directed channel (a node's injection and ejection channels and every router-to-router link) carries one
// flit per cycle.  A packet of F flits follows a deterministic route; on each channel it waits until the channel
// is free (first come, first served, ties by packet number), occupies it for F cycles, and its head reaches the
// next router one cycle later plus the router delay (virtual cut-through with unbounded buffers, so the model has
// no deadlock and no back-pressure; finite-buffer wormhole flow control is not modelled).  Zero-load latency of a
// route with L channels is (L - 1)(1 + router_delay) + F; everything above that is contention.
//
// Topologies and routing:
//   bus        one shared medium for every packet (L = 1)
//   crossbar   input port of the source, output port of the destination (L = 2)
//   ring       bidirectional, shortest direction (ties: clockwise from even sources, anticlockwise from odd)
//   mesh       k x k, dimension-order (XY) routing          (P must be a square)
//   torus      k x k with wrap-around links, XY, shortest way in each dimension (ties split by source parity)
//   hypercube  e-cube routing, lowest differing bit first   (P a power of two)

enum class NetTopology { Bus, Crossbar, Ring, Mesh, Torus, Hypercube };
enum class Traffic { Uniform, Transpose, BitComplement, Hotspot, Neighbor };

struct ContentionConfig {
  std::size_t nodes = 16;
  NetTopology topology = NetTopology::Mesh;
  std::size_t flits = 4;         // flits per packet
  std::size_t router_delay = 1;  // cycles a head spends in each router
};

struct Packet {
  std::size_t source = 0;
  std::size_t destination = 0;
  std::size_t inject = 0;  // cycle the packet is created at its source
};

struct ChannelLoad {
  std::string name;  // e.g. "inj 3", "5->6", "ej 0", "bus"
  std::size_t busy = 0;  // cycles carrying flits
  std::size_t packets = 0;
};

struct NetworkResult {
  bool success = false;
  std::string error;
  std::size_t packets = 0;
  std::size_t flits = 0;
  std::vector<std::size_t> latency;        // per packet: delivery of the tail minus creation
  std::vector<std::size_t> zero_load;      // per packet, the same route on an idle network
  std::size_t contention_cycles = 0;       // sum over packets and channels of the wait for a busy channel
  double average_latency = 0.0;
  double average_zero_load = 0.0;
  std::size_t max_latency = 0;
  std::size_t makespan = 0;                // last tail delivery
  double accepted_throughput = 0.0;        // flits delivered per node per cycle over the makespan
  std::vector<ChannelLoad> channels;
  std::size_t busiest = 0;                 // index into channels
};

std::string topology_name(NetTopology topology);
std::string traffic_name(Traffic traffic);
// Checks that the topology fits the node count; returns an error message or "".
std::string validate(const ContentionConfig& config);
// Channel names along the route from source to destination.
std::vector<std::string> route(const ContentionConfig& config, std::size_t source, std::size_t destination);
NetworkResult simulate_network(const ContentionConfig& config, const std::vector<Packet>& packets);

// Bernoulli injection: in each of `cycles` cycles every node creates a packet with probability rate / flits
// (so `rate` is the offered load in flits per node per cycle).  Destinations by pattern; packets to oneself are
// dropped (they would not use the network).
std::vector<Packet> make_traffic(Traffic traffic, std::size_t nodes, double rate, std::size_t flits, std::size_t cycles,
                                 std::uint32_t seed);
// Destination of `source` under a deterministic pattern (Uniform has none: returns source).
std::size_t pattern_destination(Traffic traffic, std::size_t nodes, std::size_t source);
// Upper bound on the accepted throughput (flits/node/cycle) of uniform traffic over the other P - 1 nodes: the
// smaller of 1 (one injection channel per node) and the bisection bound, where P/2 nodes send a fraction
// (P/2)/(P-1) of their flits across b channels per direction: lambda <= 4 b (P - 1) / P^2, with b = k for a
// k x k mesh, 2k for a torus, 2 for a ring, P/2 for a hypercube; a crossbar has no bisection limit and a bus
// carries one flit per cycle in total (lambda <= 1/P).
double uniform_throughput_bound(const ContentionConfig& config);

}  // namespace nexus::sim::parallel
