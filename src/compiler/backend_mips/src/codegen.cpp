#include "nexus/compiler/backend_mips/codegen.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "nexus/compiler/backend_mips/regalloc.hpp"
#include "nexus/mips/assembler_support/printer.hpp"
#include "nexus/mips/isa/instruction.hpp"

namespace nexus::compiler::backend_mips {

namespace {

using ir::BinaryOp;
using ir::BlockId;
using ir::Function;
using ir::Instruction;
using ir::InstructionKind;
using ir::LocalId;
using ir::Terminator;
using ir::TerminatorKind;
using ir::ValueId;
using mips::assembler_support::TextProgram;
using mips::isa::Register;

constexpr Register kScratch0 = Register::T0;
constexpr Register kScratch1 = Register::T1;
constexpr Register kScratch2 = Register::T2;
constexpr Register kScratch3 = Register::T3;

std::string reg(Register reg) {
  return std::string(mips::isa::register_name(reg));
}

std::string mem(int offset, Register base = Register::FP) {
  return std::to_string(offset) + "(" + reg(base) + ")";
}

int align_to(int value, int alignment) {
  const int remainder = value % alignment;
  return remainder == 0 ? value : value + alignment - remainder;
}

std::string sanitize_label(std::string text) {
  for (char& ch : text) {
    if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
      ch = '_';
    }
  }
  return text;
}

std::int32_t word_count_for_type(const ir::Type& type) {
  if (!type.is_array()) {
    return 1;
  }
  std::int64_t words = 1;
  for (const std::int64_t extent : type.array_extents) {
    words *= extent;
  }
  return static_cast<std::int32_t>(words);
}

struct LocalStorage {
  int offset = 0;
  int size_bytes = 4;
  bool pointer_slot = false;
};

struct FrameLayout {
  std::vector<LocalStorage> locals;
  std::vector<int> values;
  std::vector<int> saved_callee_offsets;
  int saved_fp_offset = 0;
  int saved_ra_offset = 0;
  int frame_size = 0;
};

FrameLayout build_frame_layout(const Function& function, std::size_t callee_saved_count) {
  FrameLayout layout;
  layout.locals.resize(function.locals.size());
  layout.values.resize(function.values.size());

  int cursor = 0;
  for (const auto& local : function.locals) {
    LocalStorage storage;
    storage.offset = cursor;
    storage.pointer_slot = local.is_parameter && local.type.is_array();
    storage.size_bytes = storage.pointer_slot ? 4 : word_count_for_type(local.type) * 4;
    layout.locals[local.id] = storage;
    cursor += storage.size_bytes;
  }

  for (const auto& value : function.values) {
    layout.values[value.id] = cursor;
    cursor += 4;
  }

  for (std::size_t index = 0; index < callee_saved_count; ++index) {
    layout.saved_callee_offsets.push_back(cursor);
    cursor += 4;
  }

  cursor = align_to(cursor, 8);
  layout.saved_fp_offset = cursor;
  layout.saved_ra_offset = cursor + 4;
  layout.frame_size = align_to(cursor + 8, 8);
  return layout;
}

class FunctionEmitter {
 public:
  FunctionEmitter(const Function& function, std::optional<RegisterAllocation> allocation)
      : function_(function),
        allocation_(std::move(allocation)),
        frame_(build_frame_layout(function, allocation_ ? allocation_->used_callee_saved.size() : 0U)) {}

  void emit(TextProgram& program) {
    build_labels();
    mips::assembler_support::append_label(program, function_.name);
    if (allocation_.has_value()) {
      std::size_t in_registers = 0;
      for (const auto& interval : allocation_->intervals) {
        in_registers += interval.reg.has_value() ? 1U : 0U;
      }
      mips::assembler_support::append_comment(
          program,
          "linear-scan: " + std::to_string(in_registers) + " of " + std::to_string(allocation_->intervals.size()) +
              " intervals in registers, " + std::to_string(allocation_->spilled) + " spilled");
    }
    emit_prologue(program);
    emit_blocks(program);
    emit_epilogue(program);
    mips::assembler_support::append_blank_line(program);
  }

  const std::vector<std::string>& diagnostics() const { return diagnostics_; }

 private:
  void build_labels() {
    for (const auto& block : function_.blocks) {
      if (block.id == function_.entry_block) {
        block_labels_[block.id] = function_.name;
      } else {
        block_labels_[block.id] =
            function_.name + "$bb" + std::to_string(block.id) + "_" + sanitize_label(block.label);
      }
    }
    epilogue_label_ = function_.name + "$epilogue";
  }

