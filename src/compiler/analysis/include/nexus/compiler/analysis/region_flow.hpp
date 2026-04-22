#pragma once

#include <set>
#include <string>
#include <vector>

#include "nexus/compiler/analysis/cfg.hpp"
#include "nexus/compiler/analysis/data_flow.hpp"

namespace nexus::compiler::analysis {

struct RegionSummary {
  std::size_t id = 0;
  bool is_loop = false;
  std::vector<ir::BlockId> blocks;
};

struct RegionFlowResult {
  std::vector<RegionSummary> regions;
  std::vector<std::vector<std::size_t>> successors;
  std::vector<DataFlowSet> use_sets;
  std::vector<DataFlowSet> def_sets;
  std::vector<DataFlowSet> in_sets;
  std::vector<DataFlowSet> out_sets;
};

RegionFlowResult analyze_region_liveness(const ir::Function& function, const ControlFlowGraph& cfg);
std::string print_region_liveness(const ir::Function& function, const RegionFlowResult& result);

}  // namespace nexus::compiler::analysis
