#include "nexus/mips/loader/parser.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>

namespace nexus::mips::loader {

namespace {

std::string trim(std::string text) {
  auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
  text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
  return text;
}

std::vector<std::string> split_operands(const std::string& text) {
  std::vector<std::string> operands;
  std::string current;
  for (char ch : text) {
    if (ch == ',') {
      operands.push_back(trim(current));
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  if (!current.empty()) {
    operands.push_back(trim(current));
  }
  return operands;
}

bool parse_int32(const std::string& text, std::int32_t& value) {
  try {
    std::size_t parsed = 0;
    const long long wide = std::stoll(text, &parsed, 10);
    if (parsed != text.size()) {
      return false;
    }
    if (wide < static_cast<long long>(std::numeric_limits<std::int32_t>::min()) ||
        wide > static_cast<long long>(std::numeric_limits<std::int32_t>::max())) {
      return false;
    }
    value = static_cast<std::int32_t>(wide);
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_memory_operand(
    const std::string& text,
    std::int32_t& offset,
    isa::Register& base,
    std::string& error) {
  const std::size_t lparen = text.find('(');
  const std::size_t rparen = text.find(')');
  if (lparen == std::string::npos || rparen == std::string::npos || rparen <= lparen + 1) {
    error = "expected offset(base) memory operand";
    return false;
  }

  if (!parse_int32(trim(text.substr(0, lparen)), offset)) {
    error = "invalid memory offset '" + text.substr(0, lparen) + "'";
    return false;
  }

  const auto reg = isa::parse_register(trim(text.substr(lparen + 1, rparen - lparen - 1)));
  if (!reg.has_value()) {
    error = "invalid base register in memory operand '" + text + "'";
    return false;
  }

  base = *reg;
  return true;
}

void append_error(std::vector<std::string>& diagnostics, std::size_t line, const std::string& message) {
  diagnostics.push_back("line " + std::to_string(line) + ": " + message);
}

std::optional<LoadedInstruction> parse_instruction(
    const std::string& text,
    std::size_t line_number,
    const std::unordered_map<std::string, std::size_t>& labels,
    std::vector<std::string>& diagnostics) {
  std::istringstream stream(text);
  std::string opcode_text;
  stream >> opcode_text;
  const auto opcode = isa::parse_opcode(opcode_text);
  if (!opcode.has_value()) {
    append_error(diagnostics, line_number, "unknown opcode '" + opcode_text + "'");
    return std::nullopt;
  }

  std::string operand_text;
  std::getline(stream, operand_text);
  const std::vector<std::string> operands = split_operands(trim(operand_text));

  LoadedInstruction instruction;
  instruction.opcode = *opcode;
  instruction.source_line = line_number;
  instruction.text = text;

  auto parse_reg_operand = [&](std::size_t index, isa::Register& out) -> bool {
    if (index >= operands.size()) {
      append_error(diagnostics, line_number, "missing register operand");
      return false;
    }
    const auto reg = isa::parse_register(operands[index]);
    if (!reg.has_value()) {
      append_error(diagnostics, line_number, "invalid register '" + operands[index] + "'");
      return false;
    }
    out = *reg;
    return true;
  };

  auto parse_label_operand = [&](std::size_t index, std::size_t& out) -> bool {
    if (index >= operands.size()) {
      append_error(diagnostics, line_number, "missing branch/jump label operand");
      return false;
    }
    const auto found = labels.find(operands[index]);
    if (found == labels.end()) {
      append_error(diagnostics, line_number, "unknown label '" + operands[index] + "'");
      return false;
    }
    out = found->second;
    return true;
  };

  auto parse_immediate_operand = [&](std::size_t index, std::int32_t& out) -> bool {
    if (index >= operands.size()) {
      append_error(diagnostics, line_number, "missing immediate operand");
      return false;
    }
    if (!parse_int32(operands[index], out)) {
      append_error(diagnostics, line_number, "invalid immediate '" + operands[index] + "'");
      return false;
    }
    return true;
  };

  switch (*opcode) {
    case isa::Opcode::Add:
    case isa::Opcode::Addu:
    case isa::Opcode::Sub:
    case isa::Opcode::And:
    case isa::Opcode::Or:
    case isa::Opcode::Xor:
    case isa::Opcode::Slt:
    case isa::Opcode::Sltu:
      if (operands.size() != 3 || !parse_reg_operand(0, instruction.rd) ||
          !parse_reg_operand(1, instruction.rs) || !parse_reg_operand(2, instruction.rt)) {
        append_error(diagnostics, line_number, "expected three register operands");
        return std::nullopt;
      }
      return instruction;
    case isa::Opcode::Addiu:
    case isa::Opcode::Ori:
    case isa::Opcode::Xori:
    case isa::Opcode::Sltiu:
      if (operands.size() != 3 || !parse_reg_operand(0, instruction.rt) ||
          !parse_reg_operand(1, instruction.rs) || !parse_immediate_operand(2, instruction.immediate)) {
        append_error(diagnostics, line_number, "expected rt, rs, immediate");
        return std::nullopt;
      }
      return instruction;
    case isa::Opcode::Sll:
      if (operands.size() != 3 || !parse_reg_operand(0, instruction.rd) ||
          !parse_reg_operand(1, instruction.rt) || !parse_immediate_operand(2, instruction.immediate)) {
        append_error(diagnostics, line_number, "expected rd, rt, shamt");
        return std::nullopt;
      }
      return instruction;
    case isa::Opcode::Lui:
      if (operands.size() != 2 || !parse_reg_operand(0, instruction.rt) ||
          !parse_immediate_operand(1, instruction.immediate)) {
        append_error(diagnostics, line_number, "expected rt, immediate");
        return std::nullopt;
      }
      return instruction;
    case isa::Opcode::Lw:
    case isa::Opcode::Sw: {
      if (operands.size() != 2 || !parse_reg_operand(0, instruction.rt)) {
        append_error(diagnostics, line_number, "expected rt, offset(base)");
        return std::nullopt;
      }
      std::string error;
      if (!parse_memory_operand(operands[1], instruction.immediate, instruction.rs, error)) {
        append_error(diagnostics, line_number, error);
        return std::nullopt;
      }
      return instruction;
    }
    case isa::Opcode::Beq:
    case isa::Opcode::Bne:
      if (operands.size() != 3 || !parse_reg_operand(0, instruction.rs) ||
          !parse_reg_operand(1, instruction.rt) || !parse_label_operand(2, instruction.target)) {
        append_error(diagnostics, line_number, "expected rs, rt, label");
        return std::nullopt;
      }
      return instruction;
    case isa::Opcode::J:
    case isa::Opcode::Jal:
      if (operands.size() != 1 || !parse_label_operand(0, instruction.target)) {
        append_error(diagnostics, line_number, "expected label operand");
        return std::nullopt;
      }
      return instruction;
    case isa::Opcode::Jr:
      if (operands.size() != 1 || !parse_reg_operand(0, instruction.rs)) {
        append_error(diagnostics, line_number, "expected source register");
        return std::nullopt;
      }
      return instruction;
    case isa::Opcode::Mult:
    case isa::Opcode::Div:
      if (operands.size() != 2 || !parse_reg_operand(0, instruction.rs) ||
          !parse_reg_operand(1, instruction.rt)) {
        append_error(diagnostics, line_number, "expected rs, rt");
        return std::nullopt;
      }
      return instruction;
    case isa::Opcode::Mflo:
    case isa::Opcode::Mfhi:
      if (operands.size() != 1 || !parse_reg_operand(0, instruction.rd)) {
        append_error(diagnostics, line_number, "expected destination register");
        return std::nullopt;
      }
      return instruction;
  }

  append_error(diagnostics, line_number, "unsupported opcode shape");
  return std::nullopt;
}

}  // namespace

ParseResult load_program_from_text(const std::string& text) {
  ParseResult result;
  std::vector<std::pair<std::size_t, std::string>> instruction_lines;
  std::unordered_map<std::string, std::size_t> labels;

  std::istringstream input(text);
  std::string raw_line;
  std::size_t line_number = 0;
  while (std::getline(input, raw_line)) {
    ++line_number;
    const std::size_t comment_pos = raw_line.find('#');
    std::string line = trim(raw_line.substr(0, comment_pos));
    if (line.empty()) {
      continue;
    }
    if (line.starts_with('.')) {
      continue;
    }

    while (true) {
      const std::size_t colon = line.find(':');
      if (colon == std::string::npos) {
        break;
      }
      const std::string label = trim(line.substr(0, colon));
      if (label.empty()) {
        append_error(result.diagnostics, line_number, "empty label");
        return result;
      }
      if (labels.contains(label)) {
        append_error(result.diagnostics, line_number, "duplicate label '" + label + "'");
        return result;
      }
      labels.emplace(label, instruction_lines.size());
      line = trim(line.substr(colon + 1));
      if (line.empty()) {
        break;
      }
    }

    if (!line.empty()) {
      instruction_lines.emplace_back(line_number, line);
    }
  }

  if (!labels.contains("main")) {
    append_error(result.diagnostics, 0, "program does not define a 'main' label");
    return result;
  }

  LoadedProgram program;
  program.entry_point = labels.at("main");
  program.labels = labels;
  for (const auto& [source_line, line] : instruction_lines) {
    auto parsed = parse_instruction(line, source_line, labels, result.diagnostics);
    if (!parsed.has_value()) {
      return result;
    }
    program.instructions.push_back(std::move(*parsed));
  }

  result.program = std::move(program);
  return result;
}

ParseResult load_program_from_file(const std::string& file_name) {
  std::ifstream input(file_name);
  ParseResult result;
  if (!input) {
    result.diagnostics.push_back("failed to open assembly file '" + file_name + "'");
    return result;
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return load_program_from_text(buffer.str());
}

}  // namespace nexus::mips::loader
