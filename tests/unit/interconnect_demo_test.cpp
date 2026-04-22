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
      "  addiu $t1, $zero, 9\n"
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
      "  jr $ra\n"
      "core2:\n"
      "  addiu $t0, $zero, 0\n"
      "  lw $t4, 0($t0)\n"
      "  lui $t2, 2\n"
      "  addiu $t3, $zero, 1\n"
      "  sw $t3, 8($t2)\n"
      "  jr $ra\n"
      "core3:\n"
      "  addiu $t0, $zero, 0\n"
      "  lw $t4, 0($t0)\n"
      "  lui $t2, 2\n"
      "  addiu $t3, $zero, 1\n"
      "  sw $t3, 8($t2)\n"
      "  jr $ra\n");

  const auto bus = nexus::sim::parallel::run_program(
      program,
      {
          .cores = 4,
          .coherence = nexus::sim::parallel::CoherenceKind::SnoopingLite,
          .interconnect = nexus::sim::parallel::InterconnectKind::Bus,
      });
  const auto sw = nexus::sim::parallel::run_program(
      program,
      {
          .cores = 4,
          .coherence = nexus::sim::parallel::CoherenceKind::SnoopingLite,
          .interconnect = nexus::sim::parallel::InterconnectKind::Switch,
      });
  const auto noc = nexus::sim::parallel::run_program(
      program,
      {
          .cores = 4,
          .coherence = nexus::sim::parallel::CoherenceKind::SnoopingLite,
          .interconnect = nexus::sim::parallel::InterconnectKind::NoCLite,
      });

  if (!bus.success || !sw.success || !noc.success || bus.exit_code != 9 || sw.exit_code != 9 || noc.exit_code != 9) {
    std::cerr << "Interconnect demo did not execute correctly.\n";
    return 1;
  }

  if (bus.interconnect_messages < 3U || sw.interconnect_messages < 3U || noc.interconnect_messages < 3U) {
    std::cerr << "Interconnect message accounting is too small for the sharing demo.\n";
    return 1;
  }

  if (!(bus.interconnect_cycles > sw.interconnect_cycles && bus.interconnect_cycles > noc.interconnect_cycles)) {
    std::cerr << "Interconnect comparison did not differentiate bus traffic.\n";
    return 1;
  }

  return 0;
}
