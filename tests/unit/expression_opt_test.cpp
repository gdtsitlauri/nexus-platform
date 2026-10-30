// Expression optimisation (NEY709 (f)): local value numbering must never change what a program computes, and
// available expressions solved by regions (intersection meet) must equal the iterative solution.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "nexus/compiler/analysis/cfg.hpp"
#include "nexus/compiler/analysis/region_flow.hpp"
#include "nexus/compiler/backend_mips/codegen.hpp"
#include "nexus/compiler/frontend/parser.hpp"
#include "nexus/compiler/ir/lowering.hpp"
#include "nexus/compiler/passes/value_numbering.hpp"
#include "nexus/compiler/semantics/semantic_analyzer.hpp"
#include "nexus/mips/assembler_support/printer.hpp"
#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/functional/interpreter.hpp"

namespace an = nexus::compiler::analysis;
namespace ir = nexus::compiler::ir;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::optional<ir::Module> lower(const std::string& source) {
  auto parsed = nexus::compiler::frontend::parse_source(source);
  if (!parsed.program || !parsed.diagnostics.empty()) return std::nullopt;
  if (!nexus::compiler::semantics::analyze_program(*parsed.program).diagnostics.empty()) return std::nullopt;
  auto lowered = ir::lower_program(*parsed.program);
  if (!lowered.module || !lowered.diagnostics.empty()) return std::nullopt;
  return std::move(*lowered.module);
}

std::optional<std::int32_t> run(const ir::Module& module) {
  const auto backend = nexus::compiler::backend_mips::lower_module(module);
  if (!backend.program || !backend.diagnostics.empty()) return std::nullopt;
  const auto loaded = nexus::mips::loader::load_program_from_text(
      nexus::mips::assembler_support::print_program(*backend.program));
  if (!loaded.program || !loaded.diagnostics.empty()) return std::nullopt;
  const auto result = nexus::sim::functional::run_program(*loaded.program, {.max_instructions = 5'000'000});
  if (!result.success) return std::nullopt;
  return result.exit_code;
}

std::size_t instruction_count(const ir::Module& module) {
  std::size_t count = 0;
  for (const auto& function : module.functions) {
    for (const auto& block : function.blocks) count += block.instructions.size();
  }
  return count;
}

// Returns false (and reports) when value numbering changed the result or the analyses disagree.
bool check(const std::string& source, const std::string& name, std::size_t* removed = nullptr) {
  const auto module = lower(source);
  if (!module.has_value()) {
    expect(false, name + ": does not compile");
    return false;
  }
  const auto before = run(*module);
  const auto numbered = nexus::compiler::passes::number_values(*module);
  const auto after = run(numbered.module);
  expect(before.has_value() && after == before, name + ": value numbering changed the result");
  expect(instruction_count(numbered.module) + numbered.removed == instruction_count(*module),
         name + ": removed count matches the instructions that disappeared");
  if (removed != nullptr) *removed += numbered.removed;
  for (const auto& function : module->functions) {
    const auto report = an::print_available_expressions(function, an::build_cfg(function));
    expect(report.find("agrees") != std::string::npos, name + " " + function.name + ":\n" + report);
  }
  return before.has_value() && after == before;
}

void worked_example(const std::string& repo) {
  std::ifstream input(repo + "/examples/source_lang/common_subexpressions.nx");
  std::stringstream text;
  text << input.rdbuf();
  const auto module = lower(text.str());
  expect(module.has_value(), "common_subexpressions.nx compiles");
  if (!module.has_value()) return;
  expect(run(*module) == 82, "common_subexpressions.nx returns 49 + 13 + 12 + 8 = 82");
  const auto numbered = nexus::compiler::passes::number_values(*module);
  expect(numbered.removed >= 5 && run(numbered.module) == 82, "value numbering removes copies and keeps 82");

  const auto& f = module->functions.front();
  const auto cfg = an::build_cfg(f);
  const auto tree = an::build_region_tree(cfg);
  const auto problem = an::available_expression_problem(f);
  const auto solution = an::solve_forward_by_regions(cfg, tree, problem.transfer, an::Meet::Intersection, {});
  const auto redundant = an::redundant_expressions(f, problem, solution.in_sets, tree.reachable);
  const bool local_ab = std::any_of(redundant.begin(), redundant.end(), [](const an::RedundantExpression& r) {
    return r.expression == "a + b" && !r.from_other_blocks;
  });
  const bool global_ab = std::any_of(redundant.begin(), redundant.end(), [](const an::RedundantExpression& r) {
    return r.expression == "a * b" && r.from_other_blocks;
  });
  expect(local_ab, "a + b twice in the first block is a local redundancy");
  expect(global_ab, "a * b after the if is available on both paths: a global redundancy");
  expect(redundant.size() == 2, "the a + b after 'a = a + 1' is not redundant (a changed)");
}

