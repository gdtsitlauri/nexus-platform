#include "nexus/compiler/isa_styles/isa_styles.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_map>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace nexus::compiler::isa_styles {

namespace {

using ir::BinaryOp;
using ir::BlockId;
using ir::Function;
using ir::Instruction;
using ir::InstructionKind;
using ir::LocalId;
using ir::TerminatorKind;
using ir::ValueId;

constexpr std::size_t kMemoryWords = 1U << 16;
constexpr std::size_t kStepLimit = 5'000'000;
constexpr std::size_t kNoTarget = std::numeric_limits<std::size_t>::max();

enum class Cond { Lt, Le, Gt, Ge, Eq, Ne };

Cond cond_of(BinaryOp op) {
  switch (op) {
    case BinaryOp::Less:
      return Cond::Lt;
    case BinaryOp::LessEqual:
      return Cond::Le;
    case BinaryOp::Greater:
      return Cond::Gt;
    case BinaryOp::GreaterEqual:
      return Cond::Ge;
    case BinaryOp::Equal:
      return Cond::Eq;
    default:
      return Cond::Ne;
  }
}

bool is_comparison(BinaryOp op) {
  return op == BinaryOp::Less || op == BinaryOp::LessEqual || op == BinaryOp::Greater ||
         op == BinaryOp::GreaterEqual || op == BinaryOp::Equal || op == BinaryOp::NotEqual;
}

Cond negate(Cond cond) {
  switch (cond) {
    case Cond::Lt:
      return Cond::Ge;
    case Cond::Le:
      return Cond::Gt;
    case Cond::Gt:
      return Cond::Le;
    case Cond::Ge:
      return Cond::Lt;
    case Cond::Eq:
      return Cond::Ne;
    case Cond::Ne:
      return Cond::Eq;
  }
  return Cond::Ne;
}

bool holds(Cond cond, std::int32_t lhs, std::int32_t rhs) {
  switch (cond) {
    case Cond::Lt:
      return lhs < rhs;
    case Cond::Le:
      return lhs <= rhs;
    case Cond::Gt:
      return lhs > rhs;
    case Cond::Ge:
      return lhs >= rhs;
    case Cond::Eq:
      return lhs == rhs;
    case Cond::Ne:
      return lhs != rhs;
  }
  return false;
}

std::string_view cond_suffix(Cond cond) {
  switch (cond) {
    case Cond::Lt:
      return "lt";
    case Cond::Le:
      return "le";
    case Cond::Gt:
      return "gt";
    case Cond::Ge:
      return "ge";
    case Cond::Eq:
      return "eq";
    case Cond::Ne:
      return "ne";
  }
  return "?";
}

std::string_view x86_cond_suffix(Cond cond) {
  switch (cond) {
    case Cond::Lt:
      return "l";
    case Cond::Le:
      return "le";
    case Cond::Gt:
      return "g";
    case Cond::Ge:
      return "ge";
    case Cond::Eq:
      return "e";
    case Cond::Ne:
      return "ne";
  }
  return "?";
}

// ---------------------------------------------------------------------------------------------
// Machine instructions (one enum for all three styles; each style uses its own subset).
// ---------------------------------------------------------------------------------------------
enum class Op {
  // stack (JVM-like)
  SConst, SLoad, SStore, SArrayRef, SALoad, SAStore, SAdd, SSub, SMul, SDiv, SRem, SNeg, SAnd, SOr, SXor,
  SIfCmp, SIfNe, SGoto, SInvoke, SIReturn, SReturn, SPop,
  // accumulator
  ALoadI, ALoad, AStore, AAdd, ASub, AMul, ADiv, AMod, AAnd, AOr, ACmp, ANeg, ANot, AMulI, ALea, ALoadInd,
  AStoreInd, AParam, AGetParam, ACall, ARet, AJnz, AJmp,
  // register-memory (IA-32-like); r = register, m = [ebp+disp]
  XMovRM, XMovMR, XMovMI, XMovRI, XMovRR, XAddRM, XSubRM, XAndRM, XOrRM, XImulRM, XImulRRI, XCdq, XIdivM,
  XCmpRM, XCmpRI, XSetcc, XMovzx, XNeg, XXorRI, XLea, XLoadIdx, XStoreIdx, XLeaIdx, XPushM, XPushR, XPopR,
  XCall, XRet, XAddEsp, XSubEsp, XJne, XJmp,
};

struct MachineInstr {
  Op op = Op::SConst;
  std::int32_t a = 0;  // register / slot / displacement / argument count
  std::int32_t b = 0;  // second register / slot
  std::int32_t c = 0;
  std::int64_t imm = 0;
  Cond cond = Cond::Eq;
  std::size_t target = kNoTarget;  // jump target index or callee function index
  std::size_t bytes = 1;
  std::string text;
};

enum Reg : std::int32_t { EAX = 0, ECX = 1, EDX = 2, EBX = 3, ESP = 4, EBP = 5 };
constexpr std::string_view kRegNames[] = {"eax", "ecx", "edx", "ebx", "esp", "ebp"};

// Frame layout shared by all styles: one word per scalar local / IR value, extents-product words per
// array, one pointer word per array parameter, plus two scratch words.
struct Frame {
  std::vector<std::int32_t> local_slot;
  std::vector<bool> local_is_pointer;
  std::vector<std::int32_t> value_slot;
  std::int32_t scratch0 = 0;
  std::int32_t scratch1 = 0;
  std::int32_t words = 0;
  std::vector<std::int32_t> param_slots;
};

std::int32_t array_words(const ir::Type& type) {
  std::int64_t words = 1;
  for (const auto extent : type.array_extents) {
    words *= extent;
  }
  return static_cast<std::int32_t>(words);
}

Frame build_frame(const Function& function) {
  Frame frame;
  frame.local_slot.resize(function.locals.size());
  frame.local_is_pointer.resize(function.locals.size());
  frame.value_slot.resize(function.values.size());
  std::int32_t cursor = 0;
  for (const auto& local : function.locals) {
    frame.local_slot[local.id] = cursor;
    frame.local_is_pointer[local.id] = local.is_parameter && local.type.is_array();
    cursor += frame.local_is_pointer[local.id] || !local.type.is_array() ? 1 : array_words(local.type);
  }
  for (const auto& value : function.values) {
    frame.value_slot[value.id] = cursor++;
  }
  frame.scratch0 = cursor++;
  frame.scratch1 = cursor++;
  frame.words = cursor;
  for (const auto& parameter : function.parameters) {
    frame.param_slots.push_back(frame.local_slot[parameter.local]);
  }
  return frame;
}

std::int32_t sub_array_stride(const ir::Type& type, std::size_t given_indices) {
  std::int64_t stride = 1;
  for (std::size_t dimension = given_indices; dimension < type.array_extents.size(); ++dimension) {
    stride *= type.array_extents[dimension];
  }
  return static_cast<std::int32_t>(stride);
}

std::vector<std::size_t> use_counts(const Function& function) {
  std::vector<std::size_t> counts(function.values.size(), 0);
  for (const auto& block : function.blocks) {
    for (const auto& instruction : block.instructions) {
      for (const ValueId value : ir::instruction_uses(instruction)) {
        counts[value] += 1;
      }
    }
    if (block.terminator.has_value()) {
      for (const ValueId value : ir::terminator_uses(*block.terminator)) {
        counts[value] += 1;
      }
    }
  }
  return counts;
}

// First IR value an instruction consumes (the one a stack/accumulator machine loads first).
std::optional<ValueId> first_consumed(const Instruction& instruction, IsaStyle style) {
  switch (instruction.kind) {
    case InstructionKind::StoreLocal:
    case InstructionKind::Unary:
    case InstructionKind::Binary:
      return instruction.operands.front();
    case InstructionKind::LoadElement:
    case InstructionKind::StoreElement:
      if (style == IsaStyle::Stack || instruction.operands.empty()) {
        return std::nullopt;  // the array reference is pushed first
      }
      return instruction.operands.front();
    case InstructionKind::Call:
      if (style == IsaStyle::Accumulator && !instruction.call_arguments.empty() &&
          instruction.call_arguments.front().kind == ir::CallArgumentKind::Value) {
        return instruction.call_arguments.front().value;
      }
      if (style == IsaStyle::Stack && !instruction.call_arguments.empty() &&
          instruction.call_arguments.front().kind == ir::CallArgumentKind::Value) {
        return instruction.call_arguments.front().value;
      }
      return std::nullopt;
    default:
      return std::nullopt;
  }
}

std::optional<ValueId> first_consumed(const ir::Terminator& terminator) {
  if (terminator.kind == TerminatorKind::Branch) {
    return terminator.condition;
  }
  if (terminator.kind == TerminatorKind::Return) {
    return terminator.return_value;
  }
  return std::nullopt;
}

// ---------------------------------------------------------------------------------------------
// Emitter: shared bookkeeping (labels, fixups, listing) plus one lowering routine per style.
// ---------------------------------------------------------------------------------------------
struct FunctionInfo {
  std::string name;
  std::size_t entry = 0;
  Frame frame;
  bool returns_value = false;
};

class Emitter {
 public:
  Emitter(const ir::Module& module, IsaStyle style) : module_(module), style_(style) {
    for (std::size_t index = 0; index < module.functions.size(); ++index) {
      function_index_[module.functions[index].name] = index;
      functions_.push_back(FunctionInfo{
          .name = module.functions[index].name,
          .entry = 0,
          .frame = build_frame(module.functions[index]),
          .returns_value = !module.functions[index].return_type.is_void(),
      });
    }
  }

