#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include "nexus/compiler/analysis/cfg.hpp"
#include "nexus/compiler/analysis/dominators.hpp"
#include "nexus/compiler/analysis/ssa.hpp"
#include "nexus/compiler/frontend/parser.hpp"
#include "nexus/compiler/ir/lowering.hpp"
#include "nexus/compiler/semantics/semantic_analyzer.hpp"

namespace analysis = nexus::compiler::analysis;

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: ssa_test <repo-root>\n";
    return 1;
  }
  std::ifstream input(std::string(argv[1]) + "/tests/programs/constant_branches.nx");
  std::stringstream buffer;
  buffer << input.rdbuf();
  auto parsed = nexus::compiler::frontend::parse_source(buffer.str());
  if (!parsed.diagnostics.empty() || !nexus::compiler::semantics::analyze_program(*parsed.program).diagnostics.empty()) {
    std::cerr << "program does not compile\n";
    return 1;
  }
  const auto lowered = nexus::compiler::ir::lower_program(*parsed.program);
  const auto& function = lowered.module->functions.front();
  const auto cfg = analysis::build_cfg(function);
  const auto dominators = analysis::compute_dominators(cfg);
  const auto ssa = analysis::build_ssa(function, cfg, dominators);
  const auto sccp = analysis::run_sccp(function, cfg, ssa);

  int failures = 0;
  auto expect = [&](bool condition, const std::string& message) {
    if (!condition) {
      std::cerr << "FAIL: " << message << '\n';
      ++failures;
    }
  };

  // Minimal SSA: phis for y at the if-join, and for i and sum at the loop header.
  expect(ssa.phi_count == 3, "expected exactly 3 phi nodes, got " + std::to_string(ssa.phi_count));
  std::map<std::string, std::size_t> phi_locals;
  for (const auto& block_phis : ssa.phis) {
    for (const auto& phi : block_phis) {
      ++phi_locals[function.locals[phi.local].name];
      expect(phi.arguments.size() == 2, "each phi merges two predecessors");
    }
  }
  expect(phi_locals["y"] == 1 && phi_locals["i"] == 1 && phi_locals["sum"] == 1 && phi_locals["x"] == 0,
         "phis must be placed for y, i and sum only");

  // Single assignment: every (local, version) is defined exactly once.
  std::map<std::pair<std::size_t, std::size_t>, int> definitions;
  for (const auto& block : function.blocks) {
    for (const auto& phi : ssa.phis[block.id]) {
      ++definitions[{phi.local, phi.version}];
    }
    for (std::size_t index = 0; index < block.instructions.size(); ++index) {
      if (block.instructions[index].kind == nexus::compiler::ir::InstructionKind::StoreLocal) {
        ++definitions[{block.instructions[index].local, ssa.versions[block.id][index]}];
      }
    }
  }
  for (const auto& [name, count] : definitions) {
    expect(count == 1, "SSA name defined more than once");
  }

  // SCCP: the else branch is dead, y is 5 after the join, loop-carried values are BOTTOM.
  expect(sccp.resolved_branches == 1, "the constant condition must resolve one branch");
  expect(function.blocks.size() - sccp.executable_blocks.size() == 1, "exactly one block is unreachable");
  bool y_is_five = false;
  bool loop_bottom = true;
  for (const auto& block_phis : ssa.phis) {
    for (const auto& phi : block_phis) {
      const auto value = sccp.local_versions.at({phi.local, phi.version});
      if (function.locals[phi.local].name == "y") {
        y_is_five = value.kind == analysis::LatticeValue::Kind::Constant && value.value == 5;
      } else {
        loop_bottom = loop_bottom && value.kind == analysis::LatticeValue::Kind::Bottom;
      }
    }
  }
  expect(y_is_five, "the phi for y must fold to the constant 5");
  expect(loop_bottom, "loop-carried phis must be BOTTOM");

  if (failures == 0) {
    std::cout << "ssa_test: all checks passed\n";
  }
  return failures == 0 ? 0 : 1;
}
