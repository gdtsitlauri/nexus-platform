#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "nexus/sim/parallel/network.hpp"

namespace net = nexus::sim::parallel;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

net::ContentionConfig config(net::NetTopology topology, std::size_t nodes, std::size_t flits = 4) {
  net::ContentionConfig c;
  c.topology = topology;
  c.nodes = nodes;
  c.flits = flits;
  return c;
}

std::size_t distance(net::NetTopology topology, std::size_t nodes, std::size_t a, std::size_t b) {
  const std::size_t k = topology == net::NetTopology::Mesh || topology == net::NetTopology::Torus
                            ? static_cast<std::size_t>(std::sqrt(static_cast<double>(nodes)) + 0.5)
                            : 0;
  auto gap = [](std::size_t x, std::size_t y) { return x > y ? x - y : y - x; };
  switch (topology) {
    case net::NetTopology::Bus: return 0;
    case net::NetTopology::Crossbar: return 0;
    case net::NetTopology::Ring: return std::min(gap(a, b), nodes - gap(a, b));
    case net::NetTopology::Mesh: return gap(a % k, b % k) + gap(a / k, b / k);
    case net::NetTopology::Torus: {
      const std::size_t dx = gap(a % k, b % k);
      const std::size_t dy = gap(a / k, b / k);
      return std::min(dx, k - dx) + std::min(dy, k - dy);
    }
    case net::NetTopology::Hypercube: return static_cast<std::size_t>(std::popcount(a ^ b));
  }
  return 0;
}

void routing() {
  const std::vector<std::pair<net::NetTopology, std::size_t>> cases = {
      {net::NetTopology::Ring, 9}, {net::NetTopology::Ring, 16}, {net::NetTopology::Mesh, 16}, {net::NetTopology::Mesh, 25},
      {net::NetTopology::Torus, 16}, {net::NetTopology::Torus, 25}, {net::NetTopology::Hypercube, 32},
      {net::NetTopology::Crossbar, 8}};
  std::size_t routes = 0;
  for (const auto& [topology, nodes] : cases) {
    const auto c = config(topology, nodes);
    for (std::size_t a = 0; a < nodes; ++a) {
      for (std::size_t b = 0; b < nodes; ++b) {
        if (a == b) {
          continue;
        }
        const auto path = net::route(c, a, b);
        ++routes;
        bool ok = path.size() >= 2 && path.front() == "inj " + std::to_string(a) && path.back() == "ej " + std::to_string(b);
        // the router-to-router links form a chain from a to b
        std::size_t at = a;
        bool y_started = false;
        for (std::size_t h = 1; ok && h + 1 < path.size(); ++h) {
          const auto arrow = path[h].find("->");
          ok = arrow != std::string::npos && std::stoul(path[h].substr(0, arrow)) == at;
          if (!ok) {
            break;
          }
          const std::size_t next = std::stoul(path[h].substr(arrow + 2));
          if (topology == net::NetTopology::Mesh) {
            const std::size_t k = nodes == 16 ? 4 : 5;
            const bool y_move = at % k == next % k;
            ok = !(y_started && !y_move);  // XY: no x move after the first y move
            y_started = y_started || y_move;
          }
          at = next;
        }
        const bool direct = topology == net::NetTopology::Crossbar;  // input port straight to output port
        ok = ok && (direct || at == b) && path.size() - 2 == distance(topology, nodes, a, b);
        expect(ok, net::topology_name(topology) + ": route " + std::to_string(a) + " -> " + std::to_string(b) +
                       " is a minimal connected path");
      }
    }
  }
  expect(net::route(config(net::NetTopology::Bus, 4), 1, 3) == std::vector<std::string>{"bus"}, "bus route is the bus");
  std::cout << "routing: " << routes << " routes checked (minimal, connected, XY order on the mesh)\n";
}

void exact_timing() {
  // Zero load: 0 -> 15 on a 4x4 mesh crosses 8 channels: (8 - 1) * (1 + 1) + 4 = 18 cycles.
  const auto mesh = config(net::NetTopology::Mesh, 16);
  const auto single = net::simulate_network(mesh, {{0, 15, 5}});
  expect(single.success && single.latency[0] == 18 && single.zero_load[0] == 18 && single.contention_cycles == 0,
         "single packet: latency equals the zero-load formula");
  expect(net::route(mesh, 0, 15) == std::vector<std::string>{"inj 0", "0->1", "1->2", "2->3", "3->7", "7->11", "11->15", "ej 15"},
         "XY route 0 -> 15 on the 4x4 mesh");
  // Two packets from the same source at the same cycle: the second waits one packet time (4 cycles) for inj 0.
  const auto crossbar = config(net::NetTopology::Crossbar, 8);
  const auto same_source = net::simulate_network(crossbar, {{0, 1, 0}, {0, 2, 0}});
  expect(same_source.latency == std::vector<std::size_t>{6, 10} && same_source.contention_cycles == 4,
         "crossbar: shared input port delays the second packet by F");
  // Different inputs and outputs: a crossbar has no internal contention.
  const auto disjoint = net::simulate_network(crossbar, {{0, 1, 0}, {2, 3, 0}, {4, 5, 0}, {6, 7, 0}});
  expect(disjoint.contention_cycles == 0 && disjoint.max_latency == 6, "crossbar: a permutation sees no contention");
  // Same output port: serialised at the destination.
  const auto same_output = net::simulate_network(crossbar, {{0, 5, 0}, {2, 5, 0}});
  expect(same_output.latency == std::vector<std::size_t>{6, 10}, "crossbar: output port contention");
  // Hotspot: 15 packets to node 0 drain through ej 0 one after the other.
  std::vector<net::Packet> hot;
  for (std::size_t s = 1; s < 16; ++s) {
    hot.push_back({s, 0, 0});
  }
  const auto hotspot = net::simulate_network(config(net::NetTopology::Crossbar, 16), hot);
  expect(hotspot.makespan == 2 + 15 * 4, "hotspot: the ejection channel serialises 15 packets (makespan 62)");
  expect(hotspot.channels[hotspot.busiest].name == "ej 0" && hotspot.channels[hotspot.busiest].busy == 60,
         "hotspot: ej 0 is the busiest channel");
  // A bus carries one flit per cycle in total.
  const auto bus = net::simulate_network(config(net::NetTopology::Bus, 4), {{0, 1, 0}, {2, 3, 0}, {1, 0, 0}});
  expect(bus.makespan == 12 && bus.latency == std::vector<std::size_t>{4, 8, 12}, "bus: three packets in sequence");
  // Errors.
  expect(!net::validate(config(net::NetTopology::Mesh, 12)).empty(), "a mesh needs a square node count");
  expect(!net::validate(config(net::NetTopology::Hypercube, 12)).empty(), "a hypercube needs a power of two");
  expect(!net::simulate_network(mesh, {{3, 3, 0}}).success, "a packet to itself is refused");
}