  void emit_all() {
    for (std::size_t index = 0; index < module_.functions.size(); ++index) {
      emit_function(index);
    }
  }

  std::vector<MachineInstr> code_;
  std::vector<FunctionInfo> functions_;
  std::string listing_;

 private:
  // --- helpers ---------------------------------------------------------------------------
  void add(MachineInstr instr) {
    std::ostringstream line;
    line << "  " << instr.text;
    listing_lines_.push_back({code_.size(), line.str()});
    code_.push_back(std::move(instr));
  }

  void label(const std::string& name) { labels_.push_back({code_.size(), name}); }

  std::size_t here() const { return code_.size(); }

  void emit_function(std::size_t index) {
    const Function& function = module_.functions[index];
    current_ = &function;
    info_ = &functions_[index];
    info_->entry = here();
    counts_ = use_counts(function);
    block_start_.assign(function.blocks.size() + 1, kNoTarget);
    block_fixups_.clear();
    pending_.reset();
    label(function.name);

    prologue();
    for (const auto& block : function.blocks) {
      block_start_[block.id] = here();
      if (block.id != function.entry_block) {
        label(function.name + ".bb" + std::to_string(block.id));
      }
      for (std::size_t position = 0; position < block.instructions.size(); ++position) {
        const auto& instruction = block.instructions[position];
        std::optional<ValueId> next_first;
        if (position + 1 < block.instructions.size()) {
          next_first = first_consumed(block.instructions[position + 1], style_);
        } else if (block.terminator.has_value()) {
          next_first = first_consumed(*block.terminator);
        }
        next_first_ = next_first;
        lower(instruction);
      }
      if (block.terminator.has_value()) {
        lower(*block.terminator);
      }
      pending_.reset();
    }
    if (style_ == IsaStyle::RegisterMemory) {
      epilogue_index_ = here();
      label(function.name + ".epilogue");
      add({.op = Op::XMovRR, .a = ESP, .b = EBP, .bytes = 2, .text = "mov esp, ebp"});
      add({.op = Op::XPopR, .a = EBP, .bytes = 1, .text = "pop ebp"});
      add({.op = Op::XRet, .bytes = 1, .text = "ret"});
      for (const std::size_t fixup : epilogue_fixups_) {
        code_[fixup].target = epilogue_index_;
      }
      epilogue_fixups_.clear();
    }
    for (const auto& [instr, block] : block_fixups_) {
      code_[instr].target = block_start_[block];
    }
    flush_listing();
  }

  void flush_listing() {
    std::size_t label_cursor = 0;
    std::ostringstream out;
    for (const auto& [index, text] : listing_lines_) {
      while (label_cursor < labels_.size() && labels_[label_cursor].first <= index) {
        out << labels_[label_cursor].second << ":\n";
        ++label_cursor;
      }
      out << text;
      const auto& instr = code_[index];
      if (instr.target != kNoTarget && (instr.op == Op::SIfCmp || instr.op == Op::SIfNe || instr.op == Op::SGoto ||
                                        instr.op == Op::AJnz || instr.op == Op::AJmp || instr.op == Op::XJne ||
                                        instr.op == Op::XJmp)) {
        out << " @" << instr.target;
      }
      out << "\n";
    }
    while (label_cursor < labels_.size()) {
      out << labels_[label_cursor++].second << ":\n";
    }
    listing_ += out.str();
    listing_lines_.clear();
    labels_.clear();
  }

  void jump_to_block(MachineInstr instr, BlockId block) {
    block_fixups_.push_back({here(), block});
    add(std::move(instr));
  }

  [[nodiscard]] bool keep_result(ValueId value) const {
    return next_first_.has_value() && *next_first_ == value && counts_[value] == 1;
  }

  [[nodiscard]] std::string slot_text(std::int32_t slot) const { return "#" + std::to_string(slot); }

  // x86 displacement (in words) of a frame slot: the frame occupies [ebp - words, ebp).
  [[nodiscard]] std::int32_t x86_disp(std::int32_t slot) const { return slot - info_->frame.words; }

  [[nodiscard]] std::int32_t x86_local_disp(LocalId local) const {
    for (std::size_t index = 0; index < current_->parameters.size(); ++index) {
      if (current_->parameters[index].local == local) {
        return 2 + static_cast<std::int32_t>(index);  // [ebp+8+4i]
      }
    }
    return x86_disp(info_->frame.local_slot[local]);
  }

  static std::string x86_mem(std::int32_t disp_words) {
    const std::int32_t bytes = disp_words * 4;
    return bytes >= 0 ? "[ebp+" + std::to_string(bytes) + "]" : "[ebp-" + std::to_string(-bytes) + "]";
  }

  static std::size_t x86_disp_bytes(std::int32_t disp_words) {
    const std::int32_t bytes = disp_words * 4;
    return (bytes >= -128 && bytes <= 127) ? 1U : 4U;
  }

