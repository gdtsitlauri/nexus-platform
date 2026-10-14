#include "nexus/vlsi/netlist.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace nexus::vlsi {

namespace {

std::uint8_t table(std::size_t k, const std::function<bool(const std::vector<bool>&)>& f) {
  std::uint8_t t = 0;
  for (std::uint32_t m = 0; m < (1U << k); ++m) {
    std::vector<bool> x(k);
    for (std::size_t j = 0; j < k; ++j) {
      x[j] = ((m >> j) & 1U) != 0;
    }
    if (f(x)) {
      t = static_cast<std::uint8_t>(t | (1U << m));
    }
  }
  return t;
}

}  // namespace

std::vector<CellType> default_library() {
  using V = const std::vector<bool>&;
  return {
      {"INV", 1, table(1, [](V x) { return !x[0]; }), 2, 1.0, 1.0},
      {"BUF", 1, table(1, [](V x) { return x[0]; }), 4, 1.0, 2.0},
      {"NAND2", 2, table(2, [](V x) { return !(x[0] && x[1]); }), 4, 4.0 / 3, 2.0},
      {"NOR2", 2, table(2, [](V x) { return !(x[0] || x[1]); }), 4, 5.0 / 3, 2.0},
      {"AND2", 2, table(2, [](V x) { return x[0] && x[1]; }), 6, 4.0 / 3, 3.0},
      {"OR2", 2, table(2, [](V x) { return x[0] || x[1]; }), 6, 5.0 / 3, 3.0},
      {"NAND3", 3, table(3, [](V x) { return !(x[0] && x[1] && x[2]); }), 6, 5.0 / 3, 3.0},
      {"NOR3", 3, table(3, [](V x) { return !(x[0] || x[1] || x[2]); }), 6, 7.0 / 3, 3.0},
      {"AOI21", 3, table(3, [](V x) { return !((x[0] && x[1]) || x[2]); }), 6, 2.0, 7.0 / 3},
      {"OAI21", 3, table(3, [](V x) { return !((x[0] || x[1]) && x[2]); }), 6, 2.0, 7.0 / 3},
      {"XOR2", 2, table(2, [](V x) { return x[0] != x[1]; }), 12, 4.0, 4.0},
      {"XNOR2", 2, table(2, [](V x) { return x[0] == x[1]; }), 12, 4.0, 4.0},
  };
}

std::size_t Netlist::add_net(const std::string& name) {
  nets.push_back(name);
  return nets.size() - 1;
}

double Netlist::area() const {
  double a = 0.0;
  for (const auto& c : cells) {
    a += library[c.type].area;
  }
  return a;
}

std::vector<std::uint64_t> Netlist::simulate(const std::vector<std::uint64_t>& in) const {
  std::vector<std::uint64_t> v(nets.size(), 0);
  v[1] = ~std::uint64_t{0};
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    v[inputs[i]] = in.at(i);
  }
  for (const auto& c : cells) {
    const CellType& t = library[c.type];
    std::uint64_t out = 0;
    for (std::uint32_t m = 0; m < (1U << t.inputs); ++m) {
      if (((t.function >> m) & 1U) == 0) {
        continue;
      }
      std::uint64_t term = ~std::uint64_t{0};
      for (std::size_t j = 0; j < t.inputs; ++j) {
        term &= ((m >> j) & 1U) != 0 ? v[c.inputs[j]] : ~v[c.inputs[j]];
      }
      out |= term;
    }
    v[c.output] = out;
  }
  return v;
}

Aig Netlist::to_aig() const {
  Aig aig;
  std::vector<Lit> lit(nets.size(), kLitFalse);
  lit[1] = kLitTrue;
  for (const auto in : inputs) {
    lit[in] = aig.add_input(nets[in]);
  }
  for (const auto& c : cells) {
    const CellType& t = library[c.type];
    Lit sum = kLitFalse;
    for (std::uint32_t m = 0; m < (1U << t.inputs); ++m) {
      if (((t.function >> m) & 1U) == 0) {
        continue;
      }
      Lit prod = kLitTrue;
      for (std::size_t j = 0; j < t.inputs; ++j) {
        prod = aig.and_(prod, ((m >> j) & 1U) != 0 ? lit[c.inputs[j]] : lit_not(lit[c.inputs[j]]));
      }
      sum = aig.or_(sum, prod);
    }
    lit[c.output] = sum;
  }
  for (std::size_t i = 0; i < outputs.size(); ++i) {
    aig.add_output(output_names[i], lit[outputs[i]]);
  }
  return aig;
}

