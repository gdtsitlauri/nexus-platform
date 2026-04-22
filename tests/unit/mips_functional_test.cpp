#include <iostream>
#include <string>

#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/functional/interpreter.hpp"

namespace {

nexus::sim::functional::RunResult run_text(const std::string& assembly) {
  const auto parsed = nexus::mips::loader::load_program_from_text(assembly);
  if (!parsed.diagnostics.empty() || !parsed.program.has_value()) {
    return nexus::sim::functional::RunResult{
        .success = false,
        .error = parsed.diagnostics.empty() ? "loader failed" : parsed.diagnostics.front(),
        .trace_lines = {}};
  }
  return nexus::sim::functional::run_program(*parsed.program);
}

}  // namespace

int main() {
  const std::string call_program =
      "main:\n"
      "  addiu $sp, $sp, -8\n"
      "  sw $ra, 4($sp)\n"
      "  addiu $t0, $zero, 9\n"
      "  addiu $t1, $zero, 4\n"
      "  sub $t2, $t0, $t1\n"
      "  sw $t2, 0($sp)\n"
      "  lw $a0, 0($sp)\n"
      "  jal adjust\n"
      "  lw $ra, 4($sp)\n"
      "  addiu $sp, $sp, 8\n"
      "  jr $ra\n"
      "adjust:\n"
      "  addiu $t0, $zero, 5\n"
      "  beq $a0, $t0, equal\n"
      "  addiu $v0, $zero, 0\n"
      "  jr $ra\n"
      "equal:\n"
      "  addiu $v0, $zero, 42\n"
      "  jr $ra\n";

  const auto call_result = run_text(call_program);
  if (!call_result.success || call_result.exit_code != 42) {
    std::cerr << "Expected branch/call test program to return 42.\n";
    return 1;
  }

  const std::string mult_div_program =
      "main:\n"
      "  addiu $t0, $zero, 7\n"
      "  addiu $t1, $zero, 6\n"
      "  mult $t0, $t1\n"
      "  mflo $t2\n"
      "  addiu $t3, $zero, 5\n"
      "  div $t2, $t3\n"
      "  mflo $t4\n"
      "  mfhi $t5\n"
      "  addu $v0, $t4, $t5\n"
      "  jr $ra\n";

  const auto mult_div_result = run_text(mult_div_program);
  if (!mult_div_result.success || mult_div_result.exit_code != 10) {
    std::cerr << "Expected multiply/divide test program to return 10.\n";
    return 1;
  }

  const std::string trace_program =
      "main:\n"
      "  addiu $v0, $zero, 7\n"
      "  jr $ra\n";
  const auto trace_loaded = nexus::mips::loader::load_program_from_text(trace_program);
  if (!trace_loaded.program.has_value()) {
    std::cerr << "Expected trace program to load.\n";
    return 1;
  }
  const auto trace_result =
      nexus::sim::functional::run_program(*trace_loaded.program, {.trace = true});
  if (!trace_result.success || trace_result.trace_lines.size() != 2U ||
      trace_result.trace_lines.front() != "trace: pc=0 addiu $v0, $zero, 7") {
    std::cerr << "Expected deterministic trace output for simple program.\n";
    return 1;
  }

  return 0;
}
