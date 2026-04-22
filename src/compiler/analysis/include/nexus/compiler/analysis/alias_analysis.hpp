#pragma once

#include <optional>
#include <string>
#include <vector>

#include "nexus/compiler/ir/ir.hpp"

namespace nexus::compiler::analysis {

enum class AliasKind {
  NoAlias,
  MayAlias,
  MustAlias,
};

std::string_view alias_kind_name(AliasKind kind);

struct MemoryReference {
  std::size_t id = 0;
  ir::BlockId block = 0;
  std::size_t instruction_index = 0;
  ir::LocalId base_local = ir::kInvalidId;
  bool is_element = false;
  bool from_call = false;
  std::vector<std::optional<std::int64_t>> constant_indices;
  std::string label;
};

struct AliasAnalysisResult {
  std::vector<MemoryReference> references;
  std::vector<std::vector<AliasKind>> matrix;
};

AliasAnalysisResult analyze_aliases(const ir::Function& function);
std::string print_aliases(const ir::Function& function, const AliasAnalysisResult& result);

}  // namespace nexus::compiler::analysis