  void emit_prologue(TextProgram& program) {
    using namespace mips::assembler_support;
    append_instruction(
        program,
        "addiu",
        {reg(Register::SP), reg(Register::SP), std::to_string(-frame_.frame_size)},
        "allocate " + std::to_string(frame_.frame_size) + "-byte frame");
    append_instruction(program, "sw", {reg(Register::RA), mem(frame_.saved_ra_offset, Register::SP)});
    append_instruction(program, "sw", {reg(Register::FP), mem(frame_.saved_fp_offset, Register::SP)});
    append_instruction(program, "or", {reg(Register::FP), reg(Register::SP), reg(Register::Zero)});
    if (allocation_.has_value()) {
      for (std::size_t index = 0; index < allocation_->used_callee_saved.size(); ++index) {
        append_instruction(
            program,
            "sw",
            {reg(allocation_->used_callee_saved[index]), mem(frame_.saved_callee_offsets[index])},
            "save callee-saved register");
      }
    }

    for (std::size_t index = 0; index < function_.parameters.size(); ++index) {
      if (index >= 4) {
        diagnostics_.push_back(
            "Phase 4 backend supports at most 4 parameters per function; '" + function_.name +
            "' exceeds that bound");
        return;
      }
      const LocalId local = function_.parameters[index].local;
      if (const auto home = local_reg(local); home.has_value()) {
        append_instruction(
            program,
            "or",
            {reg(*home), reg(argument_register(index)), reg(Register::Zero)},
            "parameter " + function_.parameters[index].name + " lives in a register");
        continue;
      }
      append_instruction(
          program,
          "sw",
          {reg(argument_register(index)), mem(frame_.locals[local].offset)},
          "save parameter " + function_.parameters[index].name);
    }
  }

  void emit_blocks(TextProgram& program) {
    for (const auto& block : function_.blocks) {
      if (block.id != function_.entry_block) {
        mips::assembler_support::append_label(program, block_labels_.at(block.id));
      }

      for (const Instruction& instruction : block.instructions) {
        emit_instruction(program, instruction);
      }
      if (block.terminator.has_value()) {
        emit_terminator(program, *block.terminator);
      }
    }
  }

  void emit_epilogue(TextProgram& program) {
    using namespace mips::assembler_support;
    append_label(program, epilogue_label_);
    if (allocation_.has_value()) {
      for (std::size_t index = 0; index < allocation_->used_callee_saved.size(); ++index) {
        append_instruction(
            program, "lw", {reg(allocation_->used_callee_saved[index]), mem(frame_.saved_callee_offsets[index])});
      }
    }
    append_instruction(program, "lw", {reg(Register::FP), mem(frame_.saved_fp_offset, Register::SP)});
    append_instruction(program, "lw", {reg(Register::RA), mem(frame_.saved_ra_offset, Register::SP)});
    append_instruction(
        program, "addiu", {reg(Register::SP), reg(Register::SP), std::to_string(frame_.frame_size)});
    append_instruction(program, "jr", {reg(Register::RA)});
  }

  void emit_instruction(TextProgram& program, const Instruction& instruction) {
    switch (instruction.kind) {
      case InstructionKind::ConstInt: {
        const Register target = result_register(*instruction.result, kScratch0);
        emit_load_immediate(program, target, static_cast<std::int32_t>(instruction.int_immediate));
        commit_value(program, *instruction.result, target);
        return;
      }
      case InstructionKind::ConstBool: {
        const Register target = result_register(*instruction.result, kScratch0);
        emit_load_immediate(program, target, instruction.bool_immediate ? 1 : 0);
        commit_value(program, *instruction.result, target);
        return;
      }
      case InstructionKind::LoadLocal: {
        const Register target = result_register(*instruction.result, kScratch0);
        load_local_scalar(program, instruction.local, target);
        commit_value(program, *instruction.result, target);
        return;
      }
      case InstructionKind::StoreLocal:
        store_local_scalar(program, instruction.local, operand_register(program, instruction.operands.front(), kScratch0));
        return;
      case InstructionKind::LoadElement:
        compute_element_address(program, instruction.local, instruction.operands, kScratch0, kScratch1, kScratch2);
        mips::assembler_support::append_instruction(program, "lw", {reg(kScratch1), mem(0, kScratch0)});
        store_value(program, *instruction.result, kScratch1);
        return;
      case InstructionKind::StoreElement: {
        std::vector<ValueId> indices(
            instruction.operands.begin(), instruction.operands.end() - 1);
        compute_element_address(program, instruction.local, indices, kScratch0, kScratch1, kScratch2);
        load_value(program, instruction.operands.back(), kScratch1);
        mips::assembler_support::append_instruction(program, "sw", {reg(kScratch1), mem(0, kScratch0)});
        return;
      }
      case InstructionKind::Unary:
        emit_unary(program, instruction);
        return;
      case InstructionKind::Binary:
        emit_binary(program, instruction);
        return;
      case InstructionKind::Call:
        emit_call(program, instruction);
        return;
    }
  }

