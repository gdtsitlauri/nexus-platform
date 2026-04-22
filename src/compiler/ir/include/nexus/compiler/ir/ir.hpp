#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nexus::compiler::ir {

using ValueId = std::size_t;
using LocalId = std::size_t;
using BlockId = std::size_t;

inline constexpr std::size_t kInvalidId = std::numeric_limits<std::size_t>::max();

enum class BaseTypeKind {
  Int,
  Bool,
  Void,
};

struct Type {
  BaseTypeKind base = BaseTypeKind::Int;
  std::vector<std::int64_t> array_extents;

  [[nodiscard]] bool is_array() const { return !array_extents.empty(); }
  [[nodiscard]] bool is_scalar() const { return array_extents.empty() && base != BaseTypeKind::Void; }
  [[nodiscard]] bool is_void() const { return array_extents.empty() && base == BaseTypeKind::Void; }
  [[nodiscard]] Type element_type() const;
};

bool operator==(const Type& lhs, const Type& rhs);

std::string_view base_type_name(BaseTypeKind kind);
std::string type_to_string(const Type& type);

struct ValueInfo {
  ValueId id = 0;
  Type type{};
};

struct LocalInfo {
  LocalId id = 0;
  std::string name;
  Type type{};
  bool is_parameter = false;
};

struct ParameterInfo {
  std::string name;
  Type type{};
  LocalId local = kInvalidId;
};

enum class UnaryOp {
  Negate,
  LogicalNot,
};

enum class BinaryOp {
  Add,
  Sub,
  Mul,
  Div,
  Mod,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  Equal,
  NotEqual,
  LogicalAnd,
  LogicalOr,
};

std::string_view unary_op_name(UnaryOp op);
std::string_view binary_op_name(BinaryOp op);

enum class CallArgumentKind {
  Value,
  LocalRef,
};

struct CallArgument {
  CallArgumentKind kind = CallArgumentKind::Value;
  Type type{};
  ValueId value = kInvalidId;
  LocalId local = kInvalidId;
  std::vector<ValueId> indices;
};

enum class InstructionKind {
  ConstInt,
  ConstBool,
  LoadLocal,
  StoreLocal,
  LoadElement,
  StoreElement,
  Unary,
  Binary,
  Call,
};

struct Instruction {
  InstructionKind kind = InstructionKind::ConstInt;
  std::optional<ValueId> result;
  Type result_type{};
  std::int64_t int_immediate = 0;
  bool bool_immediate = false;
  UnaryOp unary_op = UnaryOp::Negate;
  BinaryOp binary_op = BinaryOp::Add;
  LocalId local = kInvalidId;
  std::vector<ValueId> operands;
  std::string callee;
  std::vector<CallArgument> call_arguments;
};

enum class TerminatorKind {
  Jump,
  Branch,
  Return,
};

struct Terminator {
  TerminatorKind kind = TerminatorKind::Jump;
  std::optional<ValueId> condition;
  BlockId true_target = kInvalidId;
  BlockId false_target = kInvalidId;
  std::optional<ValueId> return_value;
};

struct BasicBlock {
  BlockId id = 0;
  std::string label;
  std::vector<Instruction> instructions;
  std::optional<Terminator> terminator;
};

struct Function {
  std::string name;
  Type return_type{};
  std::vector<ParameterInfo> parameters;
  std::vector<LocalInfo> locals;
  std::vector<ValueInfo> values;
  std::vector<BasicBlock> blocks;
  BlockId entry_block = 0;
};

struct Module {
  std::vector<Function> functions;
};

[[nodiscard]] std::string value_name(ValueId id);
[[nodiscard]] const LocalInfo& local_info(const Function& function, LocalId id);
[[nodiscard]] const ValueInfo& value_info(const Function& function, ValueId id);

[[nodiscard]] std::vector<ValueId> instruction_uses(const Instruction& instruction);
[[nodiscard]] std::optional<ValueId> instruction_def(const Instruction& instruction);
[[nodiscard]] std::vector<ValueId> terminator_uses(const Terminator& terminator);

}  // namespace nexus::compiler::ir