  // --- style dispatch ------------------------------------------------------------------------
  void prologue() {
    const Frame& frame = info_->frame;
    switch (style_) {
      case IsaStyle::Stack:
        break;  // the invoke instruction moves arguments into the parameter slots
      case IsaStyle::Accumulator:
        for (std::size_t index = 0; index < frame.param_slots.size(); ++index) {
          add({.op = Op::AGetParam, .a = static_cast<std::int32_t>(index), .bytes = 2,
               .text = "getparam " + std::to_string(index)});
          add({.op = Op::AStore, .a = frame.param_slots[index], .bytes = 3,
               .text = "store " + slot_text(frame.param_slots[index])});
        }
        break;
      case IsaStyle::RegisterMemory:
        add({.op = Op::XPushR, .a = EBP, .bytes = 1, .text = "push ebp"});
        add({.op = Op::XMovRR, .a = EBP, .b = ESP, .bytes = 2, .text = "mov ebp, esp"});
        add({.op = Op::XSubEsp, .imm = frame.words, .bytes = frame.words * 4 <= 127 ? 3U : 6U,
             .text = "sub esp, " + std::to_string(frame.words * 4)});
        break;
    }
  }

  void lower(const Instruction& instruction) {
    switch (style_) {
      case IsaStyle::Stack:
        lower_stack(instruction);
        return;
      case IsaStyle::Accumulator:
        lower_accumulator(instruction);
        return;
      case IsaStyle::RegisterMemory:
        lower_x86(instruction);
        return;
    }
  }

  void lower(const ir::Terminator& terminator) {
    next_first_.reset();
    switch (style_) {
      case IsaStyle::Stack:
        lower_stack(terminator);
        return;
      case IsaStyle::Accumulator:
        lower_accumulator(terminator);
        return;
      case IsaStyle::RegisterMemory:
        lower_x86(terminator);
        return;
    }
  }

  // --- stack machine -------------------------------------------------------------------------
  static std::size_t jvm_const_bytes(std::int64_t value) {
    if (value >= -1 && value <= 5) {
      return 1;
    }
    if (value >= -128 && value <= 127) {
      return 2;
    }
    return value >= -32768 && value <= 32767 ? 3U : 2U;  // ldc from the constant pool
  }

  static std::size_t jvm_local_bytes(std::int32_t slot) { return slot <= 3 ? 1U : (slot <= 255 ? 2U : 4U); }

  void s_const(std::int64_t value) {
    std::string text = value >= -1 && value <= 5 ? "iconst_" + std::to_string(value)
                       : value >= -128 && value <= 127 ? "bipush " + std::to_string(value)
                       : value >= -32768 && value <= 32767 ? "sipush " + std::to_string(value)
                                                           : "ldc " + std::to_string(value);
    add({.op = Op::SConst, .imm = value, .bytes = jvm_const_bytes(value), .text = text});
  }

  void s_load_slot(std::int32_t slot) {
    add({.op = Op::SLoad, .a = slot, .bytes = jvm_local_bytes(slot), .text = "iload " + std::to_string(slot)});
  }

  void s_push(ValueId value) {
    if (pending_ == value) {
      pending_.reset();
      return;
    }
    s_load_slot(info_->frame.value_slot[value]);
  }

  void s_produce(ValueId value) {
    if (keep_result(value)) {
      pending_ = value;
      return;
    }
    const std::int32_t slot = info_->frame.value_slot[value];
    add({.op = Op::SStore, .a = slot, .bytes = jvm_local_bytes(slot), .text = "istore " + std::to_string(slot)});
  }

  void s_array_ref(LocalId local) {
    const std::int32_t slot = info_->frame.local_slot[local];
    if (info_->frame.local_is_pointer[local]) {
      add({.op = Op::SLoad, .a = slot, .bytes = jvm_local_bytes(slot), .text = "aload " + std::to_string(slot)});
    } else {
      add({.op = Op::SArrayRef, .a = slot, .bytes = jvm_local_bytes(slot),
           .text = "aload " + std::to_string(slot) + "  ; array " + current_->locals[local].name});
    }
  }

  void s_linear_index(LocalId local, const std::vector<ValueId>& indices) {
    const auto& type = current_->locals[local].type;
    s_push(indices.front());
    for (std::size_t index = 1; index < indices.size(); ++index) {
      s_const(type.array_extents[index]);
      add({.op = Op::SMul, .text = "imul"});
      s_push(indices[index]);
      add({.op = Op::SAdd, .text = "iadd"});
    }
    const std::int32_t stride = sub_array_stride(type, indices.size());
    if (stride != 1) {
      s_const(stride);
      add({.op = Op::SMul, .text = "imul"});
    }
  }

  void lower_stack(const Instruction& instruction) {
    switch (instruction.kind) {
      case InstructionKind::ConstInt:
        s_const(instruction.int_immediate);
        s_produce(*instruction.result);
        return;
      case InstructionKind::ConstBool:
        s_const(instruction.bool_immediate ? 1 : 0);
        s_produce(*instruction.result);
        return;
      case InstructionKind::LoadLocal:
        s_load_slot(info_->frame.local_slot[instruction.local]);
        s_produce(*instruction.result);
        return;
      case InstructionKind::StoreLocal: {
        s_push(instruction.operands.front());
        const std::int32_t slot = info_->frame.local_slot[instruction.local];
        add({.op = Op::SStore, .a = slot, .bytes = jvm_local_bytes(slot), .text = "istore " + std::to_string(slot)});
        return;
      }
      case InstructionKind::LoadElement:
        s_array_ref(instruction.local);
        s_linear_index(instruction.local, instruction.operands);
        add({.op = Op::SALoad, .text = "iaload"});
        s_produce(*instruction.result);
        return;
      case InstructionKind::StoreElement: {
        std::vector<ValueId> indices(instruction.operands.begin(), instruction.operands.end() - 1);
        s_array_ref(instruction.local);
        s_linear_index(instruction.local, indices);
        s_push(instruction.operands.back());
        add({.op = Op::SAStore, .text = "iastore"});
        return;
      }
      case InstructionKind::Unary:
        s_push(instruction.operands.front());
        if (instruction.unary_op == ir::UnaryOp::Negate) {
          add({.op = Op::SNeg, .text = "ineg"});
        } else {
          s_const(1);
          add({.op = Op::SXor, .text = "ixor"});
        }
        s_produce(*instruction.result);
        return;
      case InstructionKind::Binary: {
        s_push(instruction.operands[0]);
        s_push(instruction.operands[1]);
        const BinaryOp op = instruction.binary_op;
        if (is_comparison(op)) {
          // javac pattern: if_icmp<not cond> Lfalse; iconst_1; goto Lend; Lfalse: iconst_0; Lend:
          const Cond inverse = negate(cond_of(op));
          const std::size_t branch = here();
          add({.op = Op::SIfCmp, .cond = inverse, .bytes = 3, .text = "if_icmp" + std::string(cond_suffix(inverse))});
          s_const(1);
          const std::size_t jump = here();
          add({.op = Op::SGoto, .bytes = 3, .text = "goto"});
          code_[branch].target = here();
          s_const(0);
          code_[jump].target = here();
        } else {
          static const std::unordered_map<int, std::pair<Op, const char*>> kOps = {
              {static_cast<int>(BinaryOp::Add), {Op::SAdd, "iadd"}},
              {static_cast<int>(BinaryOp::Sub), {Op::SSub, "isub"}},
              {static_cast<int>(BinaryOp::Mul), {Op::SMul, "imul"}},
              {static_cast<int>(BinaryOp::Div), {Op::SDiv, "idiv"}},
              {static_cast<int>(BinaryOp::Mod), {Op::SRem, "irem"}},
              {static_cast<int>(BinaryOp::LogicalAnd), {Op::SAnd, "iand"}},
              {static_cast<int>(BinaryOp::LogicalOr), {Op::SOr, "ior"}},
          };
          const auto& [machine_op, name] = kOps.at(static_cast<int>(op));
          add({.op = machine_op, .text = name});
        }
        s_produce(*instruction.result);
        return;
      }
      case InstructionKind::Call: {
        for (const auto& argument : instruction.call_arguments) {
          if (argument.kind == ir::CallArgumentKind::Value) {
            s_push(argument.value);
          } else {
            s_array_ref(argument.local);
            if (!argument.indices.empty()) {
              s_linear_index(argument.local, argument.indices);
              add({.op = Op::SAdd, .text = "iadd  ; sub-array reference"});
            }
          }
        }
        const std::size_t callee = function_index_.at(instruction.callee);
        add({.op = Op::SInvoke, .a = static_cast<std::int32_t>(instruction.call_arguments.size()), .target = callee,
             .bytes = 3, .text = "invokestatic " + instruction.callee});
        if (instruction.result.has_value()) {
          s_produce(*instruction.result);
        } else if (functions_[callee].returns_value) {
          add({.op = Op::SPop, .text = "pop"});
        }
        return;
      }
    }
  }

