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

}  // namespace

int main() {
  const auto program = load_program(
      "main:\n"
      "  j core0\n"
      "core0:\n"
      "  addiu $t0, $zero, 0\n"
      "  addiu $t1, $zero, 7\n"
      "  sw $t1, 0($t0)\n"
      "  lui $t2, 2\n"
      "  addiu $t3, $zero, 1\n"
      "  sw $t3, 8($t2)\n"
      "  lw $v0, 0($t0)\n"
      "  jr $ra\n"
      "core1:\n"
      "  addiu $t0, $zero, 0\n"
      "  lw $t4, 0($t0)\n"
      "  lui $t2, 2\n"
      "  addiu $t3, $zero, 1\n"
      "  sw $t3, 8($t2)\n"
      "  jr $ra\n");

  const auto snoop = nexus::sim::parallel::run_program(
      program,
      {
          .cores = 2,
          .coherence = nexus::sim::parallel::CoherenceKind::SnoopingLite,
          .consistency = nexus::sim::parallel::ConsistencyKind::Sequential,
      });
  const auto directory = nexus::sim::parallel::run_program(
      program,
      {
          .cores = 2,
          .coherence = nexus::sim::parallel::CoherenceKind::DirectoryLite,
          .consistency = nexus::sim::parallel::ConsistencyKind::Sequential,
      });

  if (!snoop.success || !directory.success || snoop.exit_code != 7 || directory.exit_code != 7) {
    std::cerr << "Coherence demo did not execute correctly.\n";
    return 1;
  }

  if (snoop.invalidations < 1U || snoop.coherence_events < 1U || snoop.directory_lookups != 0U) {
    std::cerr << "Snooping-lite coherence accounting is incorrect.\n";
    return 1;
  }

  if (directory.invalidations < 1U || directory.coherence_events < 1U || directory.directory_lookups < 1U) {
    std::cerr << "Directory-lite coherence accounting is incorrect.\n";
    return 1;
  }

  return 0;
}
