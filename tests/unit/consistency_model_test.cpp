#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/parallel/model.hpp"

namespace {

nexus::mips::loader::LoadedProgram load_program(const std::string& assembly) {
  const auto parsed = nexus::mips::loader::load_program_from_text(assembly);
  if (!parsed.diagnostics.empty() || !parsed.program.has_value()) {
    throw std::runtime_error(parsed.diagnostics.empty() ? "loader failed" : parsed.diagnostics.front());
  }
  return *parsed.program;
}

bool contains_line(const std::vector<std::string>& lines, std::string_view needle) {
  return std::any_of(lines.begin(), lines.end(), [needle](const std::string& line) {
    return line.find(needle) != std::string::npos;
  });
}

}  // namespace

int main() {
  const auto program = load_program(
      "main:\n"
      "  j core0\n"
      "core0:\n"
      "  addiu $t1, $zero, 1\n"
      "  sw $t1, 0($zero)\n"
      "  sw $t1, 4($zero)\n"
      "  lw $v0, 0($zero)\n"
      "  jr $ra\n"
      "core1:\n"
      "  addiu $t0, $zero, 4\n"
      "wait:\n"
      "  lw $t2, 0($t0)\n"
      "  beq $t2, $zero, wait\n"
      "  addiu $t0, $zero, 0\n"
      "  lw $v0, 0($t0)\n"
      "  jr $ra\n");

  const auto sc = nexus::sim::parallel::run_program(
      program,
      {
          .trace = true,
          .cores = 2,
          .consistency = nexus::sim::parallel::ConsistencyKind::Sequential,
      });
  const auto weak = nexus::sim::parallel::run_program(
      program,
      {
          .trace = true,
          .cores = 2,
          .consistency = nexus::sim::parallel::ConsistencyKind::WeakLite,
      });

  if (!sc.success || !weak.success || sc.exit_code != 1 || weak.exit_code != 1) {
    std::cerr << "Consistency demo did not execute correctly.\n";
    return 1;
  }

  if (weak.cycles <= sc.cycles || weak.store_buffer_flushes != 2U ||
      !contains_line(weak.system_lines, "consistency[weak-lite]: drain core0 address=0 value=1") ||
      !contains_line(weak.system_lines, "consistency[weak-lite]: drain core0 address=4 value=1")) {
    std::cerr << "Weak-lite consistency model did not expose buffered stores.\n";
    return 1;
  }

  return 0;
}