  void lower_stack(const ir::Terminator& terminator) {
    switch (terminator.kind) {
      case TerminatorKind::Jump:
        jump_to_block({.op = Op::SGoto, .bytes = 3, .text = "goto"}, terminator.true_target);
        return;
      case TerminatorKind::Branch:
        s_push(*terminator.condition);
        jump_to_block({.op = Op::SIfNe, .bytes = 3, .text = "ifne"}, terminator.true_target);
        jump_to_block({.op = Op::SGoto, .bytes = 3, .text = "goto"}, terminator.false_target);
        return;
      case TerminatorKind::Return:
        if (terminator.return_value.has_value()) {
          s_push(*terminator.return_value);
          add({.op = Op::SIReturn, .text = "ireturn"});
        } else {
          add({.op = Op::SReturn, .text = "return"});
        }
        return;
    }
  }

  // --- accumulator machine ------------------------------------------------------------------
  void a_mem(Op op, const std::string& name, std::int32_t slot) {
    add({.op = op, .a = slot, .bytes = 3, .text = name + " " + slot_text(slot)});
  }

  void a_load(ValueId value) {
    if (pending_ == value) {
      pending_.reset();
      return;
    }
    a_mem(Op::ALoad, "load", info_->frame.value_slot[value]);
  }

  void a_produce(ValueId value) {
    if (keep_result(value)) {
      pending_ = value;
      return;
    }
    a_mem(Op::AStore, "store", info_->frame.value_slot[value]);
  }

  // Leaves the address of local[indices...] in the accumulator.
  void a_address(LocalId local, const std::vector<ValueId>& indices) {
    const Frame& frame = info_->frame;
    const auto& type = current_->locals[local].type;
    if (!indices.empty()) {
      a_load(indices.front());
      for (std::size_t index = 1; index < indices.size(); ++index) {
        add({.op = Op::AMulI, .imm = type.array_extents[index], .bytes = 3,
             .text = "muli " + std::to_string(type.array_extents[index])});
        a_mem(Op::AAdd, "add", frame.value_slot[indices[index]]);
      }
      const std::int32_t stride = sub_array_stride(type, indices.size());
      if (stride != 1) {
        add({.op = Op::AMulI, .imm = stride, .bytes = 3, .text = "muli " + std::to_string(stride)});
      }
      a_mem(Op::AStore, "store", frame.scratch0);
    }
    if (frame.local_is_pointer[local]) {
      a_mem(Op::ALoad, "load", frame.local_slot[local]);
    } else {
      a_mem(Op::ALea, "lea", frame.local_slot[local]);
    }
    if (!indices.empty()) {
      a_mem(Op::AAdd, "add", frame.scratch0);
    }
  }

  void lower_accumulator(const Instruction& instruction) {
    const Frame& frame = info_->frame;
    switch (instruction.kind) {
      case InstructionKind::ConstInt:
      case InstructionKind::ConstBool: {
        const std::int64_t value =
            instruction.kind == InstructionKind::ConstInt ? instruction.int_immediate : (instruction.bool_immediate ? 1 : 0);
        add({.op = Op::ALoadI, .imm = value, .bytes = 3, .text = "loadi " + std::to_string(value)});
        a_produce(*instruction.result);
        return;
      }
      case InstructionKind::LoadLocal:
        a_mem(Op::ALoad, "load", frame.local_slot[instruction.local]);
        a_produce(*instruction.result);
        return;
      case InstructionKind::StoreLocal:
        a_load(instruction.operands.front());
        a_mem(Op::AStore, "store", frame.local_slot[instruction.local]);
        return;
      case InstructionKind::LoadElement:
        a_address(instruction.local, instruction.operands);
        add({.op = Op::ALoadInd, .bytes = 1, .text = "loadind"});
        a_produce(*instruction.result);
        return;
      case InstructionKind::StoreElement: {
        std::vector<ValueId> indices(instruction.operands.begin(), instruction.operands.end() - 1);
        a_address(instruction.local, indices);
        a_mem(Op::AStore, "store", frame.scratch1);
        a_load(instruction.operands.back());
        a_mem(Op::AStoreInd, "storeind", frame.scratch1);
        return;
      }
      case InstructionKind::Unary:
        a_load(instruction.operands.front());
        if (instruction.unary_op == ir::UnaryOp::Negate) {
          add({.op = Op::ANeg, .bytes = 1, .text = "neg"});
        } else {
          add({.op = Op::ANot, .bytes = 1, .text = "not"});
        }
        a_produce(*instruction.result);
        return;
      case InstructionKind::Binary: {
        a_load(instruction.operands[0]);
        const std::int32_t rhs = frame.value_slot[instruction.operands[1]];
        const BinaryOp op = instruction.binary_op;
        if (is_comparison(op)) {
          const Cond cond = cond_of(op);
          add({.op = Op::ACmp, .a = rhs, .cond = cond, .bytes = 3,
               .text = "c" + std::string(cond_suffix(cond)) + " " + slot_text(rhs)});
        } else {
          static const std::unordered_map<int, std::pair<Op, const char*>> kOps = {
              {static_cast<int>(BinaryOp::Add), {Op::AAdd, "add"}},
              {static_cast<int>(BinaryOp::Sub), {Op::ASub, "sub"}},
              {static_cast<int>(BinaryOp::Mul), {Op::AMul, "mul"}},
              {static_cast<int>(BinaryOp::Div), {Op::ADiv, "div"}},
              {static_cast<int>(BinaryOp::Mod), {Op::AMod, "mod"}},
              {static_cast<int>(BinaryOp::LogicalAnd), {Op::AAnd, "and"}},
              {static_cast<int>(BinaryOp::LogicalOr), {Op::AOr, "or"}},
          };
          const auto& [machine_op, name] = kOps.at(static_cast<int>(op));
          a_mem(machine_op, name, rhs);
        }
        a_produce(*instruction.result);
        return;
      }
      case InstructionKind::Call: {
        for (std::size_t index = 0; index < instruction.call_arguments.size(); ++index) {
          const auto& argument = instruction.call_arguments[index];
          if (argument.kind == ir::CallArgumentKind::Value) {
            a_load(argument.value);
          } else {
            a_address(argument.local, argument.indices);
          }
          add({.op = Op::AParam, .a = static_cast<std::int32_t>(index), .bytes = 2,
               .text = "param " + std::to_string(index)});
        }
        add({.op = Op::ACall, .target = function_index_.at(instruction.callee), .bytes = 3,
             .text = "call " + instruction.callee});
        if (instruction.result.has_value()) {
          a_produce(*instruction.result);
        }
        return;
      }
    }
  }

