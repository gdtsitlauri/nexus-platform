#include <iostream>
#include <stdexcept>
#include <string>

#include "nexus/mips/isa/instruction.hpp"
#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/functional/interpreter.hpp"
#include "nexus/sim/multi_cycle/control.hpp"
#include "nexus/sim/multi_cycle/model.hpp"

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
  using nexus::mips::isa::Opcode;
  using nexus::sim::multi_cycle::ControlStyle;
  using nexus::sim::multi_cycle::State;

  nexus::mips::loader::LoadedInstruction load_word;
  load_word.opcode = Opcode::Lw;
  const auto hardwired_lw = nexus::sim::multi_cycle::build_hardwired_plan(load_word);
  if (hardwired_lw.size() != 5U || hardwired_lw.front().state != State::Fetch ||
      hardwired_lw.back().state != State::WritebackLoad) {
    std::cerr << "Hardwired control plan for lw is incorrect.\n";
    return 1;
  }

  nexus::mips::loader::LoadedInstruction jal;
  jal.opcode = Opcode::Jal;
  const auto micro_jal = nexus::sim::multi_cycle::build_microcode_plan(jal);
  if (micro_jal.size() != 3U || !micro_jal.back().signals.reg_write || !micro_jal.back().signals.link) {
    std::cerr << "Microcode control plan for jal is incorrect.\n";
    return 1;
  }

  const std::string memory_program =
      "main:\n"
      "  addiu $sp, $sp, -4\n"
      "  addiu $t0, $zero, 11\n"
      "  sw $t0, 0($sp)\n"
      "  lw $v0, 0($sp)\n"
      "  addiu $sp, $sp, 4\n"
      "  jr $ra\n";
  const auto loaded = load_program(memory_program);
  const auto functional = nexus::sim::functional::run_program(loaded);
  const auto hardwired =
      nexus::sim::multi_cycle::run_program(loaded, {.control = ControlStyle::Hardwired});
  const auto microcode =
      nexus::sim::multi_cycle::run_program(loaded, {.control = ControlStyle::Microcode});
  if (!functional.success || !hardwired.success || !microcode.success) {
    std::cerr << "Expected all reference and multi-cycle executions to succeed.\n";
    return 1;
  }
  if (functional.exit_code != 11 || hardwired.exit_code != 11 || microcode.exit_code != 11) {
    std::cerr << "Multi-cycle execution returned the wrong exit code.\n";
    return 1;
  }
  if (hardwired.registers != functional.registers || microcode.registers != functional.registers) {
    std::cerr << "Multi-cycle architectural registers diverged from functional reference.\n";
    return 1;
  }
  if (hardwired.executed_instructions != functional.executed_instructions ||
      microcode.executed_instructions != functional.executed_instructions) {
    std::cerr << "Multi-cycle instruction counting diverged from functional reference.\n";
    return 1;
  }
  if (hardwired.cycles != 24U || microcode.cycles != 24U) {
    std::cerr << "Multi-cycle cycle accounting is incorrect for the memory test program.\n";
    return 1;
  }

  const std::string trace_program =
      "main:\n"
      "  addiu $v0, $zero, 7\n"
      "  jr $ra\n";
  const auto trace_loaded = load_program(trace_program);
  const auto trace_result =
      nexus::sim::multi_cycle::run_program(
          trace_loaded,
          {.trace = true, .control = ControlStyle::Microcode});
  if (!trace_result.success || trace_result.trace_lines.size() != 7U ||
      trace_result.trace_lines.front() !=
          "trace[multi-cycle]: cycle=1 control=microcode state=fetch pc=0 opcode=addiu signals={ir_write, mem_read, pc_write} action=microcode IF" ||
      trace_result.trace_lines.back() !=
          "trace[multi-cycle]: cycle=7 control=microcode state=execute-jump-register pc=1 opcode=jr signals={pc_write} action=microcode JR: PC <- A") {
    std::cerr << "Multi-cycle trace format is not deterministic.\n";
    return 1;
  }

  return 0;
}
