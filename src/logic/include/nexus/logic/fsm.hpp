#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "nexus/logic/minimize.hpp"

// Finite-state machines (EY121 sequential circuits; NEY604 embedded-system modelling): state diagrams and tables,
// state minimisation, state assignment, synthesis with D, T or JK flip-flops, analysis of a sequential circuit back
// into a state table, and Verilog generation.
//
// Text format (one item per line, '#' comments):
//   fsm <name> mealy|moore inputs=<bits> outputs=<bits> reset=<state>
//   <state> <input> -> <next> / <output>     (Mealy; input and output as binary strings, input may be '-')
//   <state> <input> -> <next>                 (Moore)
//   <state> / <output>                        (Moore output of a state)
namespace nexus::logic {

struct Fsm {
  std::string name = "fsm";
  bool moore = false;
  std::size_t input_bits = 1;
  std::size_t output_bits = 1;
  std::vector<std::string> states;
  std::size_t reset = 0;
  std::vector<std::vector<std::size_t>> next;      // [state][input value]
  std::vector<std::vector<std::uint32_t>> output;  // [state][input value]; Moore: equal across inputs
};

Fsm parse_fsm(const std::string& text);
std::string format_fsm(const Fsm& f);          // text format above (round-trips through parse_fsm)
std::string format_state_table(const Fsm& f);  // human-readable state table

std::vector<std::uint32_t> simulate(const Fsm& f, const std::vector<std::uint32_t>& inputs);

// Equivalence from the reset states (product-machine search); on a difference, `witness` gets an input sequence
// whose last output differs.
bool fsm_equivalent(const Fsm& a, const Fsm& b, std::vector<std::uint32_t>* witness = nullptr);

// Removes unreachable states and merges equivalent ones (partition refinement). `block_of[s]` gives the state of
// the result that state s maps to (or SIZE_MAX if s was unreachable).
Fsm minimize_fsm(const Fsm& f, std::vector<std::size_t>* block_of = nullptr);

enum class Encoding { Binary, Gray, OneHot };
enum class FlipFlop { D, T, JK };

struct SequentialCircuit {
  Encoding encoding = Encoding::Binary;
  FlipFlop flip_flop = FlipFlop::D;
  std::size_t state_bits = 0;
  std::size_t input_bits = 0;
  std::size_t output_bits = 0;
  bool moore = false;
  std::vector<std::uint32_t> codes;  // code of each FSM state
  std::uint32_t reset_code = 0;
  std::vector<std::string> vars;     // q<k-1> ... q0 x<m-1> ... x0: minterm = (code << m) | input
  // Covers over `vars`. D and T: excite[i] for state bit i (bit 0 = q0). JK: excite[2i] = J_i, excite[2i+1] = K_i.
  std::vector<std::vector<Cube>> excite;
  std::vector<std::vector<Cube>> outputs;  // output bit 0 first
  std::size_t literals() const;
  std::string equations() const;
};

std::vector<std::uint32_t> state_codes(std::size_t num_states, Encoding encoding, std::size_t* bits);

// Two-level-minimised excitation and output equations; unused state codes are don't cares. One-hot uses the
// standard one-hot equations (D flip-flops only).
SequentialCircuit synthesize(const Fsm& f, Encoding encoding, FlipFlop flip_flop);

std::uint32_t next_code(const SequentialCircuit& c, std::uint32_t code, std::uint32_t input);
std::uint32_t output_of(const SequentialCircuit& c, std::uint32_t code, std::uint32_t input);
std::vector<std::uint32_t> simulate(const SequentialCircuit& c, const std::vector<std::uint32_t>& inputs);

// Analysis: the state table of a circuit, over the codes reachable from its reset code.
Fsm analyze(const SequentialCircuit& c);

// Analysis from text equations:
//   flipflop D|T|JK
//   state q1 q0          (most significant first)
//   inputs x
//   reset 00
//   D q1 = <expr>        (T q1 = ...; J q1 = ..., K q1 = ...)
//   out z = <expr>       (one line per output bit, most significant first)
SequentialCircuit parse_circuit(const std::string& text);

std::string to_verilog_behavioural(const Fsm& f);
std::string to_verilog_structural(const SequentialCircuit& c, const std::string& module_name);

}  // namespace nexus::logic
