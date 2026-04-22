#include <iostream>
#include <string>

#include "nexus/compiler/frontend/parser.hpp"
#include "nexus/compiler/semantics/semantic_analyzer.hpp"

namespace {

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

}  // namespace

int main() {
  using namespace nexus::compiler::frontend;
  using namespace nexus::compiler::semantics;

  const std::string valid_source =
      "fn factorial(n: int) -> int {\n"
      "  if (n <= 1) {\n"
      "    return 1;\n"
      "  }\n"
      "  return n * factorial(n - 1);\n"
      "}\n"
      "fn main() -> int {\n"
      "  var answer: int = factorial(5);\n"
      "  return answer;\n"
      "}\n";

  ParseResult valid_parse = parse_source(valid_source);
  if (!valid_parse.diagnostics.empty() || !valid_parse.program) {
    std::cerr << "Valid semantic test input should parse cleanly.\n";
    return 1;
  }

  const SemanticResult valid_semantics = analyze_program(*valid_parse.program);
  if (!valid_semantics.diagnostics.empty()) {
    std::cerr << "Valid semantic test input should check cleanly.\n";
    return 1;
  }

  const std::string invalid_source =
      "fn bad(flag: bool) -> int {\n"
      "  var flag: bool = true;\n"
      "  var x: int = flag;\n"
      "  y = 3;\n"
      "  if (x) {\n"
      "    return true;\n"
      "  }\n"
      "}\n";

  ParseResult invalid_parse = parse_source(invalid_source);
  if (!invalid_parse.diagnostics.empty() || !invalid_parse.program) {
    std::cerr << "Invalid semantic test input should still parse.\n";
    return 1;
  }

  const SemanticResult invalid_semantics = analyze_program(*invalid_parse.program);
  if (invalid_semantics.diagnostics.size() < 4U) {
    std::cerr << "Expected multiple semantic diagnostics.\n";
    return 1;
  }

  bool saw_duplicate = false;
  bool saw_type_mismatch = false;
  bool saw_undefined = false;
  bool saw_bad_condition = false;
  bool saw_bad_return = false;

  for (const auto& diagnostic : invalid_semantics.diagnostics) {
    saw_duplicate = saw_duplicate || contains(diagnostic.message, "duplicate definition");
    saw_type_mismatch = saw_type_mismatch || contains(diagnostic.message, "initializer");
    saw_undefined = saw_undefined || contains(diagnostic.message, "undefined identifier");
    saw_bad_condition = saw_bad_condition || contains(diagnostic.message, "condition must have type 'bool'");
    saw_bad_return = saw_bad_return || contains(diagnostic.message, "return expression has type");
  }

  if (!(saw_duplicate && saw_type_mismatch && saw_undefined && saw_bad_condition && saw_bad_return)) {
    std::cerr << "Missing expected semantic diagnostics.\n";
    return 1;
  }

  return 0;
}
