#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/functional/interpreter.hpp"
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
  const auto scalar_program = load_program(
      "main:\n"
      "  addiu $t0, $zero, 1\n"
      "  addiu $t1, $zero, 2\n"
      "  addu $v0, $t0, $t1\n"
      "  jr $ra\n");

  const auto functional = nexus::sim::functional::run_program(scalar_program);
  const auto parallel_scalar =
      nexus::sim::parallel::run_program(scalar_program, {.cores = 1, .consistency = nexus::sim::parallel::ConsistencyKind::Sequential});
  if (!functional.success || !parallel_scalar.success || functional.exit_code != 3 || parallel_scalar.exit_code != 3 ||
      parallel_scalar.active_cores != 1U) {
    std::cerr << "Parallel mode did not preserve single-core functional behavior.\n";
    return 1;
  }

  const auto sync_program = load_program(
      "main:\n"
      "  j core0\n"
      "core0:\n"
      "  lui $t7, 2\n"
      "  addiu $t0, $zero, 1\n"
      "  sw $t0, 0($t7)\n"
      "  addiu $t1, $zero, 64\n"
      "  lw $t2, 0($t1)\n"
      "  addiu $t2, $t2, 1\n"
      "  sw $t2, 0($t1)\n"
      "  sw $t0, 4($t7)\n"
      "  sw $t0, 8($t7)\n"
      "  lw $v0, 64($zero)\n"
      "  jr $ra\n"
      "core1:\n"
      "  lui $t7, 2\n"
      "  addiu $t0, $zero, 1\n"
      "  sw $t0, 0($t7)\n"
      "  addiu $t1, $zero, 64\n"
      "  lw $t2, 0($t1)\n"
      "  addiu $t2, $t2, 1\n"
      "  sw $t2, 0($t1)\n"
      "  sw $t0, 4($t7)\n"
      "  sw $t0, 8($t7)\n"
      "  jr $ra\n");

  const auto sync_result = nexus::sim::parallel::run_program(
      sync_program,
      {
          .trace = true,
          .cores = 2,
          .coherence = nexus::sim::parallel::CoherenceKind::SnoopingLite,
          .consistency = nexus::sim::parallel::ConsistencyKind::Sequential,
          .interconnect = nexus::sim::parallel::InterconnectKind::Bus,
      });

  if (!sync_result.success || sync_result.exit_code != 2 || sync_result.lock_acquisitions != 2U ||
      sync_result.barrier_arrivals != 2U || sync_result.synchronization_events < 4U ||
      !contains_line(sync_result.system_lines, "sync[lock-contended]") ||
      !contains_line(sync_result.system_lines, "sync[barrier-release]")) {
    std::cerr << "Parallel synchronization demo produced unexpected results.\n";
    return 1;
  }

  const auto atomic_program = load_program(
      "main:\n"
      "  j core0\n"
      "core0:\n"
      "  lui $t7, 2\n"
      "  lw $t0, 12($t7)\n"
      "  sw $t0, 64($zero)\n"
      "  addiu $t1, $zero, 1\n"
      "  sw $t1, 8($t7)\n"
      "  lw $t2, 64($zero)\n"
      "  lw $t3, 68($zero)\n"
      "  addu $v0, $t2, $t3\n"
      "  jr $ra\n"
      "core1:\n"
      "  lui $t7, 2\n"
      "  lw $t0, 12($t7)\n"
      "  sw $t0, 68($zero)\n"
      "  addiu $t1, $zero, 1\n"
      "  sw $t1, 8($t7)\n"
      "  jr $ra\n");

  const auto atomic_result = nexus::sim::parallel::run_program(atomic_program, {.trace = true, .cores = 2});
  if (!atomic_result.success || atomic_result.exit_code != 1 || atomic_result.atomic_operations != 2U ||
      !contains_line(atomic_result.system_lines, "sync[atomic-fetch-inc]: core0 value=0") ||
      !contains_line(atomic_result.system_lines, "sync[atomic-fetch-inc]: core1 value=1")) {
    std::cerr << "Parallel atomic demo did not behave deterministically.\n";
    return 1;
  }

  return 0;
}
