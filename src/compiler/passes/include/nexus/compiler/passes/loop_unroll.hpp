#pragma once

#include <string>
#include <vector>

#include "nexus/compiler/ir/ir.hpp"

namespace nexus::compiler::passes {

enum class LoopUnrollMode {
  Concrete,
  Symbolic,
};

struct LoopUnrollResult {
  ir::Module module;
  std::size_t transformed_loops = 0;
  std::vector<std::string> notes;
};

LoopUnrollResult unroll_loops(const ir::Module& module, LoopUnrollMode mode);

}  // namespace nexus::compiler::passes