void repository_programs(const std::string& repo) {
  std::vector<std::filesystem::path> files;
  for (const auto* folder : {"/examples", "/tests/programs"}) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(repo + folder)) {
      if (entry.path().extension() == ".nx") files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  std::size_t checked = 0;
  std::size_t removed = 0;
  for (const auto& path : files) {
    std::ifstream input(path);
    std::stringstream text;
    text << input.rdbuf();
    if (!lower(text.str()).has_value()) continue;  // the examples that are meant to fail
    check(text.str(), path.filename().string(), &removed);
    ++checked;
  }
  std::cout << "repository programs: " << checked << " run before and after value numbering, " << removed
            << " instructions removed, results unchanged\n";
  expect(checked >= 10, "the repository programs were found");
}

// Random programs with repeated expressions, assignments between them, branches and short loops.
void random_programs() {
  std::mt19937 random(613);
  auto pick = [&](int n) { return static_cast<int>(random() % static_cast<unsigned>(n)); };
  const char* vars[] = {"a", "b", "c"};
  auto operand = [&]() { return pick(4) == 0 ? std::to_string(pick(9) + 1) : std::string(vars[pick(3)]); };
  auto expression = [&]() {
    const char* ops[] = {" + ", " - ", " * "};
    std::string e = operand() + ops[pick(3)] + operand();
    if (pick(2) == 0) e = "(" + e + ")" + ops[pick(3)] + "(" + operand() + ops[pick(3)] + operand() + ")";
    return e;
  };
  std::size_t removed = 0;
  std::size_t checked = 0;
  for (int trial = 0; trial < 120; ++trial) {
    std::ostringstream body;
    for (int s = 0; s < 4 + pick(5); ++s) {
      const auto kind = pick(5);
      const std::string target = vars[pick(3)];
      if (kind <= 2) {
        body << "  " << target << " = (" << expression() << ") % 1000;\n";
      } else if (kind == 3) {
        body << "  if (" << operand() << " < " << operand() << ") {\n    " << target << " = (" << expression()
             << ") % 1000;\n  } else {\n    " << target << " = (" << expression() << ") % 1000;\n  }\n";
      } else {
        body << "  var i" << s << ": int = 0;\n  while (i" << s << " < 3) {\n    " << target << " = (" << expression()
             << ") % 1000;\n    i" << s << " = i" << s << " + 1;\n  }\n";
      }
    }
    const std::string source = "fn f(a: int, b: int, c: int) -> int {\n" + body.str() +
                               "  return (a + b * 3 + c * 7) % 1000;\n}\nfn main() -> int {\n  return f(" +
                               std::to_string(pick(9) + 1) + ", " + std::to_string(pick(9) + 1) + ", " +
                               std::to_string(pick(9) + 1) + ");\n}\n";
    if (!check(source, "random program " + std::to_string(trial), &removed)) {
      std::cerr << source;
    }
    ++checked;
  }
  std::cout << "random programs: " << checked << " run before and after value numbering, " << removed
            << " instructions removed, results unchanged\n";
  expect(removed > 100, "the random programs give value numbering real work");
}

}  // namespace

int main(int argc, char** argv) {
  const std::string repo = argc > 1 ? argv[1] : ".";
  worked_example(repo);
  repository_programs(repo);
  random_programs();
  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "expression_opt_test: all checks passed\n";
  return 0;
}
