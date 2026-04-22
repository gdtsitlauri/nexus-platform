#include <iostream>
#include <stdexcept>
#include <string>

#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/advanced/model.hpp"
#include "nexus/sim/advanced/predictor.hpp"
#include "nexus/sim/functional/interpreter.hpp"

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
  using nexus::sim::advanced::PredictorKind;
  using nexus::sim::advanced::SchedulerKind;

  const auto dual_issue_program = load_program(
      "main:\n"
      "  addiu $t0, $zero, 1\n"
      "  addiu $t1, $zero, 2\n"
      "  addiu $t2, $zero, 3\n"
      "  addiu $t3, $zero, 4\n"
      "  addu $v0, $t0, $t1\n"
      "  addu $v0, $v0, $t2\n"
      "  addu $v0, $v0, $t3\n"
      "  jr $ra\n");

  const auto functional_dual = nexus::sim::functional::run_program(dual_issue_program);
  const auto width1 = nexus::sim::advanced::run_program(
      dual_issue_program,
      {
          .predictor = PredictorKind::StaticNotTaken,
          .scheduler = SchedulerKind::InOrder,
          .issue_width = 1,
      });
  const auto width2 = nexus::sim::advanced::run_program(
      dual_issue_program,
      {
          .predictor = PredictorKind::StaticNotTaken,
          .scheduler = SchedulerKind::InOrder,
          .issue_width = 2,
      });

  if (!functional_dual.success || !width1.success || !width2.success || functional_dual.exit_code != 10 ||
      width1.exit_code != 10 || width2.exit_code != 10 || width1.registers != functional_dual.registers ||
      width2.registers != functional_dual.registers) {
    std::cerr << "Advanced width experiment does not match functional execution.\n";
    return 1;
  }

  if (width1.cycles != 8U || width2.cycles != 6U || width2.issued_packets >= width1.issued_packets ||
      width2.issued_slots != width2.executed_instructions) {
    std::cerr << "Advanced issue-width experiment produced unexpected packetization.\n";
    return 1;
  }

  const auto vliw_program = load_program(
      "main:\n"
      "  addiu $t0, $zero, 1\n"
      "  addu $t1, $t0, $t0\n"
      "  addiu $t2, $zero, 2\n"
      "  addu $t3, $t2, $t2\n"
      "  addu $v0, $t1, $t3\n"
      "  jr $ra\n");

  const auto functional_vliw = nexus::sim::functional::run_program(vliw_program);
  const auto inorder_width2 = nexus::sim::advanced::run_program(
      vliw_program,
      {
          .predictor = PredictorKind::StaticNotTaken,
          .scheduler = SchedulerKind::InOrder,
          .issue_width = 2,
      });
  const auto vliw = nexus::sim::advanced::run_program(
      vliw_program,
      {
          .trace = true,
          .predictor = PredictorKind::StaticNotTaken,
          .scheduler = SchedulerKind::VliwLite,
          .issue_width = 2,
      });

  if (!functional_vliw.success || !inorder_width2.success || !vliw.success || functional_vliw.exit_code != 6 ||
      inorder_width2.exit_code != 6 || vliw.exit_code != 6 || inorder_width2.registers != functional_vliw.registers ||
      vliw.registers != functional_vliw.registers) {
    std::cerr << "VLIW-lite experiment does not match functional execution.\n";
    return 1;
  }

  if (inorder_width2.cycles != 5U || vliw.cycles != 4U || vliw.issued_packets != 4U ||
      vliw.trace_lines.size() != 4U ||
      vliw.trace_lines.front() !=
          "trace[advanced]: cycle=1 scheduler=vliw-lite issue_width=2 packet=[I0@pc0:addiu, I2@pc2:addiu] events=-" ||
      vliw.trace_lines[1] !=
          "trace[advanced]: cycle=2 scheduler=vliw-lite issue_width=2 packet=[I1@pc1:addu, I3@pc3:addu] events=-") {
    std::cerr << "VLIW-lite scheduler did not produce the expected deterministic schedule.\n";
    return 1;
  }

  const auto scoreboard_program = load_program(
      "main:\n"
      "  addiu $t0, $zero, 6\n"
      "  addiu $t1, $zero, 7\n"
      "  mult $t0, $t1\n"
      "  addiu $t2, $zero, 3\n"
      "  addiu $t3, $zero, 4\n"
      "  addu $v0, $t2, $t3\n"
      "  mflo $t4\n"
      "  addu $v0, $v0, $t4\n"
      "  jr $ra\n");

  const auto functional_scoreboard = nexus::sim::functional::run_program(scoreboard_program);
  const auto inorder_scoreboard = nexus::sim::advanced::run_program(
      scoreboard_program,
      {
          .predictor = PredictorKind::StaticNotTaken,
          .scheduler = SchedulerKind::InOrder,
          .issue_width = 1,
      });
  const auto scoreboard = nexus::sim::advanced::run_program(
      scoreboard_program,
      {
          .trace = true,
          .predictor = PredictorKind::StaticNotTaken,
          .scheduler = SchedulerKind::Scoreboard,
          .issue_width = 1,
      });

  if (!functional_scoreboard.success || !inorder_scoreboard.success || !scoreboard.success ||
      functional_scoreboard.exit_code != 49 || inorder_scoreboard.exit_code != 49 || scoreboard.exit_code != 49 ||
      inorder_scoreboard.registers != functional_scoreboard.registers ||
      scoreboard.registers != functional_scoreboard.registers) {
    std::cerr << "Scoreboard experiment does not match functional execution.\n";
    return 1;
  }

  if (scoreboard.cycles <= inorder_scoreboard.cycles || scoreboard.trace_lines.empty() ||
      scoreboard.trace_lines.front().find("scheduler=scoreboard") == std::string::npos ||
      scoreboard.trace_lines.back().find("wb(") == std::string::npos) {
    std::cerr << "Scoreboard experiment did not produce the expected bounded scheduling behavior.\n";
    return 1;
  }

  return 0;
}
