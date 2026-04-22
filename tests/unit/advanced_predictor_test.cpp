#include <iostream>
#include <stdexcept>
#include <string>

#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/advanced/model.hpp"
#include "nexus/sim/advanced/predictor.hpp"

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
  using nexus::sim::advanced::TwoBitPredictor;

  TwoBitPredictor predictor;
  if (predictor.counter(12U) != 1U || predictor.predict(12U)) {
    std::cerr << "Two-bit predictor should start weakly not taken.\n";
    return 1;
  }

  predictor.update(12U, true);
  if (predictor.counter(12U) != 2U || !predictor.predict(12U)) {
    std::cerr << "Two-bit predictor did not move to weakly taken after a taken branch.\n";
    return 1;
  }

  predictor.update(12U, true);
  predictor.update(12U, true);
  if (predictor.counter(12U) != 3U) {
    std::cerr << "Two-bit predictor should saturate at strongly taken.\n";
    return 1;
  }

  predictor.update(12U, false);
  if (predictor.counter(12U) != 2U || !predictor.predict(12U)) {
    std::cerr << "Two-bit predictor did not step down correctly after a not-taken branch.\n";
    return 1;
  }

  const auto loop_program = load_program(
      "main:\n"
      "  addiu $t0, $zero, 0\n"
      "  addiu $t1, $zero, 4\n"
      "loop:\n"
      "  addiu $t0, $t0, 1\n"
      "  bne $t0, $t1, loop\n"
      "  addu $v0, $t0, $zero\n"
      "  jr $ra\n");

  const auto static_result = nexus::sim::advanced::run_program(
      loop_program,
      {
          .predictor = PredictorKind::StaticNotTaken,
          .scheduler = nexus::sim::advanced::SchedulerKind::InOrder,
          .issue_width = 1,
      });
  const auto two_bit_result = nexus::sim::advanced::run_program(
      loop_program,
      {
          .predictor = PredictorKind::TwoBit,
          .scheduler = nexus::sim::advanced::SchedulerKind::InOrder,
          .issue_width = 1,
      });

  if (!static_result.success || !two_bit_result.success || static_result.exit_code != 4 ||
      two_bit_result.exit_code != 4) {
    std::cerr << "Advanced loop predictor experiment failed to execute.\n";
    return 1;
  }

  if (static_result.branch_predictions != 4U || static_result.branch_mispredictions != 3U) {
    std::cerr << "Static-not-taken predictor accounting is incorrect.\n";
    return 1;
  }

  if (two_bit_result.branch_predictions != 4U || two_bit_result.branch_mispredictions != 2U ||
      two_bit_result.cycles >= static_result.cycles) {
    std::cerr << "Two-bit predictor did not reduce mispredictions on the loop example.\n";
    return 1;
  }

  return 0;
}