  void lower_accumulator(const ir::Terminator& terminator) {
    switch (terminator.kind) {
      case TerminatorKind::Jump:
        jump_to_block({.op = Op::AJmp, .bytes = 3, .text = "jmp"}, terminator.true_target);
        return;
      case TerminatorKind::Branch:
        a_load(*terminator.condition);
        jump_to_block({.op = Op::AJnz, .bytes = 3, .text = "jnz"}, terminator.true_target);
        jump_to_block({.op = Op::AJmp, .bytes = 3, .text = "jmp"}, terminator.false_target);
        return;
      case TerminatorKind::Return:
        if (terminator.return_value.has_value()) {
          a_load(*terminator.return_value);
        } else {
          add({.op = Op::ALoadI, .imm = 0, .bytes = 3, .text = "loadi 0"});
        }
        add({.op = Op::ARet, .bytes = 1, .text = "ret"});
        return;
    }
  }

  // --- register-memory (IA-32-like) ---------------------------------------------------------
  void x_load(Reg reg, ValueId value) {
    if (reg == EAX && pending_ == value) {
      pending_.reset();
      return;
    }
    const std::int32_t disp = x86_disp(info_->frame.value_slot[value]);
    add({.op = Op::XMovRM, .a = reg, .b = disp, .bytes = 2 + x86_disp_bytes(disp),
         .text = "mov " + std::string(kRegNames[reg]) + ", " + x86_mem(disp)});
  }

  void x_mem_op(Op op, const std::string& name, Reg reg, std::int32_t disp, std::size_t base_bytes = 2) {
    add({.op = op, .a = reg, .b = disp, .bytes = base_bytes + x86_disp_bytes(disp),
         .text = name + " " + std::string(kRegNames[reg]) + ", " + x86_mem(disp)});
  }

  void x_produce(ValueId value) {
    if (keep_result(value)) {
      pending_ = value;
      return;
    }
    const std::int32_t disp = x86_disp(info_->frame.value_slot[value]);
    add({.op = Op::XMovMR, .a = EAX, .b = disp, .bytes = 2 + x86_disp_bytes(disp),
         .text = "mov " + x86_mem(disp) + ", eax"});
  }

  // eax <- linear element index, ecx <- array base address
  void x_index_and_base(LocalId local, const std::vector<ValueId>& indices) {
    const Frame& frame = info_->frame;
    const auto& type = current_->locals[local].type;
    if (!indices.empty()) {
      x_load(EAX, indices.front());
      for (std::size_t index = 1; index < indices.size(); ++index) {
        const auto extent = type.array_extents[index];
        add({.op = Op::XImulRRI, .a = EAX, .b = EAX, .imm = extent, .bytes = extent <= 127 ? 3U : 6U,
             .text = "imul eax, eax, " + std::to_string(extent)});
        x_mem_op(Op::XAddRM, "add", EAX, x86_disp(frame.value_slot[indices[index]]));
      }
      const std::int32_t stride = sub_array_stride(type, indices.size());
      if (stride != 1) {
        add({.op = Op::XImulRRI, .a = EAX, .b = EAX, .imm = stride, .bytes = stride <= 127 ? 3U : 6U,
             .text = "imul eax, eax, " + std::to_string(stride)});
      }
    }
    const std::int32_t disp = x86_local_disp(local);
    if (frame.local_is_pointer[local]) {
      x_mem_op(Op::XMovRM, "mov", ECX, disp);
    } else {
      x_mem_op(Op::XLea, "lea", ECX, disp);
    }
  }

