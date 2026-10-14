#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "nexus/logic/boolean.hpp"

// And-inverter graphs (NEY705 synthesis): the internal representation of logic synthesis. Literals are
// 2 * node + complement; node 0 is constant false. Structural hashing merges identical AND nodes as they are built.
namespace nexus::vlsi {

using Lit = std::uint32_t;
constexpr Lit kLitFalse = 0;
constexpr Lit kLitTrue = 1;
inline Lit lit_not(Lit l) { return l ^ 1U; }
inline std::size_t lit_node(Lit l) { return l >> 1U; }
inline bool lit_neg(Lit l) { return (l & 1U) != 0; }

class Aig {
 public:
  Aig();
  Lit add_input(const std::string& name);
  void add_output(const std::string& name, Lit l);
  Lit and_(Lit a, Lit b);
  Lit or_(Lit a, Lit b) { return lit_not(and_(lit_not(a), lit_not(b))); }
  Lit xor_(Lit a, Lit b) { return or_(and_(a, lit_not(b)), and_(lit_not(a), b)); }
  Lit mux(Lit s, Lit t, Lit e) { return or_(and_(s, t), and_(lit_not(s), e)); }
  Lit from_expr(const nexus::logic::Expr& e, const std::vector<Lit>& var_lits);

  std::size_t num_nodes() const { return fanin0_.size(); }
  std::size_t num_ands() const;
  std::size_t num_inputs() const { return inputs_.size(); }
  bool is_and(std::size_t node) const { return node != 0 && !is_input_[node]; }
  bool is_input(std::size_t node) const { return is_input_[node]; }
  Lit fanin0(std::size_t node) const { return fanin0_[node]; }
  Lit fanin1(std::size_t node) const { return fanin1_[node]; }
  const std::vector<std::size_t>& inputs() const { return inputs_; }
  const std::vector<std::string>& input_names() const { return input_names_; }
  const std::vector<Lit>& outputs() const { return outputs_; }
  const std::vector<std::string>& output_names() const { return output_names_; }
  std::size_t depth() const;  // AND levels on the longest path

  // 64 patterns at once: one word per input, in input order; returns one word per output.
  std::vector<std::uint64_t> simulate(const std::vector<std::uint64_t>& input_words) const;
  std::vector<std::uint64_t> simulate_nodes(const std::vector<std::uint64_t>& input_words) const;

  // Rebuilds with every AND super-gate as a balanced tree (minimum depth for the given fanins).
  Aig balance() const;
  // Removes nodes not reachable from the outputs and re-hashes.
  Aig cleanup() const;

 private:
  std::vector<Lit> fanin0_, fanin1_;
  std::vector<bool> is_input_;
  std::vector<std::size_t> inputs_;
  std::vector<std::string> input_names_;
  std::vector<Lit> outputs_;
  std::vector<std::string> output_names_;
  std::map<std::pair<Lit, Lit>, Lit> strash_;
};

// Berkeley Logic Interchange Format: .model .inputs .outputs .names .latch .end. Latches are cut: a latch output
// becomes an input named after it and its data input becomes an output named "<latch>$next".
Aig read_blif(const std::string& text);
std::string write_blif(const Aig& aig, const std::string& model = "top");

struct AigEquivalence {
  bool equivalent = false;
  std::optional<std::string> mismatch;  // output name and input assignment of a counterexample
  std::size_t conflicts = 0;
};
// Combinational equivalence of two AIGs with the same input and output names: random simulation, then a SAT miter
// per output.
AigEquivalence aig_equivalent(const Aig& a, const Aig& b);

}  // namespace nexus::vlsi