std::vector<std::vector<std::size_t>> Netlist::fanouts() const {
  std::vector<std::vector<std::size_t>> f(nets.size());
  for (std::size_t i = 0; i < cells.size(); ++i) {
    for (const auto n : cells[i].inputs) {
      f[n].push_back(i);
    }
  }
  return f;
}

// ---------------------------------------------------------------- technology mapping

namespace {

constexpr std::uint8_t kProj[3] = {0xAA, 0xCC, 0xF0};

struct Match {
  std::size_t cell = 0;
  std::vector<std::size_t> leaves;  // per cell input: AIG node
  std::vector<int> phases;          // per cell input: 0 positive, 1 complemented
};

struct Best {
  double cost = std::numeric_limits<double>::infinity();
  double arrival = std::numeric_limits<double>::infinity();
  bool via_inverter = false;  // implemented as INV of the other phase
  bool is_input = false;
  Match match;
};

}  // namespace

Netlist tech_map(const Aig& aig, const std::vector<CellType>& library, MapGoal goal, MapStats* stats) {
  const std::size_t n_nodes = aig.num_nodes();
  std::size_t inv = library.size();
  for (std::size_t i = 0; i < library.size(); ++i) {
    if (library[i].name == "INV") {
      inv = i;
    }
  }
  if (inv == library.size()) {
    throw std::runtime_error("tech_map: the library needs an INV cell");
  }
  // fanout estimates for area flow and delay
  std::vector<double> refs(n_nodes, 0.0);
  for (std::size_t n = 1; n < n_nodes; ++n) {
    if (aig.is_and(n)) {
      refs[lit_node(aig.fanin0(n))] += 1.0;
      refs[lit_node(aig.fanin1(n))] += 1.0;
    }
  }
  for (const auto o : aig.outputs()) {
    refs[lit_node(o)] += 1.0;
  }
  // cut enumeration
  std::vector<std::vector<std::vector<std::size_t>>> cuts(n_nodes);
  for (std::size_t n = 1; n < n_nodes; ++n) {
    if (aig.is_input(n)) {
      cuts[n] = {{n}};
      continue;
    }
    std::set<std::vector<std::size_t>> found;
    for (const auto& c0 : cuts[lit_node(aig.fanin0(n))]) {
      for (const auto& c1 : cuts[lit_node(aig.fanin1(n))]) {
        std::vector<std::size_t> u;
        std::set_union(c0.begin(), c0.end(), c1.begin(), c1.end(), std::back_inserter(u));
        if (u.size() <= 3) {
          found.insert(u);
        }
      }
    }
    std::vector<std::vector<std::size_t>> list(found.begin(), found.end());
    // drop dominated cuts (supersets of another cut)
    std::vector<std::vector<std::size_t>> kept;
    for (const auto& c : list) {
      const bool dominated = std::any_of(list.begin(), list.end(), [&](const std::vector<std::size_t>& o) {
        return o.size() < c.size() && std::includes(c.begin(), c.end(), o.begin(), o.end());
      });
      if (!dominated) {
        kept.push_back(c);
      }
    }
    std::sort(kept.begin(), kept.end(), [](const auto& a, const auto& b) { return a.size() < b.size(); });
    if (kept.size() > 16) {
      kept.resize(16);
    }
    kept.push_back({n});  // trivial cut, used by the fanouts
    cuts[n] = kept;
    if (stats != nullptr) {
      stats->cuts_enumerated += kept.size();
    }
  }
  // truth table of node n over a cut
  auto cut_table = [&](std::size_t root, const std::vector<std::size_t>& leaves) {
    std::map<std::size_t, std::uint8_t> memo;
    for (std::size_t i = 0; i < leaves.size(); ++i) {
      memo[leaves[i]] = kProj[i];
    }
    std::function<std::uint8_t(std::size_t)> tt = [&](std::size_t x) -> std::uint8_t {
      const auto it = memo.find(x);
      if (it != memo.end()) {
        return it->second;
      }
      if (x == 0) {
        return 0;
      }
      auto side = [&](Lit l) {
        const std::uint8_t v = tt(lit_node(l));
        return lit_neg(l) ? static_cast<std::uint8_t>(~v) : v;
      };
      const auto v = static_cast<std::uint8_t>(side(aig.fanin0(x)) & side(aig.fanin1(x)));
      memo[x] = v;
      return v;
    };
    return tt(root);
  };
  // permutations of up to 3 cell inputs
  auto permutations = [](std::size_t k) {
    std::vector<std::vector<std::size_t>> out;
    std::vector<std::size_t> p(k);
    for (std::size_t i = 0; i < k; ++i) {
      p[i] = i;
    }
    do {
      out.push_back(p);
    } while (std::next_permutation(p.begin(), p.end()));
    return out;
  };

  std::vector<std::array<Best, 2>> best(n_nodes);
  const double inv_area = library[inv].area;
  const double inv_delay = library[inv].p + library[inv].g;
  auto relax_inverters = [&](std::size_t n) {
    for (int ph = 0; ph < 2; ++ph) {
      const Best& other = best[n][static_cast<std::size_t>(1 - ph)];
      Best& mine = best[n][static_cast<std::size_t>(ph)];
      const double c = other.cost + inv_area;
      const double a = other.arrival + inv_delay;
      const bool better = goal == MapGoal::Area ? (c < mine.cost - 1e-9 || (c < mine.cost + 1e-9 && a < mine.arrival))
                                                : (a < mine.arrival - 1e-9 || (a < mine.arrival + 1e-9 && c < mine.cost));
      if (better && !other.via_inverter) {
        mine = Best{c, a, true, false, {}};
      }
    }
  };
  for (std::size_t n = 1; n < n_nodes; ++n) {
    if (aig.is_input(n)) {
      best[n][0] = Best{0.0, 0.0, false, true, {}};
      relax_inverters(n);
      continue;
    }
    for (const auto& cut : cuts[n]) {
      if (cut.size() == 1 && cut[0] == n) {
        continue;
      }
      const std::uint8_t target = cut_table(n, cut);
      for (std::size_t ci = 0; ci < library.size(); ++ci) {
        const CellType& cell = library[ci];
        if (cell.inputs != cut.size()) {
          continue;
        }
        for (const auto& perm : permutations(cell.inputs)) {
          for (std::uint32_t phase = 0; phase < (1U << cell.inputs); ++phase) {
            if (stats != nullptr) {
              ++stats->matches_tried;
            }
            std::uint8_t f = 0;
            for (std::uint32_t m = 0; m < 8; ++m) {
              std::uint32_t idx = 0;
              for (std::size_t j = 0; j < cell.inputs; ++j) {
                const std::uint32_t xj = ((m >> perm[j]) & 1U) ^ ((phase >> j) & 1U);
                idx |= xj << j;
              }
              if (((cell.function >> idx) & 1U) != 0) {
                f = static_cast<std::uint8_t>(f | (1U << m));
              }
            }
            int out_phase = -1;
            if (f == target) {
              out_phase = 0;
            } else if (static_cast<std::uint8_t>(~f) == target) {
              out_phase = 1;  // the cell computes the complement of the node
            }
            if (out_phase < 0) {
              continue;
            }
            Match mt;
            mt.cell = ci;
            double cost = cell.area;
            double arrival = 0.0;
            for (std::size_t j = 0; j < cell.inputs; ++j) {
              const std::size_t leaf = cut[perm[j]];
              const int ph = static_cast<int>((phase >> j) & 1U);
              mt.leaves.push_back(leaf);
              mt.phases.push_back(ph);
              const Best& b = best[leaf][static_cast<std::size_t>(ph)];
              cost += b.cost / std::max(1.0, refs[leaf]);
              arrival = std::max(arrival, b.arrival);
            }
            arrival += cell.p + cell.g * std::max(1.0, refs[n]);
            Best& mine = best[n][static_cast<std::size_t>(out_phase)];
            const bool better = goal == MapGoal::Area
                                    ? (cost < mine.cost - 1e-9 || (cost < mine.cost + 1e-9 && arrival < mine.arrival))
                                    : (arrival < mine.arrival - 1e-9 ||
                                       (arrival < mine.arrival + 1e-9 && cost < mine.cost));
            if (better) {
              mine = Best{cost, arrival, false, false, mt};
            }
          }
        }
      }
    }
    relax_inverters(n);
  }

  // cover from the outputs
  Netlist net;
  net.library = library;
  net.add_net("1'b0");
  net.add_net("1'b1");
  std::vector<std::size_t> input_net(n_nodes, 0);
  for (std::size_t i = 0; i < aig.inputs().size(); ++i) {
    input_net[aig.inputs()[i]] = net.add_net(aig.input_names()[i]);
    net.inputs.push_back(input_net[aig.inputs()[i]]);
  }
  std::map<std::pair<std::size_t, int>, std::size_t> made;
  std::function<std::size_t(std::size_t, int)> implement = [&](std::size_t n, int ph) -> std::size_t {
    if (n == 0) {
      return ph == 0 ? 0 : 1;
    }
    const auto key = std::make_pair(n, ph);
    const auto it = made.find(key);
    if (it != made.end()) {
      return it->second;
    }
    const Best& b = best[n][static_cast<std::size_t>(ph)];
    std::size_t out = 0;
    if (b.is_input) {
      out = input_net[n];
    } else if (b.via_inverter) {
      const std::size_t in = implement(n, 1 - ph);
      out = net.add_net("n" + std::to_string(n) + (ph != 0 ? "_n" : "_p"));
      net.cells.push_back(CellInst{inv, {in}, out, "u" + std::to_string(net.cells.size())});
    } else {
      if (!std::isfinite(b.cost)) {
        throw std::runtime_error("tech_map: no match for a node (library not complete)");
      }
      std::vector<std::size_t> ins;
      for (std::size_t j = 0; j < b.match.leaves.size(); ++j) {
        ins.push_back(implement(b.match.leaves[j], b.match.phases[j]));
      }
      out = net.add_net("n" + std::to_string(n) + (ph != 0 ? "_n" : "_p"));
      net.cells.push_back(CellInst{b.match.cell, ins, out, "u" + std::to_string(net.cells.size())});
    }
    made[key] = out;
    return out;
  };
  for (std::size_t i = 0; i < aig.outputs().size(); ++i) {
    const Lit o = aig.outputs()[i];
    net.outputs.push_back(implement(lit_node(o), lit_neg(o) ? 1 : 0));
    net.output_names.push_back(aig.output_names()[i]);
  }
  return net;
}

