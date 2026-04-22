#include <iostream>
#include <stdexcept>
#include <string>

#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/functional/interpreter.hpp"
#include "nexus/sim/single_cycle/control.hpp"
#include "nexus/sim/single_cycle/model.hpp"

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
  using nexus::sim::single_cycle::ALUControl;

  const std::string control_program =
      "main:\n"
      "  lw $t0, 0($sp)\n"
      "  jr $ra\n";
  const auto control_loaded = load_program(control_program);
  const auto lw_control = nexus::sim::single_cycle::decode_control(control_loaded.instructions.front());
  if (!lw_control.mem_read || !lw_control.mem_to_reg || !lw_control.reg_write ||
      lw_control.alu_control != ALUControl::Add) {
    std::cerr << "Single-cycle hardwired control decode for lw is incorrect.\n";
    return 1;
  }

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
  const auto call_loaded = load_program(call_program);
  const auto functional = nexus::sim::functional::run_program(call_loaded);
  const auto single_cycle = nexus::sim::single_cycle::run_program(call_loaded);
  if (!functional.success || !single_cycle.success) {
    std::cerr << "Expected functional and single-cycle models to succeed.\n";
    return 1;
  }
  if (functional.exit_code != 42 || single_cycle.exit_code != 42) {
    std::cerr << "Single-cycle model returned the wrong exit code.\n";
    return 1;
  }
  if (functional.registers != single_cycle.registers) {
    std::cerr << "Single-cycle model architectural registers diverged from functional reference.\n";
    return 1;
  }
  if (single_cycle.cycles != single_cycle.executed_instructions ||
      single_cycle.executed_instructions != functional.executed_instructions) {
    std::cerr << "Single-cycle cycle accounting is incorrect.\n";
    return 1;
  }

  const std::string trace_program =
      "main:\n"
      "  addiu $v0, $zero, 7\n"
      "  jr $ra\n";
  const auto trace_loaded = load_program(trace_program);
  const auto trace_result =
      nexus::sim::single_cycle::run_program(trace_loaded, {.trace = true});
  if (!trace_result.success || trace_result.trace_lines.size() != 2U ||
      trace_result.trace_lines.front() !=
          "trace[single-cycle]: cycle=1 pc=0 opcode=addiu control={reg_write, imm, alu=add} next_pc=1 write=$v0<-7") {
    std::cerr << "Single-cycle trace format is not deterministic.\n";
    return 1;
  }

  return 0;
}
