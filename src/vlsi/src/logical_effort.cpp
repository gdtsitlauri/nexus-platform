#include "nexus/vlsi/logical_effort.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace nexus::vlsi {

GateEffort inverter() { return GateEffort{"inv", 1.0, 1.0}; }

GateEffort nand_gate(int n) {
  return GateEffort{"nand" + std::to_string(n), (n + 2) / 3.0, static_cast<double>(n)};
}

GateEffort nor_gate(int n) { return GateEffort{"nor" + std::to_string(n), (2 * n + 1) / 3.0, static_cast<double>(n)}; }

GateEffort lookup_gate(const std::string& name) {
  if (name == "inv" || name == "not" || name == "buf") {
    return inverter();
  }
  if (name.rfind("nand", 0) == 0) {
    return nand_gate(std::stoi(name.substr(4)));
  }
  if (name.rfind("nor", 0) == 0) {
    return nor_gate(std::stoi(name.substr(3)));
  }
  if (name == "aoi21") {
    return GateEffort{"aoi21", 2.0, 7.0 / 3.0};  // worst input (a, b): 6/3
  }
  if (name == "oai21") {
    return GateEffort{"oai21", 2.0, 7.0 / 3.0};
  }
  if (name == "xor2" || name == "xnor2") {
    return GateEffort{name, 4.0, 4.0};  // static CMOS XOR, both polarities of each input
  }
  if (name == "mux2") {
    return GateEffort{"mux2", 2.0, 4.0};  // tristate-inverter multiplexer, data input
  }
  throw std::runtime_error("unknown gate " + name);
}

double path_delay(const std::vector<Stage>& stages, const std::vector<double>& caps, double c_out) {
  double d = 0.0;
  for (std::size_t i = 0; i < stages.size(); ++i) {
    const double load = (i + 1 < stages.size() ? caps[i + 1] : c_out) * stages[i].branching;
    d += stages[i].gate.g * load / caps[i] + stages[i].gate.p;
  }
  return d;
}

PathAnalysis analyze_path(const std::vector<Stage>& stages, double c_in, double c_out) {
  if (stages.empty() || c_in <= 0.0 || c_out <= 0.0) {
    throw std::runtime_error("analyze_path: need stages and positive capacitances");
  }
  PathAnalysis a;
  for (const auto& s : stages) {
    a.G *= s.gate.g;
    a.B *= s.branching;
    a.P += s.gate.p;
  }
  a.H = c_out / c_in;
  a.F = a.G * a.B * a.H;
  const double n = static_cast<double>(stages.size());
  a.stage_effort = std::pow(a.F, 1.0 / n);
  a.min_delay = n * a.stage_effort + a.P;
  // sizes from the output backwards: C_in,i = g_i b_i C_out,i / f
  a.input_caps.assign(stages.size(), 0.0);
  double load = c_out;
  for (std::size_t i = stages.size(); i-- > 0;) {
    a.input_caps[i] = stages[i].gate.g * stages[i].branching * load / a.stage_effort;
    load = a.input_caps[i];
  }
  // best number of stages if inverters (g = 1, p = 1) may be appended: minimise N F^(1/N) + P + (N - n) p_inv
  a.best_stages = stages.size();
  a.best_delay = a.min_delay;
  for (std::size_t extra = 1; extra < 40; ++extra) {
    const double m = n + static_cast<double>(extra);
    const double d = m * std::pow(a.F, 1.0 / m) + a.P + static_cast<double>(extra);
    if (d < a.best_delay) {
      a.best_delay = d;
      a.best_stages = stages.size() + extra;
    }
  }
  return a;
}

std::vector<double> optimize_sizes_numerically(const std::vector<Stage>& stages, double c_in, double c_out) {
  std::vector<double> caps(stages.size(), c_in);
  // the first stage's input is fixed; optimise the others on a log scale by coordinate descent
  for (std::size_t i = 1; i < caps.size(); ++i) {
    caps[i] = c_in * std::pow(c_out / c_in, static_cast<double>(i) / static_cast<double>(caps.size()));
  }
  for (int sweep = 0; sweep < 400; ++sweep) {
    for (std::size_t i = 1; i < caps.size(); ++i) {
      double lo = std::log(caps[i]) - 3.0, hi = std::log(caps[i]) + 3.0;
      for (int it = 0; it < 80; ++it) {  // golden-section search in log(C)
        const double m1 = lo + (hi - lo) * 0.381966;
        const double m2 = lo + (hi - lo) * 0.618034;
        auto with = [&](double logc) {
          auto c = caps;
          c[i] = std::exp(logc);
          return path_delay(stages, c, c_out);
        };
        if (with(m1) < with(m2)) {
          hi = m2;
        } else {
          lo = m1;
        }
      }
      caps[i] = std::exp(0.5 * (lo + hi));
    }
  }
  return caps;
}

std::string format_path(const std::vector<Stage>& stages, const PathAnalysis& a) {
  std::ostringstream out;
  out << "G = " << a.G << ", B = " << a.B << ", H = " << a.H << ", F = GBH = " << a.F << ", P = " << a.P << '\n';
  out << "stage effort f = F^(1/N) = " << a.stage_effort << ", minimum delay D = N f + P = " << a.min_delay
      << " tau\n";
  out << "stage input capacitances for minimum delay:";
  for (std::size_t i = 0; i < stages.size(); ++i) {
    out << ' ' << stages[i].gate.name << ':' << a.input_caps[i];
  }
  out << "\nbest number of stages (adding inverters): " << a.best_stages << " with delay " << a.best_delay << " tau\n";
  return out.str();
}

}  // namespace nexus::vlsi
