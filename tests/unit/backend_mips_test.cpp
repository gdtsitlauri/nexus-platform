#include <iostream>
#include <optional>
#include <string>

#include "nexus/compiler/backend_mips/codegen.hpp"
#include "nexus/compiler/frontend/parser.hpp"
#include "nexus/compiler/ir/lowering.hpp"
#include "nexus/compiler/semantics/semantic_analyzer.hpp"
#include "nexus/mips/assembler_support/printer.hpp"

namespace {

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

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

}  // namespace

int main() {
  const std::string factorial_source =
      "fn factorial(n: int) -> int {\n"
      "  if (n <= 1) {\n"
      "    return 1;\n"
      "  }\n"
      "  return n * factorial(n - 1);\n"
      "}\n"
      "fn main() -> int {\n"
      "  return factorial(5);\n"
      "}\n";

  const auto factorial_module = lower_checked(factorial_source);
  if (!factorial_module.has_value()) {
    std::cerr << "Expected factorial program to lower to IR successfully.\n";
    return 1;
  }

  const auto factorial_codegen = nexus::compiler::backend_mips::lower_module(*factorial_module);
  if (!factorial_codegen.diagnostics.empty() || !factorial_codegen.program.has_value()) {
    std::cerr << "Expected factorial IR to lower to MIPS successfully.\n";
    return 1;
  }

  const std::string factorial_asm =
      nexus::mips::assembler_support::print_program(*factorial_codegen.program);
  const std::string factorial_fragments[] = {
      "factorial:",
      "addiu $sp, $sp, -",
      "jal factorial",
      "mult $t0, $t1",
      "jr $ra",
      "main:"};
  for (const auto& fragment : factorial_fragments) {
    if (!contains(factorial_asm, fragment)) {
      std::cerr << "Factorial assembly missing fragment: " << fragment << '\n';
      return 1;
    }
  }

  const auto factorial_codegen_again = nexus::compiler::backend_mips::lower_module(*factorial_module);
  const std::string factorial_asm_again =
      nexus::mips::assembler_support::print_program(*factorial_codegen_again.program);
  if (factorial_asm != factorial_asm_again) {
    std::cerr << "MIPS assembly emission should be deterministic across runs.\n";
    return 1;
  }

  const std::string array_source =
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

  const auto array_module = lower_checked(array_source);
  if (!array_module.has_value()) {
    std::cerr << "Expected array program to lower to IR successfully.\n";
    return 1;
  }

  const auto array_codegen = nexus::compiler::backend_mips::lower_module(*array_module);
  if (!array_codegen.diagnostics.empty() || !array_codegen.program.has_value()) {
    std::cerr << "Expected array IR to lower to MIPS successfully.\n";
    return 1;
  }

  const std::string array_asm = nexus::mips::assembler_support::print_program(*array_codegen.program);
  const std::string array_fragments[] = {
      "sum4:",
      "sll $t1, $t1, 2",
      "addiu $a0, $fp, 0",
      "jal sum4",
      "lw $t1, 0($t0)",
      "sw $t1, 0($t0)"};
  for (const auto& fragment : array_fragments) {
    if (!contains(array_asm, fragment)) {
      std::cerr << "Array assembly missing fragment: " << fragment << '\n';
      return 1;
    }
  }

  return 0;
}
