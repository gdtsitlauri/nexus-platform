#include <iostream>
#include <string>

#include "nexus/compiler/frontend/ast_printer.hpp"
#include "nexus/compiler/frontend/parser.hpp"

namespace {

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

}  // namespace

int main() {
  using namespace nexus::compiler::frontend;

  const std::string valid_source =
      "fn factorial(n: int) -> int {\n"
      "  if (n <= 1) {\n"
      "    return 1;\n"
      "  } else {\n"
      "    return n * factorial(n - 1);\n"
      "  }\n"
      "}\n"
      "fn main() -> int {\n"
      "  return factorial(5);\n"
      "}\n";

  ParseResult valid_result = parse_source(valid_source);
  if (!valid_result.diagnostics.empty() || !valid_result.program) {
    std::cerr << "Expected valid program to parse successfully.\n";
    return 1;
  }
  if (valid_result.program->functions.size() != 2U) {
    std::cerr << "Expected two functions in parsed AST.\n";
    return 1;
  }

  const std::string ast_dump = print_program(*valid_result.program);
  const std::string required_fragments[] = {
      "Program", "Function factorial(n: int) -> int", "If", "Call", "Function main() -> int"};
  for (const auto& fragment : required_fragments) {
    if (!contains(ast_dump, fragment)) {
      std::cerr << "AST output missing fragment: " << fragment << '\n';
      return 1;
    }
  }

  const std::string invalid_source =
      "fn broken() -> int {\n"
      "  var x: int = 1\n"
      "  return x;\n"
      "}\n";

  ParseResult invalid_result = parse_source(invalid_source);
  if (invalid_result.diagnostics.empty()) {
    std::cerr << "Expected syntax diagnostics for invalid source.\n";
    return 1;
  }

  bool found_expected_message = false;
  for (const auto& diagnostic : invalid_result.diagnostics) {
    if (contains(diagnostic.message, "expected ';'")) {
      found_expected_message = true;
      break;
    }
  }
  if (!found_expected_message) {
    std::cerr << "Expected missing-semicolon parser diagnostic.\n";
    return 1;
  }

  return 0;
}