Netlist buffer_fanout(const Netlist& n, std::size_t max_fanout) {
  if (max_fanout < 2) {
    throw std::runtime_error("buffer_fanout: max_fanout must be at least 2");
  }
  std::size_t buf = n.library.size();
  for (std::size_t i = 0; i < n.library.size(); ++i) {
    if (n.library[i].name == "BUF") {
      buf = i;
    }
  }
  if (buf == n.library.size()) {
    throw std::runtime_error("buffer_fanout: the library needs a BUF cell");
  }
  Netlist out = n;
  out.cells.clear();
  // sinks of every net: (original cell, input position)
  std::vector<std::vector<std::pair<std::size_t, std::size_t>>> sinks(n.nets.size());
  for (std::size_t i = 0; i < n.cells.size(); ++i) {
    for (std::size_t j = 0; j < n.cells[i].inputs.size(); ++j) {
      sinks[n.cells[i].inputs[j]].emplace_back(i, j);
    }
  }
  std::map<std::pair<std::size_t, std::size_t>, std::size_t> remap;  // original pin -> net it now reads
  // builds the buffer tree of `src` for `pins` (each pin is "set net" callback) and emits the buffers
  std::function<void(std::size_t, std::vector<std::function<void(std::size_t)>>)> tree =
      [&](std::size_t src, std::vector<std::function<void(std::size_t)>> pins) {
        if (pins.size() <= max_fanout) {
          for (auto& set : pins) {
            set(src);
          }
          return;
        }
        const std::size_t groups = (pins.size() + max_fanout - 1) / max_fanout;
        std::vector<CellInst> level;
        std::vector<std::function<void(std::size_t)>> inputs;
        for (std::size_t g = 0; g < groups; ++g) {
          const std::size_t net = out.add_net(n.nets[src] + "_buf" + std::to_string(out.nets.size()));
          level.push_back(CellInst{buf, {src}, net, "b" + std::to_string(out.nets.size())});
          for (std::size_t k = g * max_fanout; k < std::min(pins.size(), (g + 1) * max_fanout); ++k) {
            pins[k](net);
          }
        }
        // the buffers' own inputs form the next level up
        const std::size_t first = out.cells.size();
        std::vector<std::size_t> idx;
        for (std::size_t g = 0; g < groups; ++g) {
          idx.push_back(g);
        }
        std::vector<CellInst> pending = level;
        for (std::size_t g = 0; g < groups; ++g) {
          inputs.push_back([&pending, g](std::size_t net) { pending[g].inputs[0] = net; });
        }
        tree(src, inputs);  // emits the upper levels first
        (void)first;
        for (auto& c : pending) {
          out.cells.push_back(c);
        }
      };
  auto buffer_net = [&](std::size_t net) {
    if (sinks[net].size() <= max_fanout) {
      return;
    }
    std::vector<std::function<void(std::size_t)>> pins;
    for (const auto& pin : sinks[net]) {
      pins.push_back([&remap, pin](std::size_t to) { remap[pin] = to; });
    }
    tree(net, pins);
  };
  for (const auto in : n.inputs) {
    buffer_net(in);
  }
  for (std::size_t i = 0; i < n.cells.size(); ++i) {
    CellInst c = n.cells[i];
    for (std::size_t j = 0; j < c.inputs.size(); ++j) {
      const auto it = remap.find({i, j});
      if (it != remap.end()) {
        c.inputs[j] = it->second;
      }
    }
    out.cells.push_back(c);
    buffer_net(c.output);
  }
  return out;
}

