#include "nexus/compiler/analysis/region_flow.hpp"

#include <algorithm>
#include <map>
#include <queue>
#include <sstream>
#include <utility>

#include "nexus/compiler/analysis/dominators.hpp"

namespace nexus::compiler::analysis {

namespace {

std::pair<DataFlowSet, DataFlowSet> build_use_def_sets(const ir::BasicBlock& block) {
  DataFlowSet uses;
  DataFlowSet defs;

  auto note_use = [&uses, &defs](DataFlowEntityId local) {
    if (!defs.contains(local)) {
      uses.insert(local);
    }
  };
  auto note_def = [&defs](DataFlowEntityId local) { defs.insert(local); };

  for (const auto& instruction : block.instructions) {
    switch (instruction.kind) {
      case ir::InstructionKind::LoadLocal:
      case ir::InstructionKind::LoadElement:
        note_use(instruction.local);
        break;
      case ir::InstructionKind::StoreLocal:
        note_def(instruction.local);
        break;
      case ir::InstructionKind::StoreElement:
        note_use(instruction.local);
        break;
      case ir::InstructionKind::Call:
        for (const auto& argument : instruction.call_arguments) {
          if (argument.kind == ir::CallArgumentKind::LocalRef) {
            note_use(argument.local);
          }
        }
        break;
      case ir::InstructionKind::ConstInt:
      case ir::InstructionKind::ConstBool:
      case ir::InstructionKind::Unary:
      case ir::InstructionKind::Binary:
        break;
    }
  }

  return {uses, defs};
}

std::string format_region_blocks(const ir::Function& function, const RegionSummary& region) {
  std::ostringstream output;
  output << '{';
  for (std::size_t index = 0; index < region.blocks.size(); ++index) {
    if (index != 0) {
      output << ", ";
    }
    const auto block = region.blocks[index];
    output << "bb" << block << '.' << function.blocks[block].label;
  }
  output << '}';
  return output.str();
}

std::string format_local_set(const ir::Function& function, const DataFlowSet& values) {
  if (values.empty()) {
    return "{}";
  }

  std::ostringstream output;
  output << '{';
  std::size_t index = 0;
  for (const auto value : values) {
    if (index != 0) {
      output << ", ";
    }
    output << ir::local_info(function, value).name;
    ++index;
  }
  output << '}';
  return output.str();
}

std::vector<std::size_t> topological_order(const std::vector<std::vector<std::size_t>>& successors) {
  std::vector<std::size_t> indegree(successors.size(), 0);
  for (const auto& edges : successors) {
    for (const auto succ : edges) {
      ++indegree[succ];
    }
  }

  std::queue<std::size_t> ready;
  for (std::size_t index = 0; index < indegree.size(); ++index) {
    if (indegree[index] == 0) {
      ready.push(index);
    }
  }

  std::vector<std::size_t> order;
  while (!ready.empty()) {
    const auto next = ready.front();
    ready.pop();
    order.push_back(next);
    for (const auto succ : successors[next]) {
      if (--indegree[succ] == 0) {
        ready.push(succ);
      }
    }
  }
  return order;
}

}  // namespace

RegionFlowResult analyze_region_liveness(const ir::Function& function, const ControlFlowGraph& cfg) {
  RegionFlowResult result;
  const auto dominators = compute_dominators(cfg);

  std::map<ir::BlockId, std::size_t> loop_region_for_header;
  std::vector<bool> assigned(function.blocks.size(), false);

  for (ir::BlockId source = 0; source < cfg.successors.size(); ++source) {
    for (ir::BlockId target : cfg.successors[source]) {
      if (dominators.dominator_sets[source].contains(target)) {
        if (!loop_region_for_header.contains(target)) {
          loop_region_for_header[target] = result.regions.size();
          result.regions.push_back(
              RegionSummary{.id = result.regions.size(), .is_loop = true, .blocks = {target}});
        }
        auto& blocks = result.regions[loop_region_for_header[target]].blocks;
        if (std::find(blocks.begin(), blocks.end(), source) == blocks.end()) {
          blocks.push_back(source);
        }
      }
    }
  }

  for (auto& region : result.regions) {
    std::sort(region.blocks.begin(), region.blocks.end());
    for (auto block : region.blocks) {
      assigned[block] = true;
    }
  }

  for (ir::BlockId block = 0; block < function.blocks.size(); ++block) {
    if (assigned[block]) {
      continue;
    }
    result.regions.push_back(RegionSummary{
        .id = result.regions.size(),
        .is_loop = false,
        .blocks = {block},
    });
  }

  std::sort(
      result.regions.begin(),
      result.regions.end(),
      [](const RegionSummary& lhs, const RegionSummary& rhs) {
        return lhs.blocks.front() < rhs.blocks.front();
      });
  for (std::size_t index = 0; index < result.regions.size(); ++index) {
    result.regions[index].id = index;
  }

  std::vector<std::size_t> block_to_region(function.blocks.size(), 0);
  for (const auto& region : result.regions) {
    for (auto block : region.blocks) {
      block_to_region[block] = region.id;
    }
  }

  result.successors.assign(result.regions.size(), {});
  result.use_sets.assign(result.regions.size(), {});
  result.def_sets.assign(result.regions.size(), {});
  result.in_sets.assign(result.regions.size(), {});
  result.out_sets.assign(result.regions.size(), {});

  for (const auto& region : result.regions) {
    DataFlowSet region_use;
    DataFlowSet region_def;
    for (auto block_id : region.blocks) {
      auto [uses, defs] = build_use_def_sets(function.blocks[block_id]);
      for (auto value : uses) {
        if (!region_def.contains(value)) {
          region_use.insert(value);
        }
      }
      region_def.insert(defs.begin(), defs.end());

      for (auto succ : cfg.successors[block_id]) {
        const auto succ_region = block_to_region[succ];
        if (succ_region != region.id &&
            std::find(result.successors[region.id].begin(), result.successors[region.id].end(), succ_region) ==
                result.successors[region.id].end()) {
          result.successors[region.id].push_back(succ_region);
        }
      }
    }
    result.use_sets[region.id] = std::move(region_use);
    result.def_sets[region.id] = std::move(region_def);
  }

  auto order = topological_order(result.successors);
  std::reverse(order.begin(), order.end());

  for (auto region_id : order) {
    DataFlowSet out_set;
    for (auto succ : result.successors[region_id]) {
      out_set.insert(result.in_sets[succ].begin(), result.in_sets[succ].end());
    }
    result.out_sets[region_id] = out_set;
    DataFlowSet in_set = out_set;
    for (auto killed : result.def_sets[region_id]) {
      in_set.erase(killed);
    }
    in_set.insert(result.use_sets[region_id].begin(), result.use_sets[region_id].end());
    result.in_sets[region_id] = std::move(in_set);
  }

  return result;
}

std::string print_region_liveness(const ir::Function& function, const RegionFlowResult& result) {
  std::ostringstream output;
  output << "func " << function.name << " region-liveness:\n";
  for (const auto& region : result.regions) {
    output << "  region" << region.id << " (" << (region.is_loop ? "loop" : "block") << ") "
           << format_region_blocks(function, region) << '\n';
    output << "    use: " << format_local_set(function, result.use_sets[region.id]) << '\n';
    output << "    def: " << format_local_set(function, result.def_sets[region.id]) << '\n';
    output << "    in:  " << format_local_set(function, result.in_sets[region.id]) << '\n';
    output << "    out: " << format_local_set(function, result.out_sets[region.id]) << '\n';
  }
  return output.str();
}

}  // namespace nexus::compiler::analysis
