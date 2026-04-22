#include <iostream>
#include <optional>
#include <string>

#include "nexus/compiler/analysis/affine_analysis.hpp"
#include "nexus/compiler/backend_mips/codegen.hpp"
#include "nexus/compiler/frontend/parser.hpp"
#include "nexus/compiler/ir/lowering.hpp"
#include "nexus/compiler/ir/printer.hpp"
#include "nexus/compiler/passes/affine_stripmine.hpp"
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
  const std::string arrays_source =
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

  const auto arrays_module = lower_checked(arrays_source);
  if (!arrays_module.has_value()) {
    std::cerr << "Expected affine analysis source to lower successfully.\n";
    return 1;
  }

  const auto affine = nexus::compiler::analysis::analyze_affine_loop(arrays_module->functions.front());
  if (!affine.supported || affine.trip_count != std::optional<std::int64_t>(4) ||
      affine.memory_locals.empty() || affine.memory_locals.front() != "values") {
    std::cerr << "Affine analysis did not detect the expected loop structure.\n";
    return 1;
  }

  const std::string affine_text =
      nexus::compiler::analysis::print_affine_loop(arrays_module->functions.front(), affine);
  if (!contains(affine_text, "trip-count=4") || !contains(affine_text, "memory-locals: values") ||
      !contains(affine_text, "strip-mining candidate")) {
    std::cerr << "Affine analysis output was missing expected locality details.\n";
    return 1;
  }

  const std::string strip_source =
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

  const auto strip_module = lower_checked(strip_source);
  if (!strip_module.has_value()) {
    std::cerr << "Expected strip-mine source to lower successfully.\n";
    return 1;
  }

  const auto before = run_module(*strip_module);
  if (!before.has_value() || *before != 6) {
    std::cerr << "Expected strip-mine baseline to execute to 6.\n";
    return 1;
  }

  const auto strip_mined = nexus::compiler::passes::strip_mine_loops(*strip_module, 2);
  if (strip_mined.transformed_loops != 1U || strip_mined.notes.empty() ||
      strip_mined.notes.front().find("tile factor 2") == std::string::npos) {
    std::cerr << "Strip-mine pass did not report the expected affine transformation.\n";
    return 1;
  }

  const std::string strip_ir = nexus::compiler::ir::print_module(strip_mined.module);
  if (!contains(strip_ir, "while.strip.guard") || !contains(strip_ir, "while.strip.tile.0") ||
      !contains(strip_ir, "while.strip.tile.1")) {
    std::cerr << "Strip-mine IR output was missing expected tile blocks.\n";
    return 1;
  }

  const auto after = run_module(strip_mined.module);
  if (!after.has_value() || *after != *before) {
    std::cerr << "Strip-mine pass changed the observable result of the bounded affine loop.\n";
    return 1;
  }

  return 0;
}
