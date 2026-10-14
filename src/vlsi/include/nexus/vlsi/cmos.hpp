#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "nexus/logic/boolean.hpp"

// CMOS logic at the transistor level (NEY705: CMOS logic, gates and memory elements, layout).
//
// 1. A switch-level simulator in the style of MOSSIM: nodes carry 0/1/X, transistors are switches controlled by
//    their gate node, and every node takes the value of the strongest source that reaches it through conducting
//    transistors (supply and driven inputs > normal transistors > weak transistors > stored charge). This models
//    static CMOS gates, transmission gates, dynamic (charge-storage) latches and the 6T SRAM cell, whose write works
//    because the access transistor is stronger than the weak pull-up.
// 2. Static CMOS complex gates generated from a pull-down expression, with dual pull-up network, transistor sizing
//    for unit-inverter drive, logical effort and parasitic delay per input, and an Euler-path transistor ordering.
// 3. A lambda-rule stick layout generator and a design-rule checker.
namespace nexus::vlsi {

enum class Logic : std::uint8_t { Zero = 0, One = 1, X = 2 };

class SwitchNetwork {
 public:
  // Strengths: supplies and inputs 4, normal transistors 3, weak transistors 2, stored charge 1.
  std::size_t node(const std::string& name);
  std::size_t find(const std::string& name) const;
  void nmos(const std::string& gate, const std::string& a, const std::string& b, int strength = 3);
  void pmos(const std::string& gate, const std::string& a, const std::string& b, int strength = 3);
  void set_input(const std::string& name, Logic v);   // driven node (strength 4)
  void release(const std::string& name);              // stop driving: the node keeps its charge
  // Settles the network; returns false if it does not settle (oscillation).
  bool settle(std::size_t max_iterations = 200);
  Logic value(const std::string& name) const;
  std::size_t transistor_count() const { return transistors_.size(); }

 private:
  struct Transistor {
    bool p = false;
    std::size_t gate, a, b;
    int strength = 3;
  };
  std::vector<std::string> names_;
  std::vector<Logic> values_;
  std::vector<int> drive_;  // 0 = not driven
  std::vector<Logic> driven_value_;
  std::vector<Transistor> transistors_;
};

// Series-parallel switch network (the pull-down of a static CMOS gate).
struct SpNode {
  enum class Kind { Leaf, Series, Parallel };
  Kind kind = Kind::Leaf;
  std::string input;  // leaf: gate input name ('x' or "x'" for a complemented input)
  std::vector<SpNode> kids;
};

struct SizedTransistor {
  std::string input;
  bool p = false;
  double width = 1.0;  // in units of the unit nMOS width
  bool at_output = false;
};

struct CmosGate {
  std::string name;
  std::vector<std::string> inputs;
  SpNode pull_down;
  SpNode pull_up;  // the dual network
  std::vector<SizedTransistor> transistors;
  // logical effort per input and parasitic delay, for unit-inverter-equivalent drive (pMOS twice as wide)
  std::vector<double> logical_effort;
  double parasitic = 0.0;
};

// Builds the gate that computes NOT(f), where f is an AND/OR expression of (possibly complemented) inputs; e.g.
// "a b + c" gives AOI21. Throws for XOR or nested inversions.
CmosGate cmos_gate(const std::string& pull_down_expr, const std::string& name = "gate");

// Switch-level model of the gate (inputs plus complemented inputs, output "Y"), for verification.
SwitchNetwork to_switch_network(const CmosGate& g);

// For every input vector: exactly one of the networks conducts (no floating output, no short) and the output equals
// NOT(f). Returns the number of failing vectors.
std::size_t verify_complementary(const CmosGate& g);

// Euler-path layout ordering: an ordering of the inputs such that consecutive transistors share a diffusion node
// in both networks, so each network is one unbroken diffusion strip. Returns the ordering with the fewest
// diffusion breaks found by exhaustive search (inputs <= 8).
struct EulerOrdering {
  std::vector<std::string> order;
  std::size_t breaks_pull_down = 0;
  std::size_t breaks_pull_up = 0;
};
EulerOrdering euler_ordering(const CmosGate& g);
std::string stick_diagram(const CmosGate& g, const EulerOrdering& e);

// Lambda-based layout of rectangles on layers, and a design-rule checker.
enum class Layer { NDiff, PDiff, Poly, Metal1, Contact };
struct Rect {
  Layer layer;
  int x0, y0, x1, y1;  // half-open, in lambda
  std::string net;
};
struct DrcRules {
  int poly_width = 2, diff_width = 3, metal_width = 3, contact_size = 2;
  int poly_space = 2, diff_space = 3, metal_space = 3;
  int poly_overhang = 2;       // poly beyond diffusion
  int contact_enclosure = 1;   // diffusion or metal around a contact
};
struct DrcViolation {
  std::string rule;
  std::size_t a, b;
};
std::vector<Rect> layout_cell(const CmosGate& g, const EulerOrdering& e, const DrcRules& rules = {});
std::vector<DrcViolation> drc(const std::vector<Rect>& rects, const DrcRules& rules = {});
int cell_width(const std::vector<Rect>& rects);

}  // namespace nexus::vlsi
