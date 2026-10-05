#include <iostream>
#include <stdexcept>
#include <string>

#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/advanced/model.hpp"
#include "nexus/sim/functional/interpreter.hpp"

namespace {

using nexus::sim::advanced::PredictorKind;
using nexus::sim::advanced::RunOptions;
using nexus::sim::advanced::RunResult;
using nexus::sim::advanced::SchedulerKind;

nexus::mips::loader::LoadedProgram load_program(const std::string& assembly) {
  const auto parsed = nexus::mips::loader::load_program_from_text(assembly);
  if (!parsed.diagnostics.empty() || !parsed.program.has_value()) {
    throw std::runtime_error(parsed.diagnostics.empty() ? "loader failed" : parsed.diagnostics.front());
  }
  return *parsed.program;
}

RunOptions tomasulo(std::size_t width = 1, std::size_t rob = 16) {
  RunOptions options;
  options.scheduler = SchedulerKind::Tomasulo;
  options.predictor = PredictorKind::TwoBit;
  options.issue_width = width;
  options.rob_entries = rob;
  return options;
}

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void expect_matches_functional(
    const nexus::mips::loader::LoadedProgram& program,
    const RunResult& result,
    const std::string& name) {
  const auto reference = nexus::sim::functional::run_program(program);
  expect(result.success, name + ": tomasulo run failed: " + result.error);
  expect(result.exit_code == reference.exit_code, name + ": exit code differs from the functional model");
  expect(result.registers == reference.registers, name + ": architectural registers differ");
}

}  // namespace

