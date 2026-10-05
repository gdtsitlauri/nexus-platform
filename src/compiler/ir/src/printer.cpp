#include "nexus/compiler/ir/printer.hpp"

#include <sstream>

namespace nexus::compiler::ir {

namespace {

std::string format_argument(const Function& function, const CallArgument& argument) {
  if (argument.kind == CallArgumentKind::Value) {
    return value_name(argument.value);
  }

  std::ostringstream output;
  output << local_info(function, argument.local).name;
  for (ValueId index : argument.indices) {
    output << '[' << value_name(index) << ']';
  }
  return output.str();
}

std::string format_indices(const std::vector<ValueId>& indices) {
  std::ostringstream output;
  for (ValueId index : indices) {
    output << '[' << value_name(index) << ']';
  }
  return output.str();
}

void print_instruction(const Function& function, const Instruction& instruction, std::ostringstream& output) {
  output << "    ";
  if (instruction.result.has_value()) {
    output << value_name(*instruction.result) << ": " << type_to_string(instruction.result_type)
           << " = ";
  }

  switch (instruction.kind) {
    case InstructionKind::ConstInt:
      output << "const_int " << instruction.int_immediate;
      return;
    case InstructionKind::ConstBool:
      output << "const_bool " << (instruction.bool_immediate ? "true" : "false");
      return;
    case InstructionKind::LoadLocal:
      output << "load " << local_info(function, instruction.local).name;
      return;
    case InstructionKind::StoreLocal:
      output << "store " << local_info(function, instruction.local).name << ", "
             << value_name(instruction.operands.front());
      return;
    case InstructionKind::LoadElement:
      output << "load_element " << local_info(function, instruction.local).name
             << format_indices(instruction.operands);
      return;
    case InstructionKind::StoreElement: {
      const std::size_t index_count = instruction.operands.size() - 1;
      std::vector<ValueId> indices(
          instruction.operands.begin(), instruction.operands.begin() + static_cast<std::ptrdiff_t>(index_count));
      output << "store_element " << local_info(function, instruction.local).name
             << format_indices(indices) << ", " << value_name(instruction.operands.back());
      return;
    }
    case InstructionKind::Unary:
      output << unary_op_name(instruction.unary_op) << ' ' << value_name(instruction.operands.front());
      return;
    case InstructionKind::Binary:
      output << binary_op_name(instruction.binary_op) << ' ' << value_name(instruction.operands[0])
             << ", " << value_name(instruction.operands[1]);
      return;
    case InstructionKind::Call:
      output << "call " << instruction.callee << '(';
      for (std::size_t index = 0; index < instruction.call_arguments.size(); ++index) {
        if (index != 0) {
          output << ", ";
        }
        output << format_argument(function, instruction.call_arguments[index]);
      }
      output << ')';
      return;
  }
}

void print_terminator(const Terminator& terminator, std::ostringstream& output) {
  output << "    ";
  switch (terminator.kind) {
    case TerminatorKind::Jump:
      output << "jump bb" << terminator.true_target;
      return;
    case TerminatorKind::Branch:
      output << "branch " << value_name(*terminator.condition) << ", bb" << terminator.true_target
             << ", bb" << terminator.false_target;
      return;
    case TerminatorKind::Return:
      if (terminator.return_value.has_value()) {
        output << "return " << value_name(*terminator.return_value);
      } else {
        output << "return";
      }
      return;
  }
}

}  // namespace

std::string print_function(const Function& function) {
  std::ostringstream output;
  output << "func " << function.name << '(';
  for (std::size_t index = 0; index < function.parameters.size(); ++index) {
    if (index != 0) {
      output << ", ";
    }
    output << function.parameters[index].name << ": " << type_to_string(function.parameters[index].type);
  }
  output << ") -> " << type_to_string(function.return_type) << " {\n";

  if (!function.locals.empty()) {
    output << "  locals:\n";
    for (const LocalInfo& local : function.locals) {
      output << "    " << local.name << ": " << type_to_string(local.type);
      if (local.is_parameter) {
        output << " [param]";
      }
      output << '\n';
    }
  }

  for (const BasicBlock& block : function.blocks) {
    output << "  bb" << block.id << "." << block.label << ":\n";
    for (const Instruction& instruction : block.instructions) {
      print_instruction(function, instruction, output);
      output << '\n';
    }
    if (block.terminator.has_value()) {
      print_terminator(*block.terminator, output);
      output << '\n';
    }
  }

  output << "}\n";
  return output.str();
}

std::string print_module(const Module& module) {
  std::ostringstream output;
  for (std::size_t index = 0; index < module.functions.size(); ++index) {
    if (index != 0) {
      output << '\n';
    }
    output << print_function(module.functions[index]);
  }
  return output.str();
}

namespace {

struct Quad {
  std::string op;
  std::string arg1;
  std::string arg2;
  std::string result;
};

std::string block_ref(const Function& function, BlockId id) {
  for (const auto& block : function.blocks) {
    if (block.id == id) {
      return "bb" + std::to_string(id) + (block.label.empty() ? "" : "." + block.label);
    }
  }
  return "bb" + std::to_string(id);
}

std::string element_ref(const Function& function, LocalId local, const std::vector<ValueId>& indices) {
  std::string text = function.locals[local].name;
  for (const ValueId index : indices) {
    text += "[" + value_name(index) + "]";
  }
  return text;
}

}  // namespace

std::string print_quadruples(const Module& module) {
  std::ostringstream output;
  for (const auto& function : module.functions) {
    output << "func " << function.name << " quadruples:\n";
    output << "  #    op          arg1          arg2          result\n";
    std::size_t counter = 0;
    auto emit = [&](const Quad& quad) {
      std::string line = "  " + std::to_string(counter++);
      line.resize(7, ' ');
      line += quad.op;
      line.resize(19, ' ');
      line += quad.arg1;
      line.resize(33, ' ');
      line += quad.arg2;
      line.resize(47, ' ');
      line += quad.result;
      while (!line.empty() && line.back() == ' ') {
        line.pop_back();
      }
      output << line << '\n';
    };
    for (const auto& block : function.blocks) {
      output << "  " << block_ref(function, block.id) << ":\n";
      for (const auto& instruction : block.instructions) {
        const std::string result = instruction.result.has_value() ? value_name(*instruction.result) : "";
        switch (instruction.kind) {
          case InstructionKind::ConstInt:
            emit({"=", std::to_string(instruction.int_immediate), "", result});
            break;
          case InstructionKind::ConstBool:
            emit({"=", instruction.bool_immediate ? "true" : "false", "", result});
            break;
          case InstructionKind::LoadLocal:
            emit({"=", function.locals[instruction.local].name, "", result});
            break;
          case InstructionKind::StoreLocal:
            emit({"=", value_name(instruction.operands.front()), "", function.locals[instruction.local].name});
            break;
          case InstructionKind::LoadElement:
            emit({"=[]", element_ref(function, instruction.local, instruction.operands), "", result});
            break;
          case InstructionKind::StoreElement: {
            std::vector<ValueId> indices(instruction.operands.begin(), instruction.operands.end() - 1);
            emit({"[]=", value_name(instruction.operands.back()), "", element_ref(function, instruction.local, indices)});
            break;
          }
          case InstructionKind::Unary:
            emit({std::string(unary_op_name(instruction.unary_op)), value_name(instruction.operands.front()), "", result});
            break;
          case InstructionKind::Binary:
            emit({std::string(binary_op_name(instruction.binary_op)), value_name(instruction.operands[0]),
                  value_name(instruction.operands[1]), result});
            break;
          case InstructionKind::Call:
            for (const auto& argument : instruction.call_arguments) {
              emit({"param",
                    argument.kind == CallArgumentKind::Value ? value_name(argument.value)
                                                             : "&" + element_ref(function, argument.local, argument.indices),
                    "", ""});
            }
            emit({"call", instruction.callee, std::to_string(instruction.call_arguments.size()), result});
            break;
        }
      }
      if (!block.terminator.has_value()) {
        continue;
      }
      const auto& terminator = *block.terminator;
      switch (terminator.kind) {
        case TerminatorKind::Jump:
          emit({"goto", "", "", block_ref(function, terminator.true_target)});
          break;
        case TerminatorKind::Branch:
          emit({"if", value_name(*terminator.condition), "", block_ref(function, terminator.true_target)});
          emit({"goto", "", "", block_ref(function, terminator.false_target)});
          break;
        case TerminatorKind::Return:
          emit({"return", terminator.return_value.has_value() ? value_name(*terminator.return_value) : "", "", ""});
          break;
      }
    }
    output << '\n';
  }
  return output.str();
}

}  // namespace nexus::compiler::ir
