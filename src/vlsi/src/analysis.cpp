#include "nexus/vlsi/analysis.hpp"

#include "nexus/logic/sat.hpp"

#include <algorithm>
#include <bit>
#include <functional>
#include <limits>
#include <random>
#include <sstream>

namespace nexus::vlsi {

namespace {

std::vector<double> net_load(const Netlist& n, const TimingOptions& opt) {
  std::vector<double> load(n.nets.size(), 0.0);
  for (const auto& c : n.cells) {
    for (const auto in : c.inputs) {
      load[in] += n.library[c.type].g + opt.wire_cap_per_fanout;
    }
  }
  for (const auto o : n.outputs) {
    load[o] += opt.output_load;
  }
  for (std::size_t i = 0; i < opt.extra_net_cap.size() && i < load.size(); ++i) {
    load[i] += opt.extra_net_cap[i];
  }
  return load;
}

double delay_of(const CellType& t, double load) {
  // unit-size cell: input capacitance g, so h = load / g and d = g h + p = load + p
  return t.g * (load / t.g) + t.p;
}

}  // namespace

TimingReport static_timing(const Netlist& n, const TimingOptions& opt) {
  TimingReport r;
  const auto load = net_load(n, opt);
  r.arrival.assign(n.nets.size(), 0.0);
  for (const auto in : n.inputs) {
    r.arrival[in] = opt.input_drive * load[in];
  }
  r.cell_delay.assign(n.cells.size(), 0.0);
  std::vector<std::size_t> driver(n.nets.size(), n.cells.size());
  for (std::size_t i = 0; i < n.cells.size(); ++i) {
    const auto& c = n.cells[i];
    double in_arrival = 0.0;
    for (const auto in : c.inputs) {
      in_arrival = std::max(in_arrival, r.arrival[in]);
    }
    r.cell_delay[i] = delay_of(n.library[c.type], load[c.output]);
    r.arrival[c.output] = in_arrival + r.cell_delay[i];
    driver[c.output] = i;
  }
  for (const auto o : n.outputs) {
    r.critical_delay = std::max(r.critical_delay, r.arrival[o]);
  }
  const double period = opt.clock_period > 0.0 ? opt.clock_period : r.critical_delay;
  r.required.assign(n.nets.size(), std::numeric_limits<double>::infinity());
  for (const auto o : n.outputs) {
    r.required[o] = period;
  }
  for (std::size_t i = n.cells.size(); i-- > 0;) {
    const auto& c = n.cells[i];
    const double req_in = r.required[c.output] - r.cell_delay[i];
    for (const auto in : c.inputs) {
      r.required[in] = std::min(r.required[in], req_in);
    }
  }
  r.slack.assign(n.nets.size(), 0.0);
  r.worst_slack = std::numeric_limits<double>::infinity();
  for (std::size_t net = 0; net < n.nets.size(); ++net) {
    r.slack[net] = r.required[net] - r.arrival[net];
  }
  for (const auto o : n.outputs) {
    r.worst_slack = std::min(r.worst_slack, r.slack[o]);
  }
  // trace the critical path back from the latest output
  std::size_t net = n.outputs.empty() ? 0 : n.outputs[0];
  for (const auto o : n.outputs) {
    if (r.arrival[o] > r.arrival[net]) {
      net = o;
    }
  }
  while (driver[net] < n.cells.size()) {
    const std::size_t ci = driver[net];
    r.critical_path.push_back(ci);
    std::size_t next = n.cells[ci].inputs.empty() ? 0 : n.cells[ci].inputs[0];
    for (const auto in : n.cells[ci].inputs) {
      if (r.arrival[in] > r.arrival[next]) {
        next = in;
      }
    }
    net = next;
  }
  std::reverse(r.critical_path.begin(), r.critical_path.end());
  return r;
}

std::string format_timing(const Netlist& n, const TimingReport& r) {
  std::ostringstream out;
  out << "critical delay: " << r.critical_delay << " tau, worst slack: " << r.worst_slack << " tau\n";
  out << "critical path (" << r.critical_path.size() << " cells):\n";
  for (std::size_t k = 0; k < r.critical_path.size(); ++k) {
    if (r.critical_path.size() > 24 && k == 10) {
      out << "  ... " << r.critical_path.size() - 20 << " more cells ...\n";
      k = r.critical_path.size() - 10;
    }
    const auto ci = r.critical_path[k];
    const auto& c = n.cells[ci];
    out << "  " << n.library[c.type].name << ' ' << c.name << "  delay " << r.cell_delay[ci] << "  arrival "
        << r.arrival[c.output] << "  slack " << r.slack[c.output] << '\n';
  }
  return out.str();
}

double longest_path_brute_force(const Netlist& n, const TimingOptions& opt) {
  const auto load = net_load(n, opt);
  std::vector<std::size_t> driver(n.nets.size(), n.cells.size());
  for (std::size_t i = 0; i < n.cells.size(); ++i) {
    driver[n.cells[i].output] = i;
  }
  double best = 0.0;
  // enumerate every path from each output back to the inputs
  std::function<void(std::size_t, double)> walk = [&](std::size_t net, double acc) {
    const std::size_t ci = driver[net];
    if (ci == n.cells.size()) {
      const bool primary = std::find(n.inputs.begin(), n.inputs.end(), net) != n.inputs.end();
      best = std::max(best, acc + (primary ? opt.input_drive * load[net] : 0.0));
      return;
    }
    const double d = delay_of(n.library[n.cells[ci].type], load[net]);
    for (const auto in : n.cells[ci].inputs) {
      walk(in, acc + d);
    }
  };
  for (const auto o : n.outputs) {
    walk(o, 0.0);
  }
  return best;
}

PowerReport power_analysis(const Netlist& n, std::size_t cycles, const TimingOptions& opt) {
  PowerReport r;
  r.capacitance = net_load(n, opt);
  // simulation: consecutive random vectors, count toggles per net
  std::mt19937_64 rng(604705);
  std::vector<double> toggles(n.nets.size(), 0.0);
  std::vector<std::uint64_t> prev_last(n.nets.size(), 0);
  bool have_prev = false;
  const std::size_t words = std::max<std::size_t>(1, cycles / 64);
  for (std::size_t w = 0; w < words; ++w) {
    std::vector<std::uint64_t> in(n.inputs.size());
    for (auto& x : in) {
      x = rng();
    }
    const auto v = n.simulate(in);
    for (std::size_t net = 0; net < v.size(); ++net) {
      // toggles between bit k and k+1 within the word, and across words
      const std::uint64_t shifted = (v[net] << 1U) | (have_prev ? (prev_last[net] >> 63U) : (v[net] & 1U));
      toggles[net] += static_cast<double>(std::popcount(v[net] ^ shifted));
      prev_last[net] = v[net];
    }
    have_prev = true;
  }
  const double total = static_cast<double>(words * 64);
  r.activity_sim.assign(n.nets.size(), 0.0);
  for (std::size_t net = 0; net < n.nets.size(); ++net) {
    r.activity_sim[net] = toggles[net] / total;
  }
  // probability propagation
  std::vector<double> p(n.nets.size(), 0.5);
  p[0] = 0.0;
  p[1] = 1.0;
  for (const auto& c : n.cells) {
    const CellType& t = n.library[c.type];
    double prob = 0.0;
    for (std::uint32_t m = 0; m < (1U << t.inputs); ++m) {
      if (((t.function >> m) & 1U) == 0) {
        continue;
      }
      double term = 1.0;
      for (std::size_t j = 0; j < t.inputs; ++j) {
        term *= ((m >> j) & 1U) != 0 ? p[c.inputs[j]] : 1.0 - p[c.inputs[j]];
      }
      prob += term;
    }
    p[c.output] = prob;
  }
  r.activity_prob.assign(n.nets.size(), 0.0);
  for (std::size_t net = 0; net < n.nets.size(); ++net) {
    r.activity_prob[net] = 2.0 * p[net] * (1.0 - p[net]);
    r.dynamic_sim += r.activity_sim[net] * r.capacitance[net];
    r.dynamic_prob += r.activity_prob[net] * r.capacitance[net];
  }
  return r;
}

namespace {

// simulation with one net forced to a constant (the fault); 64 patterns at once
std::vector<std::uint64_t> simulate_with_fault(const Netlist& n, const std::vector<std::uint64_t>& in, std::size_t net,
                                               bool stuck) {
  std::vector<std::uint64_t> v(n.nets.size(), 0);
  v[1] = ~std::uint64_t{0};
  const std::uint64_t forced = stuck ? ~std::uint64_t{0} : 0;
  for (std::size_t i = 0; i < n.inputs.size(); ++i) {
    v[n.inputs[i]] = in[i];
  }
  if (net < v.size()) {
    v[net] = forced;
  }
  for (const auto& c : n.cells) {
    const CellType& t = n.library[c.type];
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
    v[c.output] = c.output == net ? forced : out;
  }
  return v;
}

}  // namespace

FaultReport fault_analysis(const Netlist& n, std::size_t random_patterns) {
  FaultReport r;
  std::vector<std::pair<std::size_t, bool>> faults;
  for (std::size_t net = 2; net < n.nets.size(); ++net) {
    faults.emplace_back(net, false);
    faults.emplace_back(net, true);
  }
  r.faults = faults.size();
  std::vector<bool> detected(faults.size(), false);
  std::mt19937_64 rng(705604);
  for (std::size_t round = 0; round < (random_patterns + 63) / 64; ++round) {
    const std::size_t left = random_patterns - round * 64;
    const std::uint64_t valid = left >= 64 ? ~std::uint64_t{0} : ((std::uint64_t{1} << left) - 1);
    std::vector<std::uint64_t> in(n.inputs.size());
    for (auto& x : in) {
      x = rng();
    }
    const auto good = n.simulate(in);
    for (std::size_t f = 0; f < faults.size(); ++f) {
      if (detected[f]) {
        continue;
      }
      const auto bad = simulate_with_fault(n, in, faults[f].first, faults[f].second);
      for (const auto o : n.outputs) {
        if (((good[o] ^ bad[o]) & valid) != 0) {
          detected[f] = true;
          ++r.detected_random;
          break;
        }
      }
    }
  }
  // SAT-based ATPG for the rest: a miter between the good circuit and the circuit with the fault
  const Aig good_aig = n.to_aig();
  for (std::size_t f = 0; f < faults.size(); ++f) {
    if (detected[f]) {
      continue;
    }
    // faulty copy: replace the faulty net by a constant
    Netlist faulty = n;
    const std::size_t net = faults[f].first;
    const std::size_t constant = faults[f].second ? 1 : 0;
    for (auto& c : faulty.cells) {
      for (auto& in : c.inputs) {
        if (in == net) in = constant;
      }
    }
    for (auto& o : faulty.outputs) {
      if (o == net) o = constant;
    }
    const auto eq = aig_equivalent(good_aig, faulty.to_aig());
    if (eq.equivalent) {
      ++r.redundant;
      continue;
    }
    ++r.detected_atpg;
    // the counterexample is printed as "name=v"; recover the vector in input order
    std::vector<bool> vec;
    for (const auto in : n.inputs) {
      const std::string key = " " + n.nets[in] + "=";
      const auto pos = eq.mismatch->find(key);
      vec.push_back(pos != std::string::npos && (*eq.mismatch)[pos + key.size()] == '1');
    }
    r.atpg_vectors.push_back(vec);
  }
  return r;
}

}  // namespace nexus::vlsi