  void emit_terminator(TextProgram& program, const Terminator& terminator) {
    switch (terminator.kind) {
      case TerminatorKind::Jump:
        mips::assembler_support::append_instruction(
            program, "j", {block_labels_.at(terminator.true_target)});
        return;
      case TerminatorKind::Branch: {
        const Register condition = operand_register(program, *terminator.condition, kScratch0);
        mips::assembler_support::append_instruction(
            program, "bne", {reg(condition), reg(Register::Zero), block_labels_.at(terminator.true_target)});
        mips::assembler_support::append_instruction(program, "j", {block_labels_.at(terminator.false_target)});
        return;
      }
      case TerminatorKind::Return:
        if (terminator.return_value.has_value()) {
          load_value(program, *terminator.return_value, Register::V0);
        } else {
          mips::assembler_support::append_instruction(
              program, "or", {reg(Register::V0), reg(Register::Zero), reg(Register::Zero)});
        }
        mips::assembler_support::append_instruction(program, "j", {epilogue_label_});
        return;
    }
  }

  void emit_unary(TextProgram& program, const Instruction& instruction) {
    const Register source = operand_register(program, instruction.operands.front(), kScratch0);
    const Register target = result_register(*instruction.result, kScratch1);
    switch (instruction.unary_op) {
      case ir::UnaryOp::Negate:
        mips::assembler_support::append_instruction(
            program, "sub", {reg(target), reg(Register::Zero), reg(source)});
        break;
      case ir::UnaryOp::LogicalNot:
        mips::assembler_support::append_instruction(
            program, "xori", {reg(target), reg(source), "1"});
        break;
    }
    commit_value(program, *instruction.result, target);
  }

  void emit_binary(TextProgram& program, const Instruction& instruction) {
    const Register lhs = operand_register(program, instruction.operands[0], kScratch0);
    const Register rhs = operand_register(program, instruction.operands[1], kScratch1);
    const Register target = result_register(*instruction.result, kScratch2);

    switch (instruction.binary_op) {
      case BinaryOp::Add:
        mips::assembler_support::append_instruction(
            program, "addu", {reg(target), reg(lhs), reg(rhs)});
        break;
      case BinaryOp::Sub:
        mips::assembler_support::append_instruction(
            program, "sub", {reg(target), reg(lhs), reg(rhs)});
        break;
      case BinaryOp::Mul:
        mips::assembler_support::append_instruction(program, "mult", {reg(lhs), reg(rhs)});
        mips::assembler_support::append_instruction(program, "mflo", {reg(target)});
        break;
      case BinaryOp::Div:
        mips::assembler_support::append_instruction(program, "div", {reg(lhs), reg(rhs)});
        mips::assembler_support::append_instruction(program, "mflo", {reg(target)});
        break;
      case BinaryOp::Mod:
        mips::assembler_support::append_instruction(program, "div", {reg(lhs), reg(rhs)});
        mips::assembler_support::append_instruction(program, "mfhi", {reg(target)});
        break;
      case BinaryOp::Less:
        mips::assembler_support::append_instruction(
            program, "slt", {reg(target), reg(lhs), reg(rhs)});
        break;
      case BinaryOp::LessEqual:
        mips::assembler_support::append_instruction(
            program, "slt", {reg(target), reg(rhs), reg(lhs)});
        mips::assembler_support::append_instruction(
            program, "xori", {reg(target), reg(target), "1"});
        break;
      case BinaryOp::Greater:
        mips::assembler_support::append_instruction(
            program, "slt", {reg(target), reg(rhs), reg(lhs)});
        break;
      case BinaryOp::GreaterEqual:
        mips::assembler_support::append_instruction(
            program, "slt", {reg(target), reg(lhs), reg(rhs)});
        mips::assembler_support::append_instruction(
            program, "xori", {reg(target), reg(target), "1"});
        break;
      case BinaryOp::Equal:
        mips::assembler_support::append_instruction(
            program, "xor", {reg(target), reg(lhs), reg(rhs)});
        mips::assembler_support::append_instruction(
            program, "sltiu", {reg(target), reg(target), "1"});
        break;
      case BinaryOp::NotEqual:
        mips::assembler_support::append_instruction(
            program, "xor", {reg(target), reg(lhs), reg(rhs)});
        mips::assembler_support::append_instruction(
            program, "sltu", {reg(target), reg(Register::Zero), reg(target)});
        break;
      case BinaryOp::LogicalAnd:
        mips::assembler_support::append_instruction(
            program, "and", {reg(target), reg(lhs), reg(rhs)});
        break;
      case BinaryOp::LogicalOr:
        mips::assembler_support::append_instruction(
            program, "or", {reg(target), reg(lhs), reg(rhs)});
        break;
    }

    commit_value(program, *instruction.result, target);
  }

