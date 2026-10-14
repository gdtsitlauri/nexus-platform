#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "nexus/vlsi/netlist.hpp"

// Timing and power analysis of a mapped netlist (NEY705).
namespace nexus::vlsi {

struct TimingOptions {
  double output_load = 4.0;      // load on every primary output, in unit-inverter input capacitances
  double wire_cap_per_fanout = 0.5;
  std::vector<double> extra_net_cap;  // optional per-net wire capacitance (e.g. from placement)
  double clock_period = 0.0;     // in tau; 0 = use the critical delay (zero worst slack)
  double input_drive = 1.0;      // primary inputs are driven by a unit inverter: arrival = input_drive * load
};

struct TimingReport {
  std::vector<double> arrival;   // per net
  std::vector<double> required;  // per net
  std::vector<double> slack;     // per net
  std::vector<double> cell_delay;
  double critical_delay = 0.0;
  double worst_slack = 0.0;
  std::vector<std::size_t> critical_path;  // cells, from input to output
};

// Static timing analysis: cell delay d = g h + p (logical effort, unit-size cells), h = C_load / g, with the load
// the sum of fanout pin capacitances plus wire capacitance. Forward arrival times, backward required times,
// slack per net and the critical path.
TimingReport static_timing(const Netlist& n, const TimingOptions& opt = {});
std::string format_timing(const Netlist& n, const TimingReport& r);

// Longest input-to-output path delay by explicit path enumeration (exponential; for checking small circuits).
double longest_path_brute_force(const Netlist& n, const TimingOptions& opt = {});

struct PowerReport {
  std::vector<double> activity_sim;   // toggles per cycle per net, from random simulation
  std::vector<double> activity_prob;  // 2 p (1 - p) from signal probabilities assuming independent inputs
  std::vector<double> capacitance;    // switched capacitance per net (fanout pins + wire + output load)
  double dynamic_sim = 0.0;           // sum alpha C (V^2 f = 1)
  double dynamic_prob = 0.0;
};

// Dynamic power alpha C V^2 f: activity from simulation of `cycles` random vectors (inputs toggle with
// probability 1/2) and from signal-probability propagation (exact for fanout-free circuits).
PowerReport power_analysis(const Netlist& n, std::size_t cycles = 4096, const TimingOptions& opt = {});

// Manufacturing test: single stuck-at faults on every net (stuck-at-0 and stuck-at-1).
struct FaultReport {
  std::size_t faults = 0;
  std::size_t detected_random = 0;     // by `random_patterns` random vectors (parallel-pattern simulation)
  std::size_t detected_atpg = 0;       // additionally by SAT-based test generation
  std::size_t redundant = 0;           // proved untestable (no input vector distinguishes the faulty circuit)
  std::vector<std::vector<bool>> atpg_vectors;  // one input vector per fault found by ATPG
  double coverage() const { return faults == 0 ? 1.0 : static_cast<double>(detected_random + detected_atpg) / static_cast<double>(faults); }
};
FaultReport fault_analysis(const Netlist& n, std::size_t random_patterns = 256);

}  // namespace nexus::vlsi
