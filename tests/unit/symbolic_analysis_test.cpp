#include <iostream>
#include <optional>
#include <string>

#include "nexus/compiler/analysis/cfg.hpp"
#include "nexus/compiler/analysis/region_flow.hpp"
#include "nexus/compiler/analysis/symbolic.hpp"
#include "nexus/compiler/frontend/parser.hpp"
#include "nexus/compiler/ir/lowering.hpp"
#include "nexus/compiler/semantics/semantic_analyzer.hpp"

namespace {

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

std::optional<nexus::compiler::ir::Module> lower_checked(std::string_view source) {
  auto parsed = nexus::compiler::frontend::parse_source(source);
  if (!parsed.program || !parsed.diagnostics.empty()) {
    return std::nullopt;
  }

  const auto semantics = nexus::compiler::semantics::analyze_program(*parsed.program);
  if (!semantics.diagnostics.empty()) {
    return std::nullopt;
  }

  auto lowered = nexus::compiler::ir::lower_program(*parsed.program);
  if (!lowered.module || !lowered.diagnostics.empty()) {
    return std::nullopt;
  }

  return std::move(*lowered.module);
}

}  // namespace

int main() {
  const std::string symbolic_source =
      "fn main() -> int {\n"
      "  var x: int = 4;\n"
      "  var y: int = x + 0;\n"
      "  var z: int = y * 1;\n"
      "  return z - z;\n"
      "}\n";

  const auto symbolic_module = lower_checked(symbolic_source);
  if (!symbolic_module.has_value()) {
    std::cerr << "Expected symbolic analysis source to lower successfully.\n";
    return 1;
  }

  const auto& symbolic_function = symbolic_module->functions.front();
  const auto symbolic_cfg = nexus::compiler::analysis::build_cfg(symbolic_function);
  const auto symbolic =
      nexus::compiler::analysis::analyze_symbolic(symbolic_function, symbolic_cfg);

  bool saw_folded_add = false;
  bool saw_folded_mul = false;
  bool saw_folded_sub = false;
  for (const auto& instruction : symbolic_function.blocks.front().instructions) {
    if (!instruction.result.has_value() ||
        symbolic.value_symbols[*instruction.result].kind !=
            nexus::compiler::analysis::SymbolicKind::IntegerConstant) {
      continue;
    }

    const auto value = symbolic.value_symbols[*instruction.result].int_value;
    if (instruction.kind == nexus::compiler::ir::InstructionKind::Binary &&
        instruction.binary_op == nexus::compiler::ir::BinaryOp::Add && value == 4) {
      saw_folded_add = true;
    }
    if (instruction.kind == nexus::compiler::ir::InstructionKind::Binary &&
        instruction.binary_op == nexus::compiler::ir::BinaryOp::Mul && value == 4) {
      saw_folded_mul = true;
    }
    if (instruction.kind == nexus::compiler::ir::InstructionKind::Binary &&
        instruction.binary_op == nexus::compiler::ir::BinaryOp::Sub && value == 0) {
      saw_folded_sub = true;
    }
  }

  if (!saw_folded_add || !saw_folded_mul || !saw_folded_sub) {
    std::cerr << "Symbolic analysis did not fold the bounded arithmetic identities.\n";
    return 1;
  }

  const std::string symbolic_text =
      nexus::compiler::analysis::print_symbolic(symbolic_function, symbolic_cfg, symbolic);
  if (!contains(symbolic_text, "func main symbolic:") ||
      !contains(symbolic_text, "out locals: {x = 4, y = 4, z = 4}")) {
    std::cerr << "Symbolic analysis text output was missing expected local-state summaries.\n";
    return 1;
  }

  const std::string loop_source =
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

  const auto loop_module = lower_checked(loop_source);
  if (!loop_module.has_value()) {
    std::cerr << "Expected region-liveness source to lower successfully.\n";
    return 1;
  }

  const auto& loop_function = loop_module->functions.front();
  const auto loop_cfg = nexus::compiler::analysis::build_cfg(loop_function);
  const auto region = nexus::compiler::analysis::analyze_region_liveness(loop_function, loop_cfg);

  bool saw_loop_region = false;
  for (const auto& summary : region.regions) {
    if (summary.is_loop && summary.blocks.size() == 2U && summary.blocks[0] == 1U &&
        summary.blocks[1] == 2U) {
      saw_loop_region = true;
      break;
    }
  }
  if (!saw_loop_region) {
    std::cerr << "Expected region-based liveness to collapse the simple while loop region.\n";
    return 1;
  }

  const std::string region_text =
      nexus::compiler::analysis::print_region_liveness(loop_function, region);
  if (!contains(region_text, "func sum4 region-liveness:") ||
      !contains(region_text, "(loop) {bb1.while.cond, bb2.while.body}")) {
    std::cerr << "Region-liveness text output was missing the expected loop region.\n";
    return 1;
  }

  return 0;
}