  void emit_call(TextProgram& program, const Instruction& instruction) {
    if (instruction.call_arguments.size() > 4) {
      diagnostics_.push_back(
          "Phase 4 backend supports at most 4 call arguments; call to '" + instruction.callee +
          "' exceeds that bound");
      return;
    }

    for (std::size_t index = 0; index < instruction.call_arguments.size(); ++index) {
      const auto& argument = instruction.call_arguments[index];
      const Register target = argument_register(index);
      if (argument.kind == ir::CallArgumentKind::Value) {
        load_value(program, argument.value, target);
      } else {
        compute_local_reference_address(program, argument.local, argument.indices, target, kScratch1, kScratch2);
      }
    }

    mips::assembler_support::append_instruction(program, "jal", {instruction.callee});
    if (instruction.result.has_value()) {
      store_value(program, *instruction.result, Register::V0);
    }
  }

  void emit_load_immediate(TextProgram& program, Register target, std::int32_t value) {
    if (value >= -32768 && value <= 32767) {
      mips::assembler_support::append_instruction(
          program, "addiu", {reg(target), reg(Register::Zero), std::to_string(value)});
      return;
    }

    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    const std::uint32_t upper = (bits >> 16U) & 0xffffU;
    const std::uint32_t lower = bits & 0xffffU;
    mips::assembler_support::append_instruction(
        program, "lui", {reg(target), std::to_string(static_cast<std::uint32_t>(upper))});
    if (lower != 0U) {
      mips::assembler_support::append_instruction(
          program, "ori", {reg(target), reg(target), std::to_string(static_cast<std::uint32_t>(lower))});
    }
  }

  [[nodiscard]] std::optional<Register> value_reg(ValueId value) const {
    return allocation_.has_value() ? allocation_->value_regs[value] : std::nullopt;
  }

  [[nodiscard]] std::optional<Register> local_reg(LocalId local) const {
    return allocation_.has_value() ? allocation_->local_regs[local] : std::nullopt;
  }

  static void emit_move(TextProgram& program, Register target, Register source) {
    if (target != source) {
      mips::assembler_support::append_instruction(program, "or", {reg(target), reg(source), reg(Register::Zero)});
    }
  }

  // Register that holds `value`: its allocated home, or `scratch` after a reload from the frame.
  Register operand_register(TextProgram& program, ValueId value, Register scratch) {
    if (const auto home = value_reg(value); home.has_value()) {
      return *home;
    }
    load_value(program, value, scratch);
    return scratch;
  }

  // Register an instruction should write `value` into; spilled values go through `scratch`.
  [[nodiscard]] Register result_register(ValueId value, Register scratch) const {
    return value_reg(value).value_or(scratch);
  }

  void commit_value(TextProgram& program, ValueId value, Register source) {
    if (!value_reg(value).has_value()) {
      store_value(program, value, source);
    }
  }

  void load_value(TextProgram& program, ValueId value, Register target) {
    if (const auto home = value_reg(value); home.has_value()) {
      emit_move(program, target, *home);
      return;
    }
    mips::assembler_support::append_instruction(
        program, "lw", {reg(target), mem(frame_.values[value])});
  }

  void store_value(TextProgram& program, ValueId value, Register source) {
    if (const auto home = value_reg(value); home.has_value()) {
      emit_move(program, *home, source);
      return;
    }
    mips::assembler_support::append_instruction(
        program, "sw", {reg(source), mem(frame_.values[value])});
  }