  void lower_x86(const Instruction& instruction) {
    const Frame& frame = info_->frame;
    switch (instruction.kind) {
      case InstructionKind::ConstInt:
      case InstructionKind::ConstBool: {
        const std::int64_t value =
            instruction.kind == InstructionKind::ConstInt ? instruction.int_immediate : (instruction.bool_immediate ? 1 : 0);
        if (keep_result(*instruction.result)) {
          add({.op = Op::XMovRI, .a = EAX, .imm = value, .bytes = 5, .text = "mov eax, " + std::to_string(value)});
          pending_ = *instruction.result;
        } else {
          const std::int32_t disp = x86_disp(frame.value_slot[*instruction.result]);
          add({.op = Op::XMovMI, .b = disp, .imm = value, .bytes = 6 + x86_disp_bytes(disp),
               .text = "mov dword " + x86_mem(disp) + ", " + std::to_string(value)});
        }
        return;
      }
      case InstructionKind::LoadLocal:
        x_mem_op(Op::XMovRM, "mov", EAX, x86_local_disp(instruction.local));
        x_produce(*instruction.result);
        return;
      case InstructionKind::StoreLocal: {
        x_load(EAX, instruction.operands.front());
        const std::int32_t disp = x86_local_disp(instruction.local);
        add({.op = Op::XMovMR, .a = EAX, .b = disp, .bytes = 2 + x86_disp_bytes(disp),
             .text = "mov " + x86_mem(disp) + ", eax"});
        return;
      }
      case InstructionKind::LoadElement:
        x_index_and_base(instruction.local, instruction.operands);
        add({.op = Op::XLoadIdx, .a = EAX, .b = ECX, .c = EAX, .bytes = 3, .text = "mov eax, [ecx+eax*4]"});
        x_produce(*instruction.result);
        return;
      case InstructionKind::StoreElement: {
        std::vector<ValueId> indices(instruction.operands.begin(), instruction.operands.end() - 1);
        x_index_and_base(instruction.local, indices);
        x_load(EDX, instruction.operands.back());
        add({.op = Op::XStoreIdx, .a = EDX, .b = ECX, .c = EAX, .bytes = 3, .text = "mov [ecx+eax*4], edx"});
        return;
      }
      case InstructionKind::Unary:
        x_load(EAX, instruction.operands.front());
        if (instruction.unary_op == ir::UnaryOp::Negate) {
          add({.op = Op::XNeg, .a = EAX, .bytes = 2, .text = "neg eax"});
        } else {
          add({.op = Op::XXorRI, .a = EAX, .imm = 1, .bytes = 3, .text = "xor eax, 1"});
        }
        x_produce(*instruction.result);
        return;
      case InstructionKind::Binary: {
        x_load(EAX, instruction.operands[0]);
        const std::int32_t rhs = x86_disp(frame.value_slot[instruction.operands[1]]);
        const BinaryOp op = instruction.binary_op;
        if (is_comparison(op)) {
          const Cond cond = cond_of(op);
          x_mem_op(Op::XCmpRM, "cmp", EAX, rhs);
          add({.op = Op::XSetcc, .a = EAX, .cond = cond, .bytes = 3, .text = "set" + std::string(x86_cond_suffix(cond)) + " al"});
          add({.op = Op::XMovzx, .a = EAX, .bytes = 3, .text = "movzx eax, al"});
        } else if (op == BinaryOp::Div || op == BinaryOp::Mod) {
          add({.op = Op::XCdq, .bytes = 1, .text = "cdq"});
          add({.op = Op::XIdivM, .b = rhs, .bytes = 2 + x86_disp_bytes(rhs), .text = "idiv dword " + x86_mem(rhs)});
          if (op == BinaryOp::Mod) {
            add({.op = Op::XMovRR, .a = EAX, .b = EDX, .bytes = 2, .text = "mov eax, edx"});
          }
        } else if (op == BinaryOp::Mul) {
          x_mem_op(Op::XImulRM, "imul", EAX, rhs, 3);
        } else {
          static const std::unordered_map<int, std::pair<Op, const char*>> kOps = {
              {static_cast<int>(BinaryOp::Add), {Op::XAddRM, "add"}},
              {static_cast<int>(BinaryOp::Sub), {Op::XSubRM, "sub"}},
              {static_cast<int>(BinaryOp::LogicalAnd), {Op::XAndRM, "and"}},
              {static_cast<int>(BinaryOp::LogicalOr), {Op::XOrRM, "or"}},
          };
          const auto& [machine_op, name] = kOps.at(static_cast<int>(op));
          x_mem_op(machine_op, name, EAX, rhs);
        }
        x_produce(*instruction.result);
        return;
      }
      case InstructionKind::Call: {
        const auto& arguments = instruction.call_arguments;
        for (std::size_t reverse = arguments.size(); reverse-- > 0;) {
          const auto& argument = arguments[reverse];
          if (argument.kind == ir::CallArgumentKind::Value) {
            const std::int32_t disp = x86_disp(frame.value_slot[argument.value]);
            add({.op = Op::XPushM, .b = disp, .bytes = 2 + x86_disp_bytes(disp), .text = "push dword " + x86_mem(disp)});
          } else {
            x_index_and_base(argument.local, argument.indices);
            if (argument.indices.empty()) {
              add({.op = Op::XPushR, .a = ECX, .bytes = 1, .text = "push ecx"});
            } else {
              add({.op = Op::XLeaIdx, .a = EAX, .b = ECX, .c = EAX, .bytes = 3, .text = "lea eax, [ecx+eax*4]"});
              add({.op = Op::XPushR, .a = EAX, .bytes = 1, .text = "push eax"});
            }
          }
        }
        add({.op = Op::XCall, .target = function_index_.at(instruction.callee), .bytes = 5,
             .text = "call " + instruction.callee});
        if (!arguments.empty()) {
          const auto words = static_cast<std::int64_t>(arguments.size());
          add({.op = Op::XAddEsp, .imm = words, .bytes = 3, .text = "add esp, " + std::to_string(words * 4)});
        }
        if (instruction.result.has_value()) {
          x_produce(*instruction.result);
        }
        return;
      }
    }
  }

  void lower_x86(const ir::Terminator& terminator) {
    switch (terminator.kind) {
      case TerminatorKind::Jump:
        jump_to_block({.op = Op::XJmp, .bytes = 5, .text = "jmp"}, terminator.true_target);
        return;
      case TerminatorKind::Branch: {
        if (pending_ == *terminator.condition) {
          pending_.reset();
          add({.op = Op::XCmpRI, .a = EAX, .imm = 0, .bytes = 3, .text = "cmp eax, 0"});
        } else {
          const std::int32_t disp = x86_disp(info_->frame.value_slot[*terminator.condition]);
          add({.op = Op::XCmpRI, .a = -1, .b = disp, .imm = 0, .bytes = 3 + x86_disp_bytes(disp),
               .text = "cmp dword " + x86_mem(disp) + ", 0"});
        }
        jump_to_block({.op = Op::XJne, .bytes = 6, .text = "jne"}, terminator.true_target);
        jump_to_block({.op = Op::XJmp, .bytes = 5, .text = "jmp"}, terminator.false_target);
        return;
      }
      case TerminatorKind::Return:
        if (terminator.return_value.has_value()) {
          x_load(EAX, *terminator.return_value);
        } else {
          add({.op = Op::XMovRI, .a = EAX, .imm = 0, .bytes = 5, .text = "mov eax, 0"});
        }
        epilogue_fixups_.push_back(here());
        add({.op = Op::XJmp, .bytes = 5, .text = "jmp .epilogue"});
        return;
    }
  }

  const ir::Module& module_;
  IsaStyle style_;
  std::unordered_map<std::string, std::size_t> function_index_;
  const Function* current_ = nullptr;
  FunctionInfo* info_ = nullptr;
  std::vector<std::size_t> counts_;
  std::vector<std::size_t> block_start_;
  std::vector<std::pair<std::size_t, BlockId>> block_fixups_;
  std::vector<std::size_t> epilogue_fixups_;
  std::size_t epilogue_index_ = 0;
  std::optional<ValueId> pending_;
  std::optional<ValueId> next_first_;
  std::vector<std::pair<std::size_t, std::string>> listing_lines_;
  std::vector<std::pair<std::size_t, std::string>> labels_;
};

// ---------------------------------------------------------------------------------------------
// Interpreter for all three styles.
// ---------------------------------------------------------------------------------------------
class Machine {
 public:
  Machine(const std::vector<MachineInstr>& code, const std::vector<FunctionInfo>& functions, IsaStyle style)
      : code_(code), functions_(functions), style_(style), memory_(kMemoryWords, 0) {}

  StyleRun run(std::size_t main_index) {
    StyleRun run;
    try {
      execute(main_index, run);
    } catch (const std::string& error) {
      run.success = false;
      run.error = error;
    }
    return run;
  }

 private:
  struct CallFrame {
    std::size_t return_pc = 0;
    std::int32_t fp = 0;
    std::size_t stack_base = 0;
  };

  std::int32_t& word(std::int64_t address) {
    if (address < 0 || static_cast<std::size_t>(address) >= memory_.size()) {
      throw std::string("memory access out of bounds at word " + std::to_string(address));
    }
    return memory_[static_cast<std::size_t>(address)];
  }

  std::int32_t read(std::int64_t address) {
    ++run_->memory_reads;
    return word(address);
  }

  void write(std::int64_t address, std::int32_t value) {
    ++run_->memory_writes;
    word(address) = value;
  }

  std::int32_t pop() {
    if (stack_.size() <= frames_.back().stack_base) {
      throw std::string("operand stack underflow");
    }
    const std::int32_t value = stack_.back();
    stack_.pop_back();
    return value;
  }

  void push(std::int32_t value) {
    stack_.push_back(value);
    run_->max_operand_stack = std::max(run_->max_operand_stack, stack_.size() - frames_.back().stack_base);
  }

  static std::int32_t divide(std::int32_t lhs, std::int32_t rhs, bool remainder) {
    if (rhs == 0) {
      throw std::string("division by zero");
    }
    return remainder ? lhs % rhs : lhs / rhs;
  }

