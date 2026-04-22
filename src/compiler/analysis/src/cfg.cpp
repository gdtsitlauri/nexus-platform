#include "nexus/compiler/analysis/cfg.hpp"

#include <sstream>

namespace nexus::compiler::analysis {

namespace {

std::vector<ir::BlockId> block_successors(const ir::BasicBlock& block) {
  if (!block.terminator.has_value()) {
    return {};
  }

  switch (block.terminator->kind) {
    case ir::TerminatorKind::Jump:
      return {block.terminator->true_target};
    case ir::TerminatorKind::Branch:
      return {block.terminator->true_target, block.terminator->false_target};
    case ir::TerminatorKind::Return:
      return {};
  }

  return {};
}

std::string join_blocks(const ir::Function& function, const std::vector<ir::BlockId>& blocks) {
  if (blocks.empty()) {
    return "-";
  }

  std::ostringstream output;
  for (std::size_t index = 0; index < blocks.size(); ++index) {
    if (index != 0) {
      output << ", ";
    }
    output << "bb" << blocks[index] << "." << function.blocks[blocks[index]].label;
  }
  return output.str();
}

}  // namespace

ControlFlowGraph build_cfg(const ir::Function& function) {
  ControlFlowGraph cfg;
  cfg.entry_block = function.entry_block;
  cfg.successors.resize(function.blocks.size());
  cfg.predecessors.resize(function.blocks.size());

  for (const ir::BasicBlock& block : function.blocks) {
    cfg.successors[block.id] = block_successors(block);
    if (cfg.successors[block.id].empty()) {
      cfg.exit_blocks.push_back(block.id);
    }
    for (ir::BlockId successor : cfg.successors[block.id]) {
      cfg.predecessors[successor].push_back(block.id);
    }
  }

  return cfg;
}

std::string print_cfg(const ir::Function& function, const ControlFlowGraph& cfg) {
  std::ostringstream output;
  output << "func " << function.name << " cfg:\n";
  output << "  entry: bb" << cfg.entry_block << '.' << function.blocks[cfg.entry_block].label << '\n';
  output << "  exits: " << join_blocks(function, cfg.exit_blocks) << '\n';
  for (const ir::BasicBlock& block : function.blocks) {
    output << "  bb" << block.id << '.' << block.label << ":\n";
    output << "    preds: " << join_blocks(function, cfg.predecessors[block.id]) << '\n';
    output << "    succs: " << join_blocks(function, cfg.successors[block.id]) << '\n';
  }
  return output.str();
}

}  // namespace nexus::compiler::analysis
