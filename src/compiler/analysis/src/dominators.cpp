#include "nexus/compiler/analysis/dominators.hpp"

#include <algorithm>
#include <sstream>

namespace nexus::compiler::analysis {

namespace {

std::set<ir::BlockId> intersect_sets(
    const std::vector<std::set<ir::BlockId>>& sets,
    const std::vector<ir::BlockId>& block_ids) {
  if (block_ids.empty()) {
    return {};
  }

  std::set<ir::BlockId> intersection = sets[block_ids.front()];
  for (std::size_t index = 1; index < block_ids.size(); ++index) {
    std::set<ir::BlockId> next;
    std::set_intersection(
        intersection.begin(),
        intersection.end(),
        sets[block_ids[index]].begin(),
        sets[block_ids[index]].end(),
        std::inserter(next, next.begin()));
    intersection = std::move(next);
  }
  return intersection;
}

std::string format_dom_set(const ir::Function& function, const std::set<ir::BlockId>& blocks) {
  std::ostringstream output;
  output << '{';
  std::size_t index = 0;
  for (ir::BlockId block : blocks) {
    if (index != 0) {
      output << ", ";
    }
    output << "bb" << block << '.' << function.blocks[block].label;
    ++index;
  }
  output << '}';
  return output.str();
}

}  // namespace

DominatorResult compute_dominators(const ControlFlowGraph& cfg) {
  DominatorResult result;
  result.dominator_sets.resize(cfg.successors.size());
  result.immediate_dominators.resize(cfg.successors.size());

  std::set<ir::BlockId> all_blocks;
  for (ir::BlockId block = 0; block < cfg.successors.size(); ++block) {
    all_blocks.insert(block);
  }

  for (ir::BlockId block = 0; block < cfg.successors.size(); ++block) {
    if (block == cfg.entry_block) {
      result.dominator_sets[block] = {block};
    } else {
      result.dominator_sets[block] = all_blocks;
    }
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (ir::BlockId block = 0; block < cfg.successors.size(); ++block) {
      if (block == cfg.entry_block) {
        continue;
      }

      std::set<ir::BlockId> updated = intersect_sets(result.dominator_sets, cfg.predecessors[block]);
      updated.insert(block);
      if (updated != result.dominator_sets[block]) {
        result.dominator_sets[block] = std::move(updated);
        changed = true;
      }
    }
  }

  for (ir::BlockId block = 0; block < cfg.successors.size(); ++block) {
    if (block == cfg.entry_block) {
      result.immediate_dominators[block] = std::nullopt;
      continue;
    }

    std::set<ir::BlockId> strict_doms = result.dominator_sets[block];
    strict_doms.erase(block);
    std::optional<ir::BlockId> idom;
    std::size_t best_depth = 0;
    for (ir::BlockId candidate : strict_doms) {
      const std::size_t candidate_depth = result.dominator_sets[candidate].size();
      if (!idom.has_value() || candidate_depth > best_depth) {
        idom = candidate;
        best_depth = candidate_depth;
      }
    }
    result.immediate_dominators[block] = idom;
  }

  return result;
}

std::string print_dominators(
    const ir::Function& function,
    const ControlFlowGraph& cfg,
    const DominatorResult& dominators) {
  std::ostringstream output;
  output << "func " << function.name << " dominators:\n";
  for (ir::BlockId block = 0; block < cfg.successors.size(); ++block) {
    output << "  bb" << block << '.' << function.blocks[block].label << ": "
           << format_dom_set(function, dominators.dominator_sets[block]) << "  idom = ";
    if (dominators.immediate_dominators[block].has_value()) {
      const ir::BlockId idom = *dominators.immediate_dominators[block];
      output << "bb" << idom << '.' << function.blocks[idom].label;
    } else {
      output << '-';
    }
    output << '\n';
  }
  return output.str();
}

}  // namespace nexus::compiler::analysis
