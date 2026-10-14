#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Circuit delay estimation with the method of logical effort (NEY705), after Sutherland, Sproull and Harris.
// Delay of a stage in units of tau: d = g h + p, with logical effort g, electrical effort h = C_out / C_in and
// parasitic delay p. A path with path effort F = G B H and N stages has minimum delay N F^(1/N) + P, reached when
// every stage bears the same effort f = F^(1/N).
namespace nexus::vlsi {

struct GateEffort {
  std::string name;
  double g = 1.0;  // logical effort of an input
  double p = 1.0;  // parasitic delay
};

// Textbook values with pMOS twice as wide as nMOS: NAND-n g = (n+2)/3, p = n; NOR-n g = (2n+1)/3, p = n.
GateEffort inverter();
GateEffort nand_gate(int inputs);
GateEffort nor_gate(int inputs);
GateEffort lookup_gate(const std::string& name);  // "inv", "nand2", "nor3", "aoi21", "xor2", ...

struct Stage {
  GateEffort gate;
  double branching = 1.0;  // b: (on-path + off-path load) / on-path load at the stage output
};

struct PathAnalysis {
  double G = 1.0, B = 1.0, H = 1.0, F = 1.0, P = 0.0;
  double stage_effort = 1.0;          // f = F^(1/N)
  double min_delay = 0.0;             // N f + P
  std::vector<double> input_caps;     // C_in of every stage for minimum delay (the first equals the path C_in)
  std::size_t best_stages = 1;        // N that minimises delay when inverters may be added (stage effort ~ 4)
  double best_delay = 0.0;            // delay with best_stages
};

PathAnalysis analyze_path(const std::vector<Stage>& stages, double c_in, double c_out);

// Delay of the path for given stage input capacitances (stage i drives stage i+1's input times its branching).
double path_delay(const std::vector<Stage>& stages, const std::vector<double>& input_caps, double c_out);

// Independent numerical optimum (coordinate descent on the stage sizes) for checking the closed form.
std::vector<double> optimize_sizes_numerically(const std::vector<Stage>& stages, double c_in, double c_out);

std::string format_path(const std::vector<Stage>& stages, const PathAnalysis& a);

}  // namespace nexus::vlsi