namespace {

std::string esc(const std::string& name) {
  bool simple = !name.empty() && (std::isalpha(static_cast<unsigned char>(name[0])) != 0 || name[0] == '_');
  for (const char c : name) {
    simple = simple && (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$');
  }
  return simple ? name : "\\" + name + " ";
}

}  // namespace

std::string to_verilog(const Netlist& n, const std::string& module_name) {
  std::ostringstream v;
  auto net_name = [&](std::size_t id) -> std::string {
    if (id == 0) {
      return "1'b0";
    }
    if (id == 1) {
      return "1'b1";
    }
    return esc(n.nets[id]);
  };
  v << "// generated by nexus-eda tech_map: " << n.cells.size() << " cells, area " << n.area() << " transistors\n";
  v << "module " << module_name << "(";
  bool first = true;
  for (const auto in : n.inputs) {
    v << (first ? "" : ", ") << "input wire " << esc(n.nets[in]);
    first = false;
  }
  for (const auto& o : n.output_names) {
    v << (first ? "" : ", ") << "output wire " << esc(o);
    first = false;
  }
  v << ");\n";
  for (const auto& c : n.cells) {
    v << "  wire " << net_name(c.output) << ";\n";
  }
  for (const auto& c : n.cells) {
    const CellType& t = n.library[c.type];
    std::string expr;
    for (std::uint32_t m = 0; m < (1U << t.inputs); ++m) {
      if (((t.function >> m) & 1U) == 0) {
        continue;
      }
      std::string term;
      for (std::size_t j = 0; j < t.inputs; ++j) {
        term += (j > 0 ? " & " : "") + std::string(((m >> j) & 1U) != 0 ? "" : "~") + net_name(c.inputs[j]);
      }
      expr += (expr.empty() ? "" : " | ") + ("(" + term + ")");
    }
    v << "  assign " << net_name(c.output) << " = " << (expr.empty() ? "1'b0" : expr) << ";  // " << t.name << ' '
      << c.name << '\n';
  }
  for (std::size_t i = 0; i < n.outputs.size(); ++i) {
    v << "  assign " << esc(n.output_names[i]) << " = " << net_name(n.outputs[i]) << ";\n";
  }
  v << "endmodule\n";
  return v.str();
}

}  // namespace nexus::vlsi
