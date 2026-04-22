#include <iostream>
#include <optional>
#include <string>

#include "nexus/compiler/analysis/cfg.hpp"
#include "nexus/compiler/analysis/dominators.hpp"
#include "nexus/compiler/analysis/liveness.hpp"
#include "nexus/compiler/frontend/parser.hpp"
#include "nexus/compiler/ir/lowering.hpp"
#include "nexus/compiler/semantics/semantic_analyzer.hpp"

namespace {

std::optional<nexus::compiler::ir::Module> lower_checked(std::string_view source) {
  auto parsed = nexus::compiler::frontend::parse_source(source);
  if (!parsed.diagnostics.empty() || !parsed.program) {
    return std::nullopt;
  }

  const auto semantics = nexus::compiler::semantics::analyze_program(*parsed.program);
  if (!semantics.diagnostics.empty()) {
    return std::nullopt;
  }

  auto lowered = nexus::compiler::ir::lower_program(*parsed.program);
  if (!lowered.diagnostics.empty() || !lowered.module) {
    return std::nullopt;
  }

  return std::move(*lowered.module);
}

bool contains_local_name(
    const nexus::compiler::analysis::DataFlowSet& values,
    const nexus::compiler::ir::Function& function,
    std::string_view name) {
  for (const auto local : values) {
    if (nexus::compiler::ir::local_info(function, local).name == name) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  const std::string source =
      "fn sum4(values: int[4]) -> int {\n"
      "  var i: int = 0;\n"
      "  var total: int = 0;\n"
      "  while (i < 4) {\n"
      "    total = total + values[i];\n"
      "    i = i + 1;\n"
      "  }\n"
      "  return total;\n"
      "}\n"
      "fn main() -> int {\n"
      "  var data: int[4];\n"
      "  data[0] = 1;\n"
      "  data[1] = 2;\n"
      "  data[2] = 3;\n"
      "  data[3] = 4;\n"
      "  return sum4(data);\n"
      "}\n";

  const auto module = lower_checked(source);
  if (!module.has_value()) {
    std::cerr << "Expected source to lower successfully for CFG analysis.\n";
    return 1;
  }
  if (module->functions.empty()) {
    std::cerr << "Expected lowered module to contain functions.\n";
    return 1;
  }

  const auto& function = module->functions.front();
  const auto cfg = nexus::compiler::analysis::build_cfg(function);

  if (cfg.entry_block != 0 || function.blocks.size() != 4U) {
    std::cerr << "Unexpected block structure for loop CFG.\n";
    return 1;
  }

  if (cfg.successors[0] != std::vector<nexus::compiler::ir::BlockId>{1} ||
      cfg.successors[1] != std::vector<nexus::compiler::ir::BlockId>{2, 3} ||
      cfg.successors[2] != std::vector<nexus::compiler::ir::BlockId>{1} ||
      !cfg.successors[3].empty()) {
    std::cerr << "Unexpected CFG successor relationships.\n";
    return 1;
  }

  if (cfg.predecessors[0] != std::vector<nexus::compiler::ir::BlockId>{} ||
      cfg.predecessors[1] != std::vector<nexus::compiler::ir::BlockId>{0, 2} ||
      cfg.predecessors[2] != std::vector<nexus::compiler::ir::BlockId>{1} ||
      cfg.predecessors[3] != std::vector<nexus::compiler::ir::BlockId>{1}) {
    std::cerr << "Unexpected CFG predecessor relationships.\n";
    return 1;
  }

  const auto dominators = nexus::compiler::analysis::compute_dominators(cfg);
  if (dominators.immediate_dominators[0].has_value() ||
      dominators.immediate_dominators[1] != std::optional<nexus::compiler::ir::BlockId>{0} ||
      dominators.immediate_dominators[2] != std::optional<nexus::compiler::ir::BlockId>{1} ||
      dominators.immediate_dominators[3] != std::optional<nexus::compiler::ir::BlockId>{1}) {
    std::cerr << "Unexpected immediate dominators.\n";
    return 1;
  }

  const auto liveness = nexus::compiler::analysis::analyze_liveness(function, cfg);
  if (liveness.flow.iterations == 0U) {
    std::cerr << "Expected liveness to iterate to a fixed point.\n";
    return 1;
  }

  if (!contains_local_name(liveness.flow.in_sets[0], function, "values") ||
      contains_local_name(liveness.flow.in_sets[0], function, "i") ||
      contains_local_name(liveness.flow.in_sets[0], function, "total")) {
    std::cerr << "Unexpected live-in set for entry block.\n";
    return 1;
  }

  const bool block0_out_ok = contains_local_name(liveness.flow.out_sets[0], function, "values") &&
      contains_local_name(liveness.flow.out_sets[0], function, "i") &&
      contains_local_name(liveness.flow.out_sets[0], function, "total");
  if (!block0_out_ok) {
    std::cerr << "Unexpected live-out set for entry block.\n";
    return 1;
  }

  const bool block1_in_ok = contains_local_name(liveness.flow.in_sets[1], function, "values") &&
      contains_local_name(liveness.flow.in_sets[1], function, "i") &&
      contains_local_name(liveness.flow.in_sets[1], function, "total");
  if (!block1_in_ok) {
    std::cerr << "Unexpected live-in set for loop condition block.\n";
    return 1;
  }

  if (!contains_local_name(liveness.flow.in_sets[3], function, "total") ||
      contains_local_name(liveness.flow.in_sets[3], function, "values") ||
      contains_local_name(liveness.flow.in_sets[3], function, "i")) {
    std::cerr << "Unexpected live-in set for loop exit block.\n";
    return 1;
  }

  return 0;
}
