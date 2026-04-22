#include <iostream>
#include <stdexcept>
#include <string>

#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/functional/interpreter.hpp"
#include "nexus/sim/pipeline/model.hpp"

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
  using nexus::sim::pipeline::PredictorKind;

  const auto trace_program = load_program(
      "main:\n"
      "  addiu $v0, $zero, 7\n"
      "  jr $ra\n");
  const auto trace_result =
      nexus::sim::pipeline::run_program(trace_program, {.trace = true, .timeline = true});
  if (!trace_result.success || trace_result.exit_code != 7 || trace_result.cycles != 6U ||
      trace_result.retired_instructions != 2U || trace_result.flushes != 0U ||
      trace_result.trace_lines.size() != 6U ||
      trace_result.trace_lines.front() !=
          "trace[pipeline]: cycle=1 IF=I0@pc0:addiu ID=- EX=- MEM=- WB=- events=-" ||
      trace_result.timeline_lines.size() != 4U ||
      trace_result.timeline_lines.front() != "timeline[pipeline]:") {
    std::cerr << "Pipeline trace/timeline baseline is incorrect.\n";
    return 1;
  }

  const auto forwarding_program = load_program(
      "main:\n"
      "  addiu $t0, $zero, 5\n"
      "  addiu $t1, $zero, 7\n"
      "  addu $t2, $t0, $t1\n"
      "  addu $v0, $t2, $t0\n"
      "  jr $ra\n");
  const auto forwarding_functional = nexus::sim::functional::run_program(forwarding_program);
  const auto forwarding_pipeline = nexus::sim::pipeline::run_program(forwarding_program, {.trace = true});
  if (!forwarding_functional.success || !forwarding_pipeline.success ||
      forwarding_pipeline.exit_code != 17 || forwarding_pipeline.cycles != 9U ||
      forwarding_pipeline.stall_cycles != 0U || forwarding_pipeline.forwarding_events != 3U ||
      forwarding_pipeline.registers != forwarding_functional.registers ||
      forwarding_pipeline.trace_lines[4].find("forward(rs<-MEM/WB(I0@pc0:addiu))") == std::string::npos ||
      forwarding_pipeline.trace_lines[5].find("forward(rs<-EX/MEM(I2@pc2:addu))") == std::string::npos) {
    std::cerr << "Pipeline forwarding behavior is incorrect.\n";
    return 1;
  }

  const auto load_use_program = load_program(
      "main:\n"
      "  addiu $sp, $sp, -4\n"
      "  addiu $t0, $zero, 11\n"
      "  sw $t0, 0($sp)\n"
      "  lw $t1, 0($sp)\n"
      "  addu $v0, $t1, $zero\n"
      "  addiu $sp, $sp, 4\n"
      "  jr $ra\n");
  const auto load_use_functional = nexus::sim::functional::run_program(load_use_program);
  const auto load_use_pipeline =
      nexus::sim::pipeline::run_program(load_use_program, {.trace = true, .timeline = true});
  if (!load_use_functional.success || !load_use_pipeline.success ||
      load_use_pipeline.exit_code != 11 || load_use_pipeline.cycles != 12U ||
      load_use_pipeline.stall_cycles != 1U || load_use_pipeline.load_use_stalls != 1U ||
      load_use_pipeline.forwarding_events != 3U ||
      load_use_pipeline.registers != load_use_functional.registers ||
      load_use_pipeline.trace_lines[5].find("stall(load-use on $t1)") == std::string::npos ||
      load_use_pipeline.timeline_lines[6].find("ID*") == std::string::npos) {
    std::cerr << "Pipeline load-use stall behavior is incorrect.\n";
    return 1;
  }

  const auto branch_program = load_program(
      "main:\n"
      "  addiu $t0, $zero, 1\n"
      "  beq $t0, $t0, taken\n"
      "  addiu $v0, $zero, 0\n"
      "taken:\n"
      "  addiu $v0, $zero, 7\n"
      "  jr $ra\n");
  const auto branch_functional = nexus::sim::functional::run_program(branch_program);
  const auto branch_not_taken = nexus::sim::pipeline::run_program(
      branch_program, {.trace = true, .timeline = true, .predictor = PredictorKind::StaticNotTaken});
  const auto branch_taken = nexus::sim::pipeline::run_program(
      branch_program, {.predictor = PredictorKind::StaticTaken});
  if (!branch_functional.success || !branch_not_taken.success || !branch_taken.success ||
      branch_not_taken.exit_code != 7 || branch_taken.exit_code != 7 ||
      branch_not_taken.cycles != 9U || branch_taken.cycles != 8U ||
      branch_not_taken.branch_predictions != 1U || branch_not_taken.branch_mispredictions != 1U ||
      branch_not_taken.flushes != 1U || branch_taken.branch_mispredictions != 0U ||
      branch_taken.flushes != 0U ||
      branch_not_taken.registers != branch_functional.registers ||
      branch_taken.registers != branch_functional.registers ||
      branch_not_taken.trace_lines[3].find("mispredict(beq -> pc=3)") == std::string::npos ||
      branch_not_taken.timeline_lines[4].find("ID!") == std::string::npos) {
    std::cerr << "Pipeline branch prediction / flush behavior is incorrect.\n";
    return 1;
  }

  return 0;
}
