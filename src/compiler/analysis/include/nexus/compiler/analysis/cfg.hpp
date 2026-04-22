#pragma once

#include <string>
#include <vector>

#include "nexus/compiler/ir/ir.hpp"

namespace nexus::compiler::analysis {

struct ControlFlowGraph {
  ir::BlockId entry_block = 0;
  std::vector<std::vector<ir::BlockId>> successors;
  std::vector<std::vector<ir::BlockId>> predecessors;
  std::vector<ir::BlockId> exit_blocks;
};

ControlFlowGraph build_cfg(const ir::Function& function);
std::string print_cfg(const ir::Function& function, const ControlFlowGraph& cfg);

}  // namespace nexus::compiler::analysis
