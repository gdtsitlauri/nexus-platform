#pragma once

#include <optional>
#include <string>
#include <vector>

#include "nexus/compiler/ir/ir.hpp"

namespace nexus::compiler::analysis {

struct AffineLoopSummary {
  bool supported = false;
  ir::BlockId preheader = ir::kInvalidId;
  ir::BlockId cond = ir::kInvalidId;
  ir::BlockId body = ir::kInvalidId;
  ir::BlockId exit = ir::kInvalidId;
  std::string induction_local;
  std::optional<std::int64_t> init_constant;
  std::optional<std::int64_t> limit_constant;
  std::int64_t step = 0;
  std::optional<std::int64_t> trip_count;
  std::vector<std::string> memory_locals;
};

AffineLoopSummary analyze_affine_loop(const ir::Function& function);
std::string print_affine_loop(const ir::Function& function, const AffineLoopSummary& summary);

}  // namespace nexus::compiler::analysis
