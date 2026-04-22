#include <iostream>
#include <optional>
#include <string>

#include "nexus/compiler/backend_mips/codegen.hpp"
#include "nexus/compiler/frontend/parser.hpp"
#include "nexus/compiler/ir/lowering.hpp"
#include "nexus/compiler/ir/printer.hpp"
#include "nexus/compiler/passes/loop_unroll.hpp"
#include "nexus/compiler/semantics/semantic_analyzer.hpp"
#include "nexus/mips/assembler_support/printer.hpp"
#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/functional/interpreter.hpp"

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

std::optional<std::int32_t> run_module(const nexus::compiler::ir::Module& module) {
  const auto backend = nexus::compiler::backend_mips::lower_module(module);
  if (!backend.program || !backend.diagnostics.empty()) {
    return std::nullopt;
  }

  const auto assembly = nexus::mips::assembler_support::print_program(*backend.program);
  const auto loaded = nexus::mips::loader::load_program_from_text(assembly);
  if (!loaded.program || !loaded.diagnostics.empty()) {
    return std::nullopt;
  }

  const auto result = nexus::sim::functional::run_program(*loaded.program);
  if (!result.success) {
    return std::nullopt;
  }

  return result.exit_code;
}

}  // namespace

int main() {
  const std::string concrete_source =
      "fn sum_to_4() -> int {\n"
      "  var i: int = 0;\n"
      "  var total: int = 0;\n"
      "  while (i < 4) {\n"
      "    total = total + i;\n"
      "    i = i + 1;\n"
      "  }\n"
      "  return total;\n"
      "}\n"
      "fn main() -> int {\n"
      "  return sum_to_4();\n"
      "}\n";

  const auto concrete_module = lower_checked(concrete_source);
  if (!concrete_module.has_value()) {
    std::cerr << "Expected concrete unroll source to lower successfully.\n";
    return 1;
  }

  const auto concrete_before = run_module(*concrete_module);
  if (!concrete_before.has_value() || *concrete_before != 6) {
    std::cerr << "Expected concrete unroll baseline program to execute to 6.\n";
    return 1;
  }

  const auto concrete = nexus::compiler::passes::unroll_loops(
      *concrete_module, nexus::compiler::passes::LoopUnrollMode::Concrete);
  if (concrete.transformed_loops != 1U || concrete.notes.empty() ||
      concrete.notes.front().find("fully unrolled loop with trip count 4") == std::string::npos) {
    std::cerr << "Concrete unroll did not report the expected fixed-trip transformation.\n";
    return 1;
  }

  const std::string concrete_ir = nexus::compiler::ir::print_module(concrete.module);
  if (!contains(concrete_ir, "while.unrolled.0") || !contains(concrete_ir, "while.unrolled.1") ||
      !contains(concrete_ir, "while.unrolled.2") || !contains(concrete_ir, "while.unrolled.3")) {
    std::cerr << "Concrete unroll output was missing expected cloned loop bodies.\n";
    return 1;
  }

  const auto concrete_after = run_module(concrete.module);
  if (!concrete_after.has_value() || *concrete_after != *concrete_before) {
    std::cerr << "Concrete unroll changed the observable result of the bounded loop program.\n";
    return 1;
  }

  const std::string symbolic_source =
      "fn sum_n(n: int) -> int {\n"
      "  var i: int = 0;\n"
      "  var total: int = 0;\n"
      "  while (i < n) {\n"
      "    total = total + i;\n"
      "    i = i + 1;\n"
      "  }\n"
      "  return total;\n"
      "}\n"
      "fn main() -> int {\n"
      "  return sum_n(4);\n"
      "}\n";

  const auto symbolic_module = lower_checked(symbolic_source);
  if (!symbolic_module.has_value()) {
    std::cerr << "Expected symbolic unroll source to lower successfully.\n";
    return 1;
  }

  const auto symbolic_before = run_module(*symbolic_module);
  if (!symbolic_before.has_value() || *symbolic_before != 6) {
    std::cerr << "Expected symbolic unroll baseline program to execute to 6.\n";
    return 1;
  }

  const auto symbolic = nexus::compiler::passes::unroll_loops(
      *symbolic_module, nexus::compiler::passes::LoopUnrollMode::Symbolic);
  if (symbolic.transformed_loops != 1U || symbolic.notes.empty() ||
      symbolic.notes.front().find("factor-2 unrolling with residual guard") == std::string::npos) {
    std::cerr << "Symbolic unroll did not report the expected residual-guard transformation.\n";
    return 1;
  }

  const std::string symbolic_ir = nexus::compiler::ir::print_module(symbolic.module);
  if (!contains(symbolic_ir, "while.unroll.check") || !contains(symbolic_ir, "while.unroll.0") ||
      !contains(symbolic_ir, "while.unroll.1")) {
    std::cerr << "Symbolic unroll output was missing expected residual-guard blocks.\n";
    return 1;
  }

  const auto symbolic_after = run_module(symbolic.module);
  if (!symbolic_after.has_value() || *symbolic_after != *symbolic_before) {
    std::cerr << "Symbolic unroll changed the observable result of the affine loop program.\n";
    return 1;
  }

  return 0;
}
