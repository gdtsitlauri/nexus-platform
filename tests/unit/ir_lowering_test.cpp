#include <iostream>
#include <optional>
#include <string>

#include "nexus/compiler/frontend/parser.hpp"
#include "nexus/compiler/ir/lowering.hpp"
#include "nexus/compiler/ir/printer.hpp"
#include "nexus/compiler/semantics/semantic_analyzer.hpp"

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
    std::cerr << "Expected factorial source to lower successfully.\n";
    return 1;
  }
  if (factorial_module->functions.size() != 2U) {
    std::cerr << "Expected two functions in lowered factorial module.\n";
    return 1;
  }

  const std::string factorial_ir = nexus::compiler::ir::print_module(*factorial_module);
  const std::string factorial_fragments[] = {
      "func factorial(n: int) -> int {",
      "bb1.if.then:",
      "call factorial(",
      "return %"};
  for (const auto& fragment : factorial_fragments) {
    if (!contains(factorial_ir, fragment)) {
      std::cerr << "Factorial IR missing fragment: " << fragment << '\n';
      return 1;
    }
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
    std::cerr << "Expected array source to lower successfully.\n";
    return 1;
  }

  const std::string array_ir = nexus::compiler::ir::print_module(*array_module);
  const std::string array_fragments[] = {
      "load_element values[",
      "store_element data[",
      "call sum4(data)",
      "bb1.while.cond:"};
  for (const auto& fragment : array_fragments) {
    if (!contains(array_ir, fragment)) {
      std::cerr << "Array IR missing fragment: " << fragment << '\n';
      return 1;
    }
  }

  return 0;
}
