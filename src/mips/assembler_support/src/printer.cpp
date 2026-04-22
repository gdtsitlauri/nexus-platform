#include "nexus/mips/assembler_support/printer.hpp"

#include <sstream>

namespace nexus::mips::assembler_support {

void append_blank_line(TextProgram& program) {
  program.lines.push_back(TextLine{.blank = true, .label = {}, .instruction = std::nullopt});
}

void append_label(TextProgram& program, const std::string& label) {
  program.lines.push_back(TextLine{.blank = false, .label = label, .instruction = std::nullopt});
}

void append_instruction(
    TextProgram& program,
    const std::string& opcode,
    const std::vector<std::string>& operands,
    const std::string& comment) {
  program.lines.push_back(TextLine{
      .blank = false,
      .label = {},
      .instruction = TextInstruction{.opcode = opcode, .operands = operands, .comment = comment}});
}

std::string print_program(const TextProgram& program) {
  std::ostringstream output;
  for (const TextLine& line : program.lines) {
    if (line.blank) {
      output << '\n';
      continue;
    }

    if (!line.label.empty()) {
      output << line.label << ":\n";
    }

    if (!line.instruction.has_value()) {
      continue;
    }

    output << "  " << line.instruction->opcode;
    if (!line.instruction->operands.empty()) {
      output << ' ';
      for (std::size_t index = 0; index < line.instruction->operands.size(); ++index) {
        if (index != 0) {
          output << ", ";
        }
        output << line.instruction->operands[index];
      }
    }
    if (!line.instruction->comment.empty()) {
      output << "  # " << line.instruction->comment;
    }
    output << '\n';
  }
  return output.str();
}

}  // namespace nexus::mips::assembler_support
