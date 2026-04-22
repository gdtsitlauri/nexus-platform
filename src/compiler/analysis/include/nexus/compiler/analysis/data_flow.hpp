#pragma once

#include <set>
#include <string>
#include <vector>

#include "nexus/compiler/analysis/cfg.hpp"

namespace nexus::compiler::analysis {

enum class DataFlowDirection {
  Forward,
  Backward,
};

using DataFlowEntityId = std::size_t;
using DataFlowSet = std::set<DataFlowEntityId>;

struct DataFlowProblem {
  DataFlowDirection direction = DataFlowDirection::Forward;
  DataFlowSet boundary_value;
  DataFlowSet initial_value;
  std::vector<DataFlowSet> gen_sets;
  std::vector<DataFlowSet> kill_sets;
};

struct DataFlowResult {
  std::vector<DataFlowSet> in_sets;
  std::vector<DataFlowSet> out_sets;
  std::size_t iterations = 0;
};

DataFlowResult solve_data_flow(const ControlFlowGraph& cfg, const DataFlowProblem& problem);

}  // namespace nexus::compiler::analysis
