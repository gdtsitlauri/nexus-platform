#include "nexus/compiler/analysis/data_flow.hpp"

namespace nexus::compiler::analysis {

namespace {

DataFlowSet meet_union(
    const std::vector<DataFlowSet>& sets,
    const std::vector<ir::BlockId>& neighbors,
    const DataFlowSet& boundary_value,
    bool use_boundary_on_empty) {
  if (neighbors.empty()) {
    return use_boundary_on_empty ? boundary_value : DataFlowSet{};
  }

  DataFlowSet merged;
  for (ir::BlockId block : neighbors) {
    merged.insert(sets[block].begin(), sets[block].end());
  }
  return merged;
}

DataFlowSet subtract(const DataFlowSet& lhs, const DataFlowSet& rhs) {
  DataFlowSet diff = lhs;
  for (ir::ValueId value : rhs) {
    diff.erase(value);
  }
  return diff;
}

}  // namespace

DataFlowResult solve_data_flow(const ControlFlowGraph& cfg, const DataFlowProblem& problem) {
  DataFlowResult result;
  result.in_sets.assign(cfg.successors.size(), problem.initial_value);
  result.out_sets.assign(cfg.successors.size(), problem.initial_value);

  bool changed = true;
  while (changed) {
    changed = false;
    ++result.iterations;

    for (ir::BlockId block = 0; block < cfg.successors.size(); ++block) {
      DataFlowSet old_in = result.in_sets[block];
      DataFlowSet old_out = result.out_sets[block];

      if (problem.direction == DataFlowDirection::Forward) {
        result.in_sets[block] = meet_union(
            result.out_sets,
            cfg.predecessors[block],
            problem.boundary_value,
            block == cfg.entry_block);
        result.out_sets[block] = subtract(result.in_sets[block], problem.kill_sets[block]);
        result.out_sets[block].insert(
            problem.gen_sets[block].begin(), problem.gen_sets[block].end());
      } else {
        result.out_sets[block] = meet_union(
            result.in_sets,
            cfg.successors[block],
            problem.boundary_value,
            cfg.successors[block].empty());
        result.in_sets[block] = subtract(result.out_sets[block], problem.kill_sets[block]);
        result.in_sets[block].insert(
            problem.gen_sets[block].begin(), problem.gen_sets[block].end());
      }

      if (result.in_sets[block] != old_in || result.out_sets[block] != old_out) {
        changed = true;
      }
    }
  }

  return result;
}

}  // namespace nexus::compiler::analysis