  void load_local_scalar(TextProgram& program, LocalId local, Register target) {
    if (const auto home = local_reg(local); home.has_value()) {
      emit_move(program, target, *home);
      return;
    }
    mips::assembler_support::append_instruction(
        program, "lw", {reg(target), mem(frame_.locals[local].offset)});
  }

  void store_local_scalar(TextProgram& program, LocalId local, Register source) {
    if (const auto home = local_reg(local); home.has_value()) {
      emit_move(program, *home, source);
      return;
    }
    mips::assembler_support::append_instruction(
        program, "sw", {reg(source), mem(frame_.locals[local].offset)});
  }

  void load_local_base_address(TextProgram& program, LocalId local, Register target) {
    const auto& storage = frame_.locals[local];
    if (storage.pointer_slot) {
      mips::assembler_support::append_instruction(
          program, "lw", {reg(target), mem(storage.offset)});
      return;
    }

    mips::assembler_support::append_instruction(
        program, "addiu", {reg(target), reg(Register::FP), std::to_string(storage.offset)});
  }

  void compute_local_reference_address(
      TextProgram& program,
      LocalId local,
      const std::vector<ValueId>& indices,
      Register target,
      Register scratch0,
      Register scratch1) {
    load_local_base_address(program, local, target);
    if (indices.empty()) {
      return;
    }

    const auto& type = function_.locals[local].type;
    if (indices.size() > type.array_extents.size()) {
      diagnostics_.push_back(
          "too many array indices emitted for local '" + function_.locals[local].name + "'");
      return;
    }

    load_value(program, indices.front(), scratch0);
    for (std::size_t index = 1; index < indices.size(); ++index) {
      emit_load_immediate(program, scratch1, static_cast<std::int32_t>(type.array_extents[index]));
      mips::assembler_support::append_instruction(program, "mult", {reg(scratch0), reg(scratch1)});
      mips::assembler_support::append_instruction(program, "mflo", {reg(scratch0)});
      load_value(program, indices[index], scratch1);
      mips::assembler_support::append_instruction(
          program, "addu", {reg(scratch0), reg(scratch0), reg(scratch1)});
    }

    // A partial index (e.g. passing row grid[i] of int[3][4]) selects a sub-array: scale the
    // combined index by the number of elements in that sub-array.
    std::int64_t sub_array_words = 1;
    for (std::size_t dimension = indices.size(); dimension < type.array_extents.size(); ++dimension) {
      sub_array_words *= type.array_extents[dimension];
    }
    if (sub_array_words != 1) {
      emit_load_immediate(program, scratch1, static_cast<std::int32_t>(sub_array_words));
      mips::assembler_support::append_instruction(program, "mult", {reg(scratch0), reg(scratch1)});
      mips::assembler_support::append_instruction(program, "mflo", {reg(scratch0)});
    }

    mips::assembler_support::append_instruction(program, "sll", {reg(scratch0), reg(scratch0), "2"});
    mips::assembler_support::append_instruction(
        program, "addu", {reg(target), reg(target), reg(scratch0)});
  }

  void compute_element_address(
      TextProgram& program,
      LocalId local,
      const std::vector<ValueId>& indices,
      Register target,
      Register scratch0,
      Register scratch1) {
    compute_local_reference_address(program, local, indices, target, scratch0, scratch1);
  }

  static Register argument_register(std::size_t index) {
    switch (index) {
      case 0:
        return Register::A0;
      case 1:
        return Register::A1;
      case 2:
        return Register::A2;
      case 3:
        return Register::A3;
      default:
        return Register::A3;
    }
  }

  const Function& function_;
  std::optional<RegisterAllocation> allocation_;
  FrameLayout frame_;
  std::unordered_map<BlockId, std::string> block_labels_;
  std::string epilogue_label_;
  std::vector<std::string> diagnostics_;
};

}  // namespace

CodegenResult lower_module(const ir::Module& module, const CodegenOptions& options) {
  CodegenResult result;
  TextProgram program;
  for (std::size_t index = 0; index < module.functions.size(); ++index) {
    std::optional<RegisterAllocation> allocation;
    if (options.register_allocation == RegisterAllocationMode::LinearScan) {
      allocation = allocate_registers_linear_scan(module.functions[index]);
    }
    FunctionEmitter emitter(module.functions[index], std::move(allocation));
    emitter.emit(program);
    result.diagnostics.insert(
        result.diagnostics.end(), emitter.diagnostics().begin(), emitter.diagnostics().end());
  }

  if (result.diagnostics.empty()) {
    result.program = std::move(program);
  }
  return result;
}

}  // namespace nexus::compiler::backend_mips
