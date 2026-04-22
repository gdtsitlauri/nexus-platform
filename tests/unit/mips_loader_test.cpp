#include <iostream>
#include <string>

#include "nexus/mips/isa/instruction.hpp"
#include "nexus/mips/loader/parser.hpp"

int main() {
  using nexus::mips::isa::Opcode;
  using nexus::mips::isa::Register;

  const std::string simple_program =
      "main:\n"
      "  addiu $t0, $zero, 3\n"
      "  addiu $t1, $zero, 4\n"
      "  addu $v0, $t0, $t1\n"
      "  jr $ra\n";

  const auto simple_result = nexus::mips::loader::load_program_from_text(simple_program);
  if (!simple_result.diagnostics.empty() || !simple_result.program.has_value()) {
    std::cerr << "Expected simple assembly to parse successfully.\n";
    return 1;
  }
  if (simple_result.program->entry_point != 0 || simple_result.program->instructions.size() != 4U) {
    std::cerr << "Unexpected instruction count or entry point for simple assembly.\n";
    return 1;
  }
  if (simple_result.program->instructions[2].opcode != Opcode::Addu ||
      simple_result.program->instructions[2].rd != Register::V0 ||
      simple_result.program->instructions[2].rs != Register::T0 ||
      simple_result.program->instructions[2].rt != Register::T1) {
    std::cerr << "Unexpected parsed instruction fields for addu.\n";
    return 1;
  }

  const std::string branch_program =
      "main:\n"
      "  addiu $t0, $zero, 0\n"
      "  beq $t0, $zero, done\n"
      "  addiu $v0, $zero, 1\n"
      "done:\n"
      "  addiu $v0, $zero, 7\n"
      "  jr $ra\n";

  const auto branch_result = nexus::mips::loader::load_program_from_text(branch_program);
  if (!branch_result.diagnostics.empty() || !branch_result.program.has_value()) {
    std::cerr << "Expected branch assembly to parse successfully.\n";
    return 1;
  }
  if (branch_result.program->instructions[1].opcode != Opcode::Beq ||
      branch_result.program->instructions[1].target != 3U) {
    std::cerr << "Expected branch target to resolve to label index 3.\n";
    return 1;
  }

  const std::string invalid_program =
      "main:\n"
      "  bogus $v0, $zero, 1\n";
  const auto invalid_result = nexus::mips::loader::load_program_from_text(invalid_program);
  if (invalid_result.diagnostics.empty()) {
    std::cerr << "Expected invalid assembly to produce loader diagnostics.\n";
    return 1;
  }

  return 0;
}
