#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "nexus/vlsi/aig.hpp"

// Standard-cell library, gate-level netlists and technology mapping (NEY705 synthesis algorithms).
namespace nexus::vlsi {

struct CellType {
  std::string name;
  std::size_t inputs = 1;
  std::uint8_t function = 0;  // truth table: bit m = output for input minterm m (input 0 = least significant bit)
  double area = 1.0;          // in transistors
  double g = 1.0;             // logical effort of each input (pin capacitance in unit-inverter inputs)
  double p = 1.0;             // parasitic delay
};

// INV, BUF, NAND2/3, NOR2/3, AND2, OR2, AOI21, OAI21, XOR2, XNOR2, MUX2-free basic library.
std::vector<CellType> default_library();

struct CellInst {
  std::size_t type = 0;               // index into the library
  std::vector<std::size_t> inputs;    // nets
  std::size_t output = 0;             // net
  std::string name;
};

struct Netlist {
  std::vector<CellType> library;
  std::vector<std::string> nets;      // net names; net 0 = constant 0, net 1 = constant 1
  std::vector<std::size_t> inputs;    // primary input nets
  std::vector<std::size_t> outputs;   // primary output nets (same order as output_names)
  std::vector<std::string> output_names;
  std::vector<CellInst> cells;        // in topological order

  std::size_t add_net(const std::string& name);
  double area() const;
  std::vector<std::uint64_t> simulate(const std::vector<std::uint64_t>& input_words) const;  // per net
  Aig to_aig() const;  // for equivalence checking against the source
  std::vector<std::vector<std::size_t>> fanouts() const;    // net -> cells reading it
};

enum class MapGoal { Area, Delay };

struct MapStats {
  std::size_t cuts_enumerated = 0;
  std::size_t matches_tried = 0;
};

// Cut-based technology mapping: 3-feasible cuts per AND node, truth-table matching of library cells under input
// permutation and input/output phase, dynamic programming on area flow (or arrival time) for both phases of every
// node, then covering from the outputs.
Netlist tech_map(const Aig& aig, const std::vector<CellType>& library, MapGoal goal = MapGoal::Area,
                 MapStats* stats = nullptr);

// Buffer insertion: every net read by more than `max_fanout` cell inputs drives a tree of BUF cells (each buffer
// drives at most `max_fanout` loads), which divides the electrical effort of a high-fanout net over several stages.
Netlist buffer_fanout(const Netlist& n, std::size_t max_fanout);

// Structural Verilog with one continuous assignment per cell.
std::string to_verilog(const Netlist& n, const std::string& module_name);

}  // namespace nexus::vlsi