  void enter(std::size_t function, std::size_t return_pc) {
    const std::int32_t words = functions_[function].frame.words;
    if (sp_ - words < 1024) {
      throw std::string("stack overflow");
    }
    sp_ -= words;
    frames_.push_back({return_pc, sp_, stack_.size()});
    pc_ = functions_[function].entry;
  }

  void execute(std::size_t main_index, StyleRun& run) {
    run_ = &run;
    sp_ = static_cast<std::int32_t>(memory_.size());
    constexpr std::size_t kHalt = std::numeric_limits<std::size_t>::max();
    std::array<std::int32_t, 6> regs{};
    std::int32_t acc = 0;
    std::array<std::int32_t, 8> params{};
    std::int32_t cmp_lhs = 0;
    std::int32_t cmp_rhs = 0;

    if (style_ == IsaStyle::RegisterMemory) {
      regs[ESP] = static_cast<std::int32_t>(memory_.size());
      regs[ESP] -= 1;
      word(regs[ESP]) = -1;  // sentinel return address
      pc_ = functions_[main_index].entry;
      frames_.push_back({kHalt, 0, 0});
    } else {
      frames_.push_back({kHalt, 0, 0});
      enter(main_index, kHalt);
      frames_.erase(frames_.begin());
    }

    while (true) {
      if (run.dynamic_instructions++ > kStepLimit) {
        throw std::string("step limit exceeded");
      }
      if (pc_ >= code_.size()) {
        throw std::string("pc out of range");
      }
      const MachineInstr& in = code_[pc_];
      std::size_t next = pc_ + 1;
      const std::int32_t fp = frames_.back().fp;
      switch (in.op) {
        // ---- stack ----
        case Op::SConst:
          push(static_cast<std::int32_t>(in.imm));
          break;
        case Op::SLoad:
          push(read(fp + in.a));
          break;
        case Op::SStore:
          write(fp + in.a, pop());
          break;
        case Op::SArrayRef:
          push(fp + in.a);
          break;
        case Op::SALoad: {
          const std::int32_t index = pop();
          const std::int32_t base = pop();
          push(read(static_cast<std::int64_t>(base) + index));
          break;
        }
        case Op::SAStore: {
          const std::int32_t value = pop();
          const std::int32_t index = pop();
          const std::int32_t base = pop();
          write(static_cast<std::int64_t>(base) + index, value);
          break;
        }
        case Op::SAdd:
        case Op::SSub:
        case Op::SMul:
        case Op::SDiv:
        case Op::SRem:
        case Op::SAnd:
        case Op::SOr:
        case Op::SXor: {
          const std::int32_t rhs = pop();
          const std::int32_t lhs = pop();
          std::int32_t value = 0;
          switch (in.op) {
            case Op::SAdd: value = static_cast<std::int32_t>(static_cast<std::uint32_t>(lhs) + static_cast<std::uint32_t>(rhs)); break;
            case Op::SSub: value = static_cast<std::int32_t>(static_cast<std::uint32_t>(lhs) - static_cast<std::uint32_t>(rhs)); break;
            case Op::SMul: value = static_cast<std::int32_t>(static_cast<std::int64_t>(lhs) * rhs); break;
            case Op::SDiv: value = divide(lhs, rhs, false); break;
            case Op::SRem: value = divide(lhs, rhs, true); break;
            case Op::SAnd: value = lhs & rhs; break;
            case Op::SOr: value = lhs | rhs; break;
            default: value = lhs ^ rhs; break;
          }
          push(value);
          break;
        }
        case Op::SNeg:
          push(-pop());
          break;
        case Op::SIfCmp: {
          const std::int32_t rhs = pop();
          const std::int32_t lhs = pop();
          if (holds(in.cond, lhs, rhs)) {
            next = in.target;
          }
          break;
        }
        case Op::SIfNe:
          if (pop() != 0) {
            next = in.target;
          }
          break;
        case Op::SGoto:
          next = in.target;
          break;
        case Op::SPop:
          pop();
          break;
        case Op::SInvoke: {
          std::vector<std::int32_t> arguments(static_cast<std::size_t>(in.a));
          for (std::size_t index = arguments.size(); index-- > 0;) {
            arguments[index] = pop();
          }
          enter(in.target, next);
          const auto& slots = functions_[in.target].frame.param_slots;
          for (std::size_t index = 0; index < arguments.size(); ++index) {
            write(frames_.back().fp + slots[index], arguments[index]);
          }
          continue;
        }
        case Op::SIReturn:
        case Op::SReturn: {
          const std::int32_t value = in.op == Op::SIReturn ? pop() : 0;
          const CallFrame frame = frames_.back();
          frames_.pop_back();
          stack_.resize(frame.stack_base);
          sp_ = frame.fp + functions_[function_of(pc_)].frame.words;
          if (frame.return_pc == kHalt) {
            run.exit_code = value;
            run.success = true;
            return;
          }
          if (in.op == Op::SIReturn) {
            push(value);
          }
          next = frame.return_pc;
          break;
        }
        // ---- accumulator ----
        case Op::ALoadI:
          acc = static_cast<std::int32_t>(in.imm);
          break;
        case Op::ALoad:
          acc = read(fp + in.a);
          break;
        case Op::AStore:
          write(fp + in.a, acc);
          break;
        case Op::AAdd: acc = static_cast<std::int32_t>(static_cast<std::uint32_t>(acc) + static_cast<std::uint32_t>(read(fp + in.a))); break;
        case Op::ASub: acc = static_cast<std::int32_t>(static_cast<std::uint32_t>(acc) - static_cast<std::uint32_t>(read(fp + in.a))); break;
        case Op::AMul: acc = static_cast<std::int32_t>(static_cast<std::int64_t>(acc) * read(fp + in.a)); break;
        case Op::ADiv: acc = divide(acc, read(fp + in.a), false); break;
        case Op::AMod: acc = divide(acc, read(fp + in.a), true); break;
        case Op::AAnd: acc &= read(fp + in.a); break;
        case Op::AOr: acc |= read(fp + in.a); break;
        case Op::ACmp: acc = holds(in.cond, acc, read(fp + in.a)) ? 1 : 0; break;
        case Op::ANeg: acc = -acc; break;
        case Op::ANot: acc ^= 1; break;
        case Op::AMulI: acc = static_cast<std::int32_t>(static_cast<std::int64_t>(acc) * in.imm); break;
        case Op::ALea: acc = fp + in.a; break;
        case Op::ALoadInd: acc = read(acc); break;
        case Op::AStoreInd: write(read(fp + in.a), acc); break;
        case Op::AParam:
          ++run.memory_writes;
          params.at(static_cast<std::size_t>(in.a)) = acc;
          break;
        case Op::AGetParam:
          ++run.memory_reads;
          acc = params.at(static_cast<std::size_t>(in.a));
          break;
        case Op::ACall:
          enter(in.target, next);
          continue;
        case Op::ARet: {
          const CallFrame frame = frames_.back();
          frames_.pop_back();
          sp_ = frame.fp + functions_[function_of(pc_)].frame.words;
          if (frame.return_pc == kHalt) {
            run.exit_code = acc;
            run.success = true;
            return;
          }
          next = frame.return_pc;
          break;
        }
        case Op::AJnz:
          if (acc != 0) {
            next = in.target;
          }
          break;
        case Op::AJmp:
          next = in.target;
          break;
        // ---- register-memory ----
        case Op::XMovRM: regs[static_cast<std::size_t>(in.a)] = read(regs[EBP] + in.b); break;
        case Op::XMovMR: write(regs[EBP] + in.b, regs[static_cast<std::size_t>(in.a)]); break;
        case Op::XMovMI: write(regs[EBP] + in.b, static_cast<std::int32_t>(in.imm)); break;
        case Op::XMovRI: regs[static_cast<std::size_t>(in.a)] = static_cast<std::int32_t>(in.imm); break;
        case Op::XMovRR: regs[static_cast<std::size_t>(in.a)] = regs[static_cast<std::size_t>(in.b)]; break;
        case Op::XAddRM: regs[static_cast<std::size_t>(in.a)] = static_cast<std::int32_t>(static_cast<std::uint32_t>(regs[static_cast<std::size_t>(in.a)]) + static_cast<std::uint32_t>(read(regs[EBP] + in.b))); break;
        case Op::XSubRM: regs[static_cast<std::size_t>(in.a)] = static_cast<std::int32_t>(static_cast<std::uint32_t>(regs[static_cast<std::size_t>(in.a)]) - static_cast<std::uint32_t>(read(regs[EBP] + in.b))); break;
        case Op::XAndRM: regs[static_cast<std::size_t>(in.a)] &= read(regs[EBP] + in.b); break;
        case Op::XOrRM: regs[static_cast<std::size_t>(in.a)] |= read(regs[EBP] + in.b); break;
        case Op::XImulRM: regs[static_cast<std::size_t>(in.a)] = static_cast<std::int32_t>(static_cast<std::int64_t>(regs[static_cast<std::size_t>(in.a)]) * read(regs[EBP] + in.b)); break;
        case Op::XImulRRI: regs[static_cast<std::size_t>(in.a)] = static_cast<std::int32_t>(static_cast<std::int64_t>(regs[static_cast<std::size_t>(in.b)]) * in.imm); break;
        case Op::XCdq: regs[EDX] = regs[EAX] < 0 ? -1 : 0; break;
        case Op::XIdivM: {
          const std::int32_t divisor = read(regs[EBP] + in.b);
          const std::int32_t dividend = regs[EAX];
          regs[EAX] = divide(dividend, divisor, false);
          regs[EDX] = divide(dividend, divisor, true);
          break;
        }
        case Op::XCmpRM: cmp_lhs = regs[static_cast<std::size_t>(in.a)]; cmp_rhs = read(regs[EBP] + in.b); break;
        case Op::XCmpRI:
          cmp_lhs = in.a >= 0 ? regs[static_cast<std::size_t>(in.a)] : read(regs[EBP] + in.b);
          cmp_rhs = static_cast<std::int32_t>(in.imm);
          break;
        case Op::XSetcc: {
          auto& reg_value = regs[static_cast<std::size_t>(in.a)];
          reg_value = (reg_value & ~0xff) | (holds(in.cond, cmp_lhs, cmp_rhs) ? 1 : 0);
          break;
        }
        case Op::XMovzx: regs[static_cast<std::size_t>(in.a)] &= 0xff; break;
        case Op::XNeg: regs[static_cast<std::size_t>(in.a)] = -regs[static_cast<std::size_t>(in.a)]; break;
        case Op::XXorRI: regs[static_cast<std::size_t>(in.a)] ^= static_cast<std::int32_t>(in.imm); break;
        case Op::XLea: regs[static_cast<std::size_t>(in.a)] = regs[EBP] + in.b; break;
        case Op::XLoadIdx: regs[static_cast<std::size_t>(in.a)] = read(static_cast<std::int64_t>(regs[static_cast<std::size_t>(in.b)]) + regs[static_cast<std::size_t>(in.c)]); break;
        case Op::XStoreIdx: write(static_cast<std::int64_t>(regs[static_cast<std::size_t>(in.b)]) + regs[static_cast<std::size_t>(in.c)], regs[static_cast<std::size_t>(in.a)]); break;
        case Op::XLeaIdx: regs[static_cast<std::size_t>(in.a)] = regs[static_cast<std::size_t>(in.b)] + regs[static_cast<std::size_t>(in.c)]; break;
        case Op::XPushM: {
          const std::int32_t value = read(regs[EBP] + in.b);
          regs[ESP] -= 1;
          write(regs[ESP], value);
          break;
        }
        case Op::XPushR:
          regs[ESP] -= 1;
          write(regs[ESP], regs[static_cast<std::size_t>(in.a)]);
          break;
        case Op::XPopR:
          regs[static_cast<std::size_t>(in.a)] = read(regs[ESP]);
          regs[ESP] += 1;
          break;
        case Op::XCall:
          regs[ESP] -= 1;
          write(regs[ESP], static_cast<std::int32_t>(next));
          next = functions_[in.target].entry;
          break;
        case Op::XRet: {
          const std::int32_t target = read(regs[ESP]);
          regs[ESP] += 1;
          if (target == -1) {
            run.exit_code = regs[EAX];
            run.success = true;
            return;
          }
          next = static_cast<std::size_t>(target);
          break;
        }
        case Op::XAddEsp: regs[ESP] += static_cast<std::int32_t>(in.imm); break;
        case Op::XSubEsp:
          regs[ESP] -= static_cast<std::int32_t>(in.imm);
          if (regs[ESP] < 1024) {
            throw std::string("stack overflow");
          }
          break;
        case Op::XJne:
          if (cmp_lhs != cmp_rhs) {
            next = in.target;
          }
          break;
        case Op::XJmp:
          next = in.target;
          break;
      }
      pc_ = next;
    }
  }

