#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "nexus/compiler/analysis/cfg.hpp"

namespace nexus::compiler::analysis {

enum class SymbolicKind {
  Unknown,
  IntegerConstant,
  BooleanConstant,
  Named,
  Expression,
};

struct SymbolicValue {
  ir::Type type{};
  SymbolicKind kind = SymbolicKind::Unknown;
  std::int64_t int_value = 0;
  bool bool_value = false;
  std::string text;
};

bool operator==(const SymbolicValue& lhs, const SymbolicValue& rhs);

struct SymbolicResult {
  std::vector<std::vector<SymbolicValue>> block_in_locals;
  std::vector<std::vector<SymbolicValue>> block_out_locals;
  std::vector<SymbolicValue> value_symbols;
  std::size_t iterations = 0;
};

SymbolicResult analyze_symbolic(const ir::Function& function, const ControlFlowGraph& cfg);
std::string print_symbolic(
    const ir::Function& function,
    const ControlFlowGraph& cfg,
    const SymbolicResult& result);

}  // namespace nexus::compiler::analysis
