#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "nexus/compiler/analysis/cfg.hpp"

namespace nexus::compiler::analysis {

struct DominatorResult {
  std::vector<std::set<ir::BlockId>> dominator_sets;
  std::vector<std::optional<ir::BlockId>> immediate_dominators;
};

DominatorResult compute_dominators(const ControlFlowGraph& cfg);
std::string print_dominators(
    const ir::Function& function,
    const ControlFlowGraph& cfg,
    const DominatorResult& dominators);

}  // namespace nexus::compiler::analysis