  std::size_t function_of(std::size_t pc) const {
    std::size_t best = 0;
    for (std::size_t index = 0; index < functions_.size(); ++index) {
      if (functions_[index].entry <= pc && functions_[index].entry >= functions_[best].entry) {
        best = index;
      }
    }
    return best;
  }

  const std::vector<MachineInstr>& code_;
  const std::vector<FunctionInfo>& functions_;
  IsaStyle style_;
  std::vector<std::int32_t> memory_;
  std::vector<std::int32_t> stack_;
  std::vector<CallFrame> frames_;
  std::int32_t sp_ = 0;
  std::size_t pc_ = 0;
  StyleRun* run_ = nullptr;
};

}  // namespace

std::string_view isa_style_name(IsaStyle style) {
  switch (style) {
    case IsaStyle::Stack:
      return "stack";
    case IsaStyle::Accumulator:
      return "accumulator";
    case IsaStyle::RegisterMemory:
      return "register-memory";
  }
  return "unknown";
}

StyleResult compile_and_run(const ir::Module& module, IsaStyle style) {
  StyleResult result;
  result.program.style = style;
  Emitter emitter(module, style);
  emitter.emit_all();
  result.program.listing = emitter.listing_;
  result.program.static_instructions = emitter.code_.size();
  for (const auto& instr : emitter.code_) {
    result.program.code_bytes += instr.bytes;
  }

  std::optional<std::size_t> main_index;
  for (std::size_t index = 0; index < module.functions.size(); ++index) {
    if (module.functions[index].name == "main") {
      main_index = index;
    }
  }
  if (!main_index.has_value()) {
    result.run.error = "program has no main function";
    return result;
  }
  Machine machine(emitter.code_, emitter.functions_, style);
  result.run = machine.run(*main_index);
  return result;
}

}  // namespace nexus::compiler::isa_styles
