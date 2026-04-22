#include <iostream>
#include <string>

#include "nexus/compiler/experimental_parallel_parsing/bison_lr.hpp"
#include "nexus/compiler/experimental_parallel_parsing/prototype.hpp"
#include "nexus/compiler/frontend/parser.hpp"

namespace {

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

}  // namespace

int main() {
  const std::string source =
      "fn helper(x: int) -> int {\n"
      "  return x + 1;\n"
      "}\n"
      "\n"
      "fn main() -> int {\n"
      "  return helper(4);\n"
      "}\n";

  const auto production = nexus::compiler::frontend::parse_source(source);
  if (!production.program || !production.diagnostics.empty()) {
    std::cerr << "Expected production parser to accept the bounded parallel-parse source.\n";
    return 1;
  }

  const auto experimental =
      nexus::compiler::experimental_parallel_parsing::parse_source_experimental(source);
  if (!experimental.program || !experimental.diagnostics.empty()) {
    std::cerr << "Expected experimental parallel parser to accept the bounded source.\n";
    return 1;
  }

  if (experimental.program->functions.size() != production.program->functions.size() ||
      experimental.program->functions[0].name != "helper" ||
      experimental.program->functions[1].name != "main") {
    std::cerr << "Experimental parser did not preserve top-level function ordering.\n";
    return 1;
  }

  if (experimental.partitions.size() != 2U || experimental.launched_tasks != 2U) {
    std::cerr << "Expected the experimental parser to launch one task per top-level function.\n";
    return 1;
  }

  const std::string summary =
      nexus::compiler::experimental_parallel_parsing::print_partition_summary(experimental);
  if (!contains(summary, "tasks: 2") || !contains(summary, "part0: lines 1-3") ||
      !contains(summary, "part1: lines 5-7")) {
    std::cerr << "Experimental parser summary output was missing expected partition details.\n";
    return 1;
  }

  const std::string invalid_source =
      "var top: int = 1;\n"
      "fn main() -> int {\n"
      "  return top;\n"
      "}\n";
  const auto invalid =
      nexus::compiler::experimental_parallel_parsing::parse_source_experimental(invalid_source);
  if (invalid.diagnostics.empty() ||
      invalid.diagnostics.front().message.find("top-level function declarations only") ==
          std::string::npos) {
    std::cerr << "Expected a deterministic top-level partitioning diagnostic.\n";
    return 1;
  }

  const auto bison = nexus::compiler::experimental_parallel_parsing::parse_source_bison_lr(source);
  if (!bison.diagnostics.empty() || bison.summary.function_count != 2U || bison.summary.return_count != 2U) {
    std::cerr << "Expected the experimental Bison LR parser to accept the bounded source.\n";
    return 1;
  }

  const std::string bison_summary =
      nexus::compiler::experimental_parallel_parsing::print_bison_lr_summary(bison);
  if (!contains(bison_summary, "functions: 2") || !contains(bison_summary, "returns: 2")) {
    std::cerr << "Experimental Bison LR summary output was missing expected counts.\n";
    return 1;
  }

  const auto bison_invalid = nexus::compiler::experimental_parallel_parsing::parse_source_bison_lr(
      "fn main() -> int {\n"
      "  return 1\n"
      "}\n");
  if (bison_invalid.diagnostics.empty() ||
      bison_invalid.diagnostics.front().message.find("bison-lr:") == std::string::npos) {
    std::cerr << "Expected a deterministic Bison LR syntax diagnostic.\n";
    return 1;
  }

  return 0;
}
