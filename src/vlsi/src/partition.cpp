#include <algorithm>
#include <array>
#include <limits>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <stdexcept>

#include "nexus/vlsi/physical.hpp"

namespace nexus::vlsi {

std::vector<std::vector<std::size_t>> Hypergraph::cell_nets() const {
  std::vector<std::vector<std::size_t>> out(num_cells);
  for (std::size_t n = 0; n < nets.size(); ++n) {
    for (const auto c : nets[n]) {
      out[c].push_back(n);
    }
  }
  return out;
}

Hypergraph to_hypergraph(const Netlist& n, std::vector<std::vector<std::size_t>>* pads,
                         std::vector<std::size_t>* net_ids) {
  Hypergraph h;
  h.num_cells = n.cells.size();
  h.cell_area.assign(h.num_cells, 1.0);
  std::vector<std::vector<std::size_t>> pins(n.nets.size());
  for (std::size_t i = 0; i < n.cells.size(); ++i) {
    pins[n.cells[i].output].push_back(i);
    for (const auto in : n.cells[i].inputs) {
      pins[in].push_back(i);
    }
    h.cell_area[i] = n.library[n.cells[i].type].area / 4.0;
  }
  for (auto& p : pins) {
    std::sort(p.begin(), p.end());
    p.erase(std::unique(p.begin(), p.end()), p.end());
  }
  std::set<std::size_t> io(n.inputs.begin(), n.inputs.end());
  io.insert(n.outputs.begin(), n.outputs.end());
  if (net_ids != nullptr) {
    net_ids->clear();
  }
  for (std::size_t net = 2; net < pins.size(); ++net) {
    if (pins[net].size() >= 2) {
      h.nets.push_back(pins[net]);
      if (net_ids != nullptr) {
        net_ids->push_back(net);
      }
    }
  }
  if (pads != nullptr) {
    pads->clear();
    for (const auto in : n.inputs) {
      pads->push_back(pins[in]);
    }
    for (const auto out : n.outputs) {
      pads->push_back(out > 1 ? pins[out] : std::vector<std::size_t>{});
    }
  }
  return h;
}

Hypergraph planted_hypergraph(std::size_t cells, std::size_t inside, std::size_t across, std::uint32_t seed) {
  std::mt19937 rng(seed);
  Hypergraph h;
  h.num_cells = cells;
  h.cell_area.assign(cells, 1.0);
  const std::size_t half = cells / 2;
  std::uniform_int_distribution<std::size_t> deg(2, 4);
  auto random_in = [&](std::size_t lo, std::size_t hi) {
    std::uniform_int_distribution<std::size_t> d(lo, hi - 1);
    return d(rng);
  };
  for (std::size_t k = 0; k < inside; ++k) {
    const bool left = k % 2 == 0;
    std::set<std::size_t> net;
    const std::size_t want = deg(rng);
    while (net.size() < want) {
      net.insert(left ? random_in(0, half) : random_in(half, cells));
    }
    h.nets.emplace_back(net.begin(), net.end());
  }
  for (std::size_t k = 0; k < across; ++k) {
    h.nets.push_back({random_in(0, half), random_in(half, cells)});
  }
  return h;
}

std::size_t cut_size(const Hypergraph& h, const std::vector<int>& side) {
  std::size_t cut = 0;
  for (const auto& net : h.nets) {
    bool a = false, b = false;
    for (const auto c : net) {
      (side[c] == 0 ? a : b) = true;
    }
    cut += (a && b) ? 1 : 0;
  }
  return cut;
}

std::vector<int> random_bisection(std::size_t cells, std::uint32_t seed) {
  std::vector<int> side(cells, 0);
  for (std::size_t i = cells / 2; i < cells; ++i) {
    side[i] = 1;
  }
  std::mt19937 rng(seed);
  std::shuffle(side.begin(), side.end(), rng);
  return side;
}

std::vector<int> kernighan_lin(const Hypergraph& h, std::vector<int> side, std::size_t* passes) {
  const std::size_t n = h.num_cells;
  // clique model: each net of k cells contributes 1/(k-1) between every pair
  std::vector<std::map<std::size_t, double>> w(n);
  for (const auto& net : h.nets) {
    const double wt = 1.0 / static_cast<double>(net.size() - 1);
    for (std::size_t i = 0; i < net.size(); ++i) {
      for (std::size_t j = i + 1; j < net.size(); ++j) {
        w[net[i]][net[j]] += wt;
        w[net[j]][net[i]] += wt;
      }
    }
  }
  auto weight = [&](std::size_t a, std::size_t b) {
    const auto it = w[a].find(b);
    return it == w[a].end() ? 0.0 : it->second;
  };
  std::size_t pass_count = 0;
  for (;;) {
    ++pass_count;
    // D = external - internal cost
    std::vector<double> d(n, 0.0);
    for (std::size_t a = 0; a < n; ++a) {
      for (const auto& [b, wt] : w[a]) {
        d[a] += side[a] != side[b] ? wt : -wt;
      }
    }
    std::vector<bool> locked(n, false);
    std::vector<std::pair<std::size_t, std::size_t>> swaps;
    std::vector<double> gains;
    auto s = side;
    while (true) {
      double best = -1e300;
      std::size_t ba = n, bb = n;
      for (std::size_t a = 0; a < n; ++a) {
        if (locked[a] || s[a] != 0) {
          continue;
        }
        for (std::size_t b = 0; b < n; ++b) {
          if (locked[b] || s[b] != 1) {
            continue;
          }
          const double g = d[a] + d[b] - 2.0 * weight(a, b);
          if (g > best) {
            best = g;
            ba = a;
            bb = b;
          }
        }
      }
      if (ba == n) {
        break;
      }
      locked[ba] = locked[bb] = true;
      swaps.emplace_back(ba, bb);
      gains.push_back(best);
      // update D of unlocked cells as if a and b had been swapped
      for (std::size_t x = 0; x < n; ++x) {
        if (locked[x]) {
          continue;
        }
        const double wa = weight(x, ba), wb = weight(x, bb);
        if (s[x] == 0) {
          d[x] += 2.0 * wa - 2.0 * wb;
        } else {
          d[x] += 2.0 * wb - 2.0 * wa;
        }
      }
      s[ba] = 1;
      s[bb] = 0;
    }
    // best prefix of swaps
    double acc = 0.0, best_acc = 0.0;
    std::size_t best_k = 0;
    for (std::size_t k = 0; k < gains.size(); ++k) {
      acc += gains[k];
      if (acc > best_acc + 1e-12) {
        best_acc = acc;
        best_k = k + 1;
      }
    }
    if (best_k == 0) {
      break;
    }
    for (std::size_t k = 0; k < best_k; ++k) {
      std::swap(side[swaps[k].first], side[swaps[k].second]);
    }
    if (pass_count > 50) {
      break;
    }
  }
  if (passes != nullptr) {
    *passes = pass_count;
  }
  return side;
}

std::vector<int> fiduccia_mattheyses(const Hypergraph& h, std::vector<int> side, double tolerance,
                                     std::size_t* passes) {
  const std::size_t n = h.num_cells;
  const auto cnets = h.cell_nets();
  std::vector<double> area = h.cell_area.empty() ? std::vector<double>(n, 1.0) : h.cell_area;
  double total = 0.0, max_area = 0.0;
  for (const auto a : area) {
    total += a;
    max_area = std::max(max_area, a);
  }
  const double lo = total / 2.0 - tolerance * total - max_area / 2.0;
  const double hi = total / 2.0 + tolerance * total + max_area / 2.0;
  std::size_t pass_count = 0;
  for (;;) {
    ++pass_count;
    // per net: number of cells on each side
    std::vector<std::array<int, 2>> cnt(h.nets.size(), {0, 0});
    for (std::size_t e = 0; e < h.nets.size(); ++e) {
      for (const auto c : h.nets[e]) {
        ++cnt[e][static_cast<std::size_t>(side[c])];
      }
    }
    auto gain = [&](std::size_t c, const std::vector<int>& s) {
      int g = 0;
      const auto from = static_cast<std::size_t>(s[c]);
      for (const auto e : cnets[c]) {
        if (cnt[e][from] == 1) {
          ++g;  // the move uncuts the net
        }
        if (cnt[e][1 - from] == 0) {
          --g;  // the move cuts the net
        }
      }
      return g;
    };
    double area0 = 0.0;
    for (std::size_t c = 0; c < n; ++c) {
      area0 += side[c] == 0 ? area[c] : 0.0;
    }
    auto s = side;
    std::vector<bool> locked(n, false);
    std::vector<std::size_t> moves;
    std::vector<int> gains;
    for (std::size_t step = 0; step < n; ++step) {
      int best_gain = std::numeric_limits<int>::min();
      std::size_t best_cell = n;
      for (std::size_t c = 0; c < n; ++c) {
        if (locked[c]) {
          continue;
        }
        const double new0 = s[c] == 0 ? area0 - area[c] : area0 + area[c];
        if (new0 < lo || new0 > hi) {
          continue;  // would break the balance
        }
        const int g = gain(c, s);
        if (g > best_gain) {
          best_gain = g;
          best_cell = c;
        }
      }
      if (best_cell == n) {
        break;
      }
      const auto from = static_cast<std::size_t>(s[best_cell]);
      for (const auto e : cnets[best_cell]) {
        --cnt[e][from];
        ++cnt[e][1 - from];
      }
      area0 += from == 0 ? -area[best_cell] : area[best_cell];
      s[best_cell] = 1 - s[best_cell];
      locked[best_cell] = true;
      moves.push_back(best_cell);
      gains.push_back(best_gain);
    }
    int acc = 0, best_acc = 0;
    std::size_t best_k = 0;
    for (std::size_t k = 0; k < gains.size(); ++k) {
      acc += gains[k];
      if (acc > best_acc) {
        best_acc = acc;
        best_k = k + 1;
      }
    }
    if (best_k == 0) {
      break;
    }
    for (std::size_t k = 0; k < best_k; ++k) {
      side[moves[k]] = 1 - side[moves[k]];
    }
    if (pass_count > 50) {
      break;
    }
  }
  if (passes != nullptr) {
    *passes = pass_count;
  }
  return side;
}

}  // namespace nexus::vlsi
