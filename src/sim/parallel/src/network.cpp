#include "nexus/sim/parallel/network.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <queue>
#include <random>
#include <tuple>

namespace nexus::sim::parallel {

namespace {

std::size_t side_of(std::size_t nodes) {
  const auto side = static_cast<std::size_t>(std::llround(std::sqrt(static_cast<double>(nodes))));
  return side * side == nodes ? side : 0;
}

std::string link(std::size_t from, std::size_t to) { return std::to_string(from) + "->" + std::to_string(to); }

}  // namespace

std::string topology_name(NetTopology topology) {
  switch (topology) {
    case NetTopology::Bus: return "bus";
    case NetTopology::Crossbar: return "crossbar";
    case NetTopology::Ring: return "ring";
    case NetTopology::Mesh: return "2-D mesh (XY routing)";
    case NetTopology::Torus: return "2-D torus (XY routing)";
    case NetTopology::Hypercube: return "hypercube (e-cube routing)";
  }
  return "?";
}

std::string traffic_name(Traffic traffic) {
  switch (traffic) {
    case Traffic::Uniform: return "uniform random";
    case Traffic::Transpose: return "transpose";
    case Traffic::BitComplement: return "bit complement";
    case Traffic::Hotspot: return "hotspot (all to node 0)";
    case Traffic::Neighbor: return "neighbor (i to i+1)";
  }
  return "?";
}

std::string validate(const ContentionConfig& config) {
  if (config.nodes < 2) {
    return "the network needs at least 2 nodes";
  }
  if (config.flits == 0) {
    return "a packet needs at least one flit";
  }
  if ((config.topology == NetTopology::Mesh || config.topology == NetTopology::Torus) && side_of(config.nodes) == 0) {
    return "a 2-D mesh or torus needs a square number of nodes";
  }
  if (config.topology == NetTopology::Hypercube && !std::has_single_bit(config.nodes)) {
    return "a hypercube needs a power-of-two number of nodes";
  }
  return "";
}

std::vector<std::string> route(const ContentionConfig& config, std::size_t source, std::size_t destination) {
  if (config.topology == NetTopology::Bus) {
    return {"bus"};
  }
  std::vector<std::string> channels{"inj " + std::to_string(source)};
  std::size_t at = source;
  switch (config.topology) {
    case NetTopology::Bus:
    case NetTopology::Crossbar:
      break;
    case NetTopology::Ring: {
      const std::size_t n = config.nodes;
      const std::size_t forward = (destination + n - source) % n;
      // a tie (destination exactly opposite) goes clockwise from even sources and anticlockwise from odd ones
      const bool clockwise = forward < n - forward || (forward == n - forward && source % 2 == 0);
      while (at != destination) {
        const std::size_t next = clockwise ? (at + 1) % n : (at + n - 1) % n;
        channels.push_back(link(at, next));
        at = next;
      }
      break;
    }
    case NetTopology::Mesh:
    case NetTopology::Torus: {
      const std::size_t k = side_of(config.nodes);
      const bool wrap = config.topology == NetTopology::Torus;
      // dimension 0 is x (id % k), dimension 1 is y (id / k)
      for (int dimension = 0; dimension < 2; ++dimension) {
        auto coordinate = [&](std::size_t id) { return dimension == 0 ? id % k : id / k; };
        auto with = [&](std::size_t id, std::size_t value) { return dimension == 0 ? id / k * k + value : value * k + id % k; };
        while (coordinate(at) != coordinate(destination)) {
          const std::size_t from = coordinate(at);
          const std::size_t to = coordinate(destination);
          std::size_t next_value = 0;
          if (wrap) {
            const std::size_t forward = (to + k - from) % k;
            // ties split by the parity of the source's coordinate, so both directions carry half of them
            const bool up = forward < k - forward || (forward == k - forward && coordinate(source) % 2 == 0);
            next_value = up ? (from + 1) % k : (from + k - 1) % k;
          } else {
            next_value = to > from ? from + 1 : from - 1;
          }
          const std::size_t next = with(at, next_value);
          channels.push_back(link(at, next));
          at = next;
        }
      }
      break;
    }
    case NetTopology::Hypercube:
      for (std::size_t bit = 1; bit < config.nodes; bit <<= 1) {
        if (((at ^ destination) & bit) != 0) {
          channels.push_back(link(at, at ^ bit));
          at ^= bit;
        }
      }
      break;
  }
  channels.push_back("ej " + std::to_string(destination));
  return channels;
}

NetworkResult simulate_network(const ContentionConfig& config, const std::vector<Packet>& packets) {
  NetworkResult result;
  result.error = validate(config);
  if (!result.error.empty()) {
    return result;
  }
  std::map<std::string, std::size_t> ids;
  std::vector<std::vector<std::size_t>> routes;
  for (const auto& packet : packets) {
    if (packet.source >= config.nodes || packet.destination >= config.nodes || packet.source == packet.destination) {
      result.error = "packet " + std::to_string(routes.size()) + " has a bad source or destination";
      return result;
    }
    std::vector<std::size_t> path;
    for (const auto& name : route(config, packet.source, packet.destination)) {
      const auto [entry, inserted] = ids.try_emplace(name, result.channels.size());
      if (inserted) {
        result.channels.push_back(ChannelLoad{name, 0, 0});
      }
      path.push_back(entry->second);
    }
    routes.push_back(std::move(path));
  }

  const std::size_t f = config.flits;
  const std::size_t step = 1 + config.router_delay;
  std::vector<std::size_t> free_at(result.channels.size(), 0);
  result.latency.assign(packets.size(), 0);
  result.zero_load.assign(packets.size(), 0);
  // (time the head requests the channel, packet, hop): popped in time order, so channels are granted first come,
  // first served, ties to the lower packet number.
  using Event = std::tuple<std::size_t, std::size_t, std::size_t>;
  std::priority_queue<Event, std::vector<Event>, std::greater<>> events;
  for (std::size_t p = 0; p < packets.size(); ++p) {
    events.emplace(packets[p].inject, p, 0);
    result.zero_load[p] = (routes[p].size() - 1) * step + f;
  }
  while (!events.empty()) {
    const auto [time, p, hop] = events.top();
    events.pop();
    const std::size_t channel = routes[p][hop];
    const std::size_t start = std::max(time, free_at[channel]);
    result.contention_cycles += start - time;
    free_at[channel] = start + f;
    result.channels[channel].busy += f;
    result.channels[channel].packets += 1;
    if (hop + 1 == routes[p].size()) {
      const std::size_t delivered = start + f;
      result.latency[p] = delivered - packets[p].inject;
      result.makespan = std::max(result.makespan, delivered);
    } else {
      events.emplace(start + step, p, hop + 1);
    }
  }

  result.packets = packets.size();
  result.flits = packets.size() * f;
  if (!packets.empty()) {
    std::size_t total = 0;
    std::size_t ideal = 0;
    for (std::size_t p = 0; p < packets.size(); ++p) {
      total += result.latency[p];
      ideal += result.zero_load[p];
      result.max_latency = std::max(result.max_latency, result.latency[p]);
    }
    result.average_latency = static_cast<double>(total) / static_cast<double>(packets.size());
    result.average_zero_load = static_cast<double>(ideal) / static_cast<double>(packets.size());
    result.accepted_throughput =
        static_cast<double>(result.flits) / static_cast<double>(config.nodes * std::max<std::size_t>(result.makespan, 1));
  }
  for (std::size_t c = 0; c < result.channels.size(); ++c) {
    if (result.channels[c].busy > result.channels[result.busiest].busy) {
      result.busiest = c;
    }
  }
  result.success = true;
  return result;
}

std::size_t pattern_destination(Traffic traffic, std::size_t nodes, std::size_t source) {
  switch (traffic) {
    case Traffic::Uniform:
      return source;
    case Traffic::Transpose: {
      const std::size_t k = side_of(nodes);
      return k == 0 ? source : (source % k) * k + source / k;
    }
    case Traffic::BitComplement:
      return nodes - 1 - source;
    case Traffic::Hotspot:
      return 0;
    case Traffic::Neighbor:
      return (source + 1) % nodes;
  }
  return source;
}

std::vector<Packet> make_traffic(Traffic traffic, std::size_t nodes, double rate, std::size_t flits, std::size_t cycles,
                                 std::uint32_t seed) {
  std::mt19937 random(seed);
  std::uniform_real_distribution<double> coin(0.0, 1.0);
  const double probability = std::clamp(rate / static_cast<double>(std::max<std::size_t>(flits, 1)), 0.0, 1.0);
  std::vector<Packet> packets;
  for (std::size_t cycle = 0; cycle < cycles; ++cycle) {
    for (std::size_t node = 0; node < nodes; ++node) {
      if (coin(random) >= probability) {
        continue;
      }
      std::size_t destination = pattern_destination(traffic, nodes, node);
      if (traffic == Traffic::Uniform) {
        destination = random() % (nodes - 1);
        destination += destination >= node ? 1 : 0;  // any node but the source
      }
      if (destination != node) {
        packets.push_back(Packet{node, destination, cycle});
      }
    }
  }
  return packets;
}

double uniform_throughput_bound(const ContentionConfig& config) {
  const auto p = static_cast<double>(config.nodes);
  const auto k = static_cast<double>(side_of(config.nodes));
  auto bisection = [&](double channels) { return std::min(1.0, 4.0 * channels * (p - 1.0) / (p * p)); };
  switch (config.topology) {
    case NetTopology::Bus: return 1.0 / p;
    case NetTopology::Crossbar: return 1.0;
    case NetTopology::Ring: return bisection(2.0);
    case NetTopology::Mesh: return bisection(k);
    case NetTopology::Torus: return bisection(2.0 * k);
    case NetTopology::Hypercube: return bisection(p / 2.0);
  }
  return 1.0;
}

}  // namespace nexus::sim::parallel
