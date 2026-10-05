#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "nexus/compiler/analysis/cfg.hpp"
#include "nexus/compiler/analysis/dominators.hpp"
#include "nexus/compiler/ir/ir.hpp"

namespace nexus::compiler::analysis {

// Static single assignment form for the scalar locals of a function (IR values are already
// single-assignment temporaries).  Construction follows Cytron et al.: dominance frontiers
// (Cooper-Harvey-Kennedy), phi placement at the iterated dominance frontier of every definition
// site, and renaming by a walk over the dominator tree.

struct PhiNode {
  ir::LocalId local = 0;
  std::size_t version = 0;
  std::vector<std::pair<ir::BlockId, std::size_t>> arguments;  // (predecessor, incoming version)
};

struct SsaForm {
  std::vector<std::vector<ir::BlockId>> dominance_frontier;
  std::vector<std::vector<ir::BlockId>> dominator_children;
  std::vector<bool> tracked_local;                 // scalar locals that are renamed
  std::vector<std::vector<PhiNode>> phis;          // per block
  // Per block and instruction index: version read by LoadLocal / defined by StoreLocal.
  std::vector<std::vector<std::size_t>> versions;
  std::vector<std::size_t> version_count;          // per local (version 0 = value on entry)
  std::size_t phi_count = 0;
};

SsaForm build_ssa(const ir::Function& function, const ControlFlowGraph& cfg, const DominatorResult& dominators);
std::string print_ssa(const ir::Function& function, const SsaForm& ssa);

// Sparse conditional constant propagation over the SSA form (Wegman & Zadeck 1991): every SSA
// name sits in the lattice  TOP (no information yet) > constant c > BOTTOM (varies), and only
// CFG edges proven executable contribute to phi nodes, so constants flow through branches whose
// conditions are themselves constant.
struct LatticeValue {
  enum class Kind { Top, Constant, Bottom } kind = Kind::Top;
  std::int64_t value = 0;

  bool operator==(const LatticeValue& other) const {
    return kind == other.kind && (kind != Kind::Constant || value == other.value);
  }
};

struct SccpResult {
  std::vector<LatticeValue> values;                          // per IR value
  std::map<std::pair<ir::LocalId, std::size_t>, LatticeValue> local_versions;
  std::set<ir::BlockId> executable_blocks;
  std::size_t iterations = 0;
  std::size_t constant_values = 0;     // computed (non-literal) values proven constant
  std::size_t resolved_branches = 0;   // conditional branches with a constant condition
};

SccpResult run_sccp(const ir::Function& function, const ControlFlowGraph& cfg, const SsaForm& ssa);
std::string print_sccp(const ir::Function& function, const SsaForm& ssa, const SccpResult& result);

}  // namespace nexus::compiler::analysis