void invariants() {
  for (const auto topology : {net::NetTopology::Bus, net::NetTopology::Crossbar, net::NetTopology::Ring, net::NetTopology::Mesh,
                              net::NetTopology::Torus, net::NetTopology::Hypercube}) {
    const auto c = config(topology, 16, 3);
    const auto packets = net::make_traffic(net::Traffic::Uniform, 16, 0.3, 3, 500, 42);
    const auto r = net::simulate_network(c, packets);
    std::size_t expected_busy = 0;
    bool above_zero_load = true;
    for (std::size_t p = 0; p < packets.size(); ++p) {
      expected_busy += 3 * net::route(c, packets[p].source, packets[p].destination).size();
      above_zero_load = above_zero_load && r.latency[p] >= r.zero_load[p];
    }
    std::size_t busy = 0;
    bool within_makespan = true;
    for (const auto& channel : r.channels) {
      busy += channel.busy;
      within_makespan = within_makespan && channel.busy <= r.makespan;
    }
    const std::string name = net::topology_name(topology);
    expect(r.success && r.packets == packets.size(), name + ": every packet delivered");
    expect(busy == expected_busy, name + ": channel busy cycles = flits x route length");
    expect(above_zero_load, name + ": no packet beats its zero-load latency");
    expect(within_makespan, name + ": no channel is busy more than 100% of the time");
  }
}

void saturation() {
  struct Case {
    net::NetTopology topology;
    std::size_t nodes;
  };
  for (const auto& [topology, nodes] : {Case{net::NetTopology::Mesh, 64}, Case{net::NetTopology::Torus, 64},
                                        Case{net::NetTopology::Ring, 16}, Case{net::NetTopology::Bus, 8}}) {
    const auto c = config(topology, nodes);
    const double bound = net::uniform_throughput_bound(c);
    const auto light = net::simulate_network(c, net::make_traffic(net::Traffic::Uniform, nodes, 0.25 * bound, 4, 3000, 7));
    const auto heavy = net::simulate_network(c, net::make_traffic(net::Traffic::Uniform, nodes, 2.0 * bound, 4, 3000, 7));
    const std::string name = net::topology_name(topology) + " (" + std::to_string(nodes) + " nodes)";
    expect(light.accepted_throughput > 0.9 * 0.25 * bound, name + ": light load is fully accepted");
    expect(light.average_latency < 1.5 * light.average_zero_load, name + ": light load stays near zero-load latency");
    expect(heavy.accepted_throughput <= 1.03 * bound,
           name + ": accepted throughput " + std::to_string(heavy.accepted_throughput) + " respects the bisection bound " +
               std::to_string(bound));
    expect(heavy.accepted_throughput >= 0.85 * bound, name + ": overload reaches at least 85% of the bound");
    expect(heavy.average_latency > 10.0 * heavy.average_zero_load, name + ": past saturation latency grows without limit");
    std::cout << name << ": bound " << bound << ", accepted at 2x bound " << heavy.accepted_throughput << '\n';
  }
  // Wrap-around links: a torus has a shorter average route and a higher saturation point than the mesh.
  const auto mesh = config(net::NetTopology::Mesh, 64);
  const auto torus = config(net::NetTopology::Torus, 64);
  const auto offered = net::make_traffic(net::Traffic::Uniform, 64, 0.8, 4, 2000, 11);
  const auto on_mesh = net::simulate_network(mesh, offered);
  const auto on_torus = net::simulate_network(torus, offered);
  expect(on_torus.average_zero_load < on_mesh.average_zero_load && on_torus.accepted_throughput > on_mesh.accepted_throughput,
         "torus beats mesh in zero-load latency and saturation throughput");
  // Traffic pattern matters on the mesh: transpose concentrates on the diagonal, neighbour traffic barely conflicts.
  const auto transpose = net::simulate_network(mesh, net::make_traffic(net::Traffic::Transpose, 64, 0.3, 4, 2000, 3));
  const auto neighbor = net::simulate_network(mesh, net::make_traffic(net::Traffic::Neighbor, 64, 0.3, 4, 2000, 3));
  expect(transpose.average_latency - transpose.average_zero_load > 5.0 * (neighbor.average_latency - neighbor.average_zero_load),
         "transpose traffic suffers far more contention than neighbour traffic on the mesh");
}

}  // namespace

int main() {
  routing();
  exact_timing();
  invariants();
  saturation();
  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "network_contention_test: all checks passed\n";
  return 0;
}
