#include <iostream>
#include <optional>
#include <string>

#include "nexus/compiler/analysis/alias_analysis.hpp"
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

std::optional<std::size_t> find_reference(
    const nexus::compiler::ir::Function& function,
    const nexus::compiler::analysis::AliasAnalysisResult& result,
    std::string_view label_fragment,
    std::optional<std::int64_t> constant_index,
    bool from_call) {
  for (const auto& reference : result.references) {
    if (reference.from_call != from_call) {
      continue;
    }
    if (reference.label.find(label_fragment) == std::string::npos) {
      continue;
    }
    if (nexus::compiler::ir::local_info(function, reference.base_local).name != "data") {
      continue;
    }
    if (!constant_index.has_value()) {
      if (reference.constant_indices.empty()) {
        return reference.id;
      }
      continue;
    }
    if (reference.constant_indices.size() == 1U && reference.constant_indices[0] == constant_index) {
      return reference.id;
    }
  }
  return std::nullopt;
}

}  // namespace

int main() {
  const std::string source =
      "fn touch(arr: int[4]) -> int {\n"
      "  return arr[0];\n"
      "}\n"
      "fn main() -> int {\n"
      "  var data: int[4];\n"
      "  data[0] = 1;\n"
      "  data[1] = 2;\n"
      "  var x: int = data[0];\n"
      "  return touch(data) + x;\n"
      "}\n";

  const auto module = lower_checked(source);
  if (!module.has_value()) {
    std::cerr << "Expected alias-analysis source to lower successfully.\n";
    return 1;
  }

  const auto& function = module->functions.back();
  const auto alias = nexus::compiler::analysis::analyze_aliases(function);
  if (alias.references.size() < 4U) {
    std::cerr << "Expected alias analysis to find the bounded array and call references.\n";
    return 1;
  }

  const auto store0 = find_reference(function, alias, "store_element", 0, false);
  const auto store1 = find_reference(function, alias, "store_element", 1, false);
  const auto load0 = find_reference(function, alias, "load_element", 0, false);
  const auto call_data = find_reference(function, alias, "call touch", std::nullopt, true);
  if (!store0.has_value() || !store1.has_value() || !load0.has_value() || !call_data.has_value()) {
    std::cerr << "Expected alias analysis to classify store/load/call references to data.\n";
    return 1;
  }

  if (alias.matrix[*store0][*load0] != nexus::compiler::analysis::AliasKind::MustAlias) {
    std::cerr << "Expected identical constant element accesses to must-alias.\n";
    return 1;
  }
  if (alias.matrix[*store0][*store1] != nexus::compiler::analysis::AliasKind::NoAlias) {
    std::cerr << "Expected distinct constant element accesses to no-alias.\n";
    return 1;
  }
  if (alias.matrix[*store0][*call_data] != nexus::compiler::analysis::AliasKind::MayAlias) {
    std::cerr << "Expected array call-by-reference summaries to conservatively may-alias.\n";
    return 1;
  }

  const std::string printed = nexus::compiler::analysis::print_aliases(function, alias);
  if (!contains(printed, "func main alias:") || !contains(printed, "must-alias") ||
      !contains(printed, "no-alias") || !contains(printed, "may-alias")) {
    std::cerr << "Alias analysis printout was missing expected relation classes.\n";
    return 1;
  }

  return 0;
}
