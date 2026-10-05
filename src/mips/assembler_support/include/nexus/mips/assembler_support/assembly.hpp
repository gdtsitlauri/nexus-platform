#pragma once

#include <optional>
#include <string>
#include <vector>

namespace nexus::mips::assembler_support {

struct TextInstruction {
  std::string opcode;
  std::vector<std::string> operands;
  std::string comment;
};

struct TextLine {
  bool blank = false;
  std::string label;
  std::optional<TextInstruction> instruction;
  std::string comment;
};

struct TextProgram {
  std::vector<TextLine> lines;
};

void append_blank_line(TextProgram& program);
void append_label(TextProgram& program, const std::string& label);
void append_comment(TextProgram& program, const std::string& comment);
void append_instruction(
    TextProgram& program,
    const std::string& opcode,
    const std::vector<std::string>& operands = {},
    const std::string& comment = {});

}  // namespace nexus::mips::assembler_support