int main() {
  // 1. A long-latency divide followed by independent work: a larger window hides the latency.
  const auto latency_hiding = load_program(
      "main:\n"
      "  addiu $t0, $zero, 100\n"
      "  addiu $t1, $zero, 7\n"
      "  div $t0, $t1\n"
      "  mflo $t2\n"
      "  addu $t3, $t2, $t2\n"
      "  addiu $t4, $zero, 1\n"
      "  addiu $t5, $zero, 2\n"
      "  addiu $t6, $zero, 3\n"
      "  addiu $t7, $zero, 4\n"
      "  addu $t4, $t4, $t5\n"
      "  addu $t6, $t6, $t7\n"
      "  addu $v0, $t3, $t4\n"
      "  addu $v0, $v0, $t6\n"
      "  jr $ra\n");
  const auto narrow = nexus::sim::advanced::run_program(latency_hiding, tomasulo(1, 2));
  const auto wide = nexus::sim::advanced::run_program(latency_hiding, tomasulo(1, 16));
  expect_matches_functional(latency_hiding, wide, "latency hiding");
  expect(wide.exit_code == 38, "latency hiding: 100/7*2 + 3 + 7 should be 38");
  expect(wide.cycles < narrow.cycles, "a 16-entry ROB must beat a 2-entry ROB on independent work");
  expect(narrow.rob_full_stalls > 0, "a 2-entry ROB must report ROB-full stalls");

  // 2. WAW/WAR on the same architectural register is removed by renaming.
  const auto renaming = load_program(
      "main:\n"
      "  addiu $t0, $zero, 5\n"
      "  addu $t1, $t0, $t0\n"
      "  addiu $t0, $zero, 9\n"
      "  addu $t2, $t0, $t1\n"
      "  addiu $t0, $zero, 1\n"
      "  addu $v0, $t2, $t0\n"
      "  jr $ra\n");
  expect_matches_functional(renaming, nexus::sim::advanced::run_program(renaming, tomasulo(2)), "renaming");

  // 3. Store-to-load forwarding through the load/store queue.
  const auto forwarding = load_program(
      "main:\n"
      "  addiu $t0, $zero, 42\n"
      "  sw $t0, -4($sp)\n"
      "  lw $t1, -4($sp)\n"
      "  addiu $v0, $t1, 0\n"
      "  jr $ra\n");
  const auto forwarded = nexus::sim::advanced::run_program(forwarding, tomasulo());
  expect_matches_functional(forwarding, forwarded, "forwarding");
  expect(forwarded.load_forwards == 1, "the load must be satisfied by store-to-load forwarding");

  // 4. Call/return pairs are predicted by the return-address stack.
  const auto calls = load_program(
      "main:\n"
      "  addiu $sp, $sp, -8\n"
      "  sw $ra, 4($sp)\n"
      "  addiu $a0, $zero, 3\n"
      "  jal inc\n"
      "  addiu $a0, $v0, 0\n"
      "  jal inc\n"
      "  addiu $a0, $v0, 0\n"
      "  jal inc\n"
      "  lw $ra, 4($sp)\n"
      "  addiu $sp, $sp, 8\n"
      "  jr $ra\n"
      "inc:\n"
      "  addiu $v0, $a0, 1\n"
      "  jr $ra\n");
  const auto returns = nexus::sim::advanced::run_program(calls, tomasulo());
  expect_matches_functional(calls, returns, "calls");
  expect(returns.exit_code == 6, "calls: 3 + 1 + 1 + 1 should be 6");
  expect(returns.return_predictions >= 3 && returns.return_mispredictions == 0, "every return must hit in the RAS");

  // 5. A loop: wider issue raises IPC and a deeper pipeline (larger redirect penalty) costs cycles.
  const auto loop = load_program(
      "main:\n"
      "  addiu $t0, $zero, 0\n"
      "  addiu $t1, $zero, 0\n"
      "  addiu $t2, $zero, 40\n"
      "loop:\n"
      "  addu $t1, $t1, $t0\n"
      "  addiu $t3, $t0, 3\n"
      "  addiu $t4, $t0, 5\n"
      "  xor $t5, $t3, $t4\n"
      "  addiu $t0, $t0, 1\n"
      "  bne $t0, $t2, loop\n"
      "  addu $v0, $t1, $t5\n"
      "  jr $ra\n");
  auto shallow_options = tomasulo(1);
  auto deep_options = tomasulo(1);
  deep_options.predictor = shallow_options.predictor = PredictorKind::StaticNotTaken;
  deep_options.mispredict_penalty = 10;
  const auto shallow = nexus::sim::advanced::run_program(loop, shallow_options);
  const auto deep = nexus::sim::advanced::run_program(loop, deep_options);
  auto wide_options = tomasulo(4, 32);
  wide_options.reservation_stations = 8;
  wide_options.cdb_width = 4;
  const auto wide_loop = nexus::sim::advanced::run_program(loop, wide_options);
  const auto narrow_loop = nexus::sim::advanced::run_program(loop, tomasulo(1));
  expect_matches_functional(loop, wide_loop, "loop");
  expect(deep.cycles > shallow.cycles, "a larger mispredict penalty must cost cycles");
  expect(wide_loop.cycles < narrow_loop.cycles, "4-wide issue with 4 CDBs must beat 1-wide issue");

  // 6. Two SMT threads share the back end and finish sooner than running back to back.
  auto smt_options = tomasulo(2, 32);
  smt_options.cdb_width = 2;
  smt_options.smt_program = &latency_hiding;
  const auto smt = nexus::sim::advanced::run_program(loop, smt_options);
  auto solo_options = tomasulo(2, 32);
  solo_options.cdb_width = 2;
  const auto solo_loop = nexus::sim::advanced::run_program(loop, solo_options);
  const auto solo_div = nexus::sim::advanced::run_program(latency_hiding, solo_options);
  expect(smt.success, "SMT run failed: " + smt.error);
  expect(smt.thread_exit_codes.size() == 2 && smt.thread_exit_codes[0] == wide_loop.exit_code &&
             smt.thread_exit_codes[1] == 38,
         "each SMT thread must keep its own architectural result");
  expect(smt.thread_instructions.size() == 2 &&
             smt.executed_instructions == solo_loop.executed_instructions + solo_div.executed_instructions,
         "SMT must commit every instruction of both threads");
  expect(smt.cycles < solo_loop.cycles + solo_div.cycles, "SMT must overlap the two threads");

  if (failures == 0) {
    std::cout << "tomasulo_test: all checks passed\n";
  }
  return failures == 0 ? 0 : 1;
}
