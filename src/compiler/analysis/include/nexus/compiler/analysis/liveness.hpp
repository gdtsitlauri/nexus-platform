#pragma once

#include <string>
#include <vector>

#include "nexus/compiler/analysis/data_flow.hpp"

namespace nexus::compiler::analysis {

struct LivenessResult {
  DataFlowResult flow;
  std::vector<DataFlowSet> use_sets;
  std::vector<DataFlowSet> def_sets;
};

LivenessResult analyze_liveness(const ir::Function& function, const ControlFlowGraph& cfg);
std::string print_liveness(
    const ir::Function& function,
    const ControlFlowGraph& cfg,
    const LivenessResult& liveness);

}  // namespace nexus::compiler::analysis
