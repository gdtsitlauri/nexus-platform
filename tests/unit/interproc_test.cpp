#include <iostream>
#include <optional>
#include <string>

#include "nexus/compiler/analysis/interprocedural.hpp"
#include "nexus/compiler/backend_mips/codegen.hpp"
#include "nexus/compiler/frontend/parser.hpp"
#include "nexus/compiler/ir/lowering.hpp"
#include "nexus/compiler/ir/printer.hpp"
#include "nexus/compiler/passes/interprocedural_pass.hpp"
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
  const std::string source =
      "fn inc(x: int) -> int {\n"
      "  return x + 1;\n"
      "}\n"
      "fn main() -> int {\n"
      "  return inc(4) + inc(2);\n"
      "}\n";

  const auto module = lower_checked(source);
  if (!module.has_value()) {
    std::cerr << "Expected interprocedural source to lower successfully.\n";
    return 1;
  }

  const auto summaries = nexus::compiler::analysis::analyze_interprocedural(*module);
  if (summaries.summaries.size() != 2U) {
    std::cerr << "Expected two interprocedural summaries.\n";
    return 1;
  }

  const auto& inc_summary = summaries.summaries.front();
  if (inc_summary.name != "inc" || !inc_summary.pure || !inc_summary.tiny_candidate ||
      inc_summary.writes_memory || inc_summary.calls_other_functions ||
      inc_summary.return_summary.find("arg0 add 1") == std::string::npos) {
    std::cerr << "Expected interprocedural analysis to classify inc as a tiny pure summary.\n";
    return 1;
  }

  const std::string printed =
      nexus::compiler::analysis::print_interprocedural(*module, summaries);
  if (!contains(printed, "interprocedural summaries:") ||
      !contains(printed, "func inc: pure=yes, tiny=yes") ||
      !contains(printed, "return=(arg0 add 1)")) {
    std::cerr << "Interprocedural summary printout was missing expected information.\n";
    return 1;
  }

  const auto baseline = run_module(*module);
  if (!baseline.has_value() || *baseline != 8) {
    std::cerr << "Expected interprocedural baseline program to execute to 8.\n";
    return 1;
  }

  const auto folded = nexus::compiler::passes::fold_interprocedural_constants(*module);
  if (folded.replaced_calls != 2U) {
    std::cerr << "Expected interprocedural constant folding to replace both inc calls.\n";
    return 1;
  }

  const std::string folded_ir = nexus::compiler::ir::print_module(folded.module);
  if (!contains(folded_ir, "const_int 5") || !contains(folded_ir, "const_int 3")) {
    std::cerr << "Expected folded IR to materialize both constant call results.\n";
    return 1;
  }

  const auto after = run_module(folded.module);
  if (!after.has_value() || *after != *baseline) {
    std::cerr << "Interprocedural constant folding changed the program result.\n";
    return 1;
  }

  return 0;
}
