// Symbolic unrolling by a factor K with a remainder loop (NEY709 (g)): for random counted loops (upward and
// downward, every comparison, several steps and trip counts, including trips shorter than K), the unrolled
// program must compute exactly what the original computes.
#include <iostream>
#include <optional>
#include <random>
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

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::optional<nexus::compiler::ir::Module> lower_checked(const std::string& source) {
  auto parsed = nexus::compiler::frontend::parse_source(source);
  if (!parsed.program || !parsed.diagnostics.empty()) return std::nullopt;
  if (!nexus::compiler::semantics::analyze_program(*parsed.program).diagnostics.empty()) return std::nullopt;
  auto lowered = nexus::compiler::ir::lower_program(*parsed.program);
  if (!lowered.module || !lowered.diagnostics.empty()) return std::nullopt;
  return std::move(*lowered.module);
}

std::optional<std::int32_t> run_module(const nexus::compiler::ir::Module& module) {
  const auto backend = nexus::compiler::backend_mips::lower_module(module);
  if (!backend.program || !backend.diagnostics.empty()) return std::nullopt;
  const auto loaded = nexus::mips::loader::load_program_from_text(
      nexus::mips::assembler_support::print_program(*backend.program));
  if (!loaded.program || !loaded.diagnostics.empty()) return std::nullopt;
  const auto result = nexus::sim::functional::run_program(*loaded.program);
  if (!result.success) return std::nullopt;
  return result.exit_code;
}

std::string literal(int v) { return v < 0 ? "(0 - " + std::to_string(-v) + ")" : std::to_string(v); }

}  // namespace

int main() {
  const char* upward[] = {"i < n", "i <= n", "n > i", "n >= i"};
  const char* downward[] = {"i > n", "i >= n", "n < i", "n <= i"};
  std::mt19937 rng(17);
  std::size_t checked = 0;
  for (int round = 0; round < 160; ++round) {
    const bool up = round % 2 == 0;
    const std::string condition = up ? upward[rng() % 4] : downward[rng() % 4];
    const int step = 1 + static_cast<int>(rng() % 3);
    const int init = static_cast<int>(rng() % 7) - 3;
    const int n = init + (up ? 1 : -1) * static_cast<int>(rng() % 17) - (up ? 1 : -1);
    const std::size_t factor = 2 + rng() % 4;
    const std::string source =
        "fn f(n: int) -> int {\n"
        "  var i: int = " + literal(init) + ";\n"
        "  var total: int = 1;\n"
        "  while (" + condition + ") {\n"
        "    total = (total * 3 + i) % 10007;\n"
        "    i = i " + (up ? "+ " : "- ") + std::to_string(step) + ";\n"
        "  }\n"
        "  return total * 100 + i;\n"
        "}\n"
        "fn main() -> int {\n"
        "  return f(" + literal(n) + ");\n"
        "}\n";
    const auto module = lower_checked(source);
    expect(module.has_value(), "generated program lowers:\n" + source);
    if (!module) continue;
    const auto before = run_module(*module);
    const auto unrolled =
        nexus::compiler::passes::unroll_loops(*module, nexus::compiler::passes::LoopUnrollMode::SymbolicRemainder, factor);
    expect(unrolled.transformed_loops == 1, "the loop is unrolled: " + condition);
    const std::string printed = nexus::compiler::ir::print_module(unrolled.module);
    expect(printed.find("while.main.body") != std::string::npos, "main loop body present");
    const auto after = run_module(unrolled.module);
    expect(before.has_value() && after.has_value() && *before == *after,
           "same result after unrolling by " + std::to_string(factor) + ":\n" + source);
    ++checked;
  }
  // A loop whose bound changes inside the body must be left alone.
  const auto moving = lower_checked(
      "fn f(n: int) -> int {\n  var i: int = 0;\n  var m: int = n;\n  while (i < m) {\n    m = m - 1;\n"
      "    i = i + 1;\n  }\n  return i;\n}\nfn main() -> int {\n  return f(10);\n}\n");
  if (moving) {
    const auto result =
        nexus::compiler::passes::unroll_loops(*moving, nexus::compiler::passes::LoopUnrollMode::SymbolicRemainder, 4);
    expect(result.transformed_loops == 0, "a loop with a moving bound is rejected");
  }
  if (failures == 0) {
    std::cout << "remainder unrolling: " << checked << " random loops give the same result after unrolling\n";
  }
  return failures == 0 ? 0 : 1;
}
