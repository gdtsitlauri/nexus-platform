#pragma once

#include <string>
#include <vector>

#include "nexus/compiler/ir/ir.hpp"

namespace nexus::compiler::passes {

enum class LoopUnrollMode {
  Concrete,
  Symbolic,
  // Symbolic unrolling by a factor K with a remainder loop: the main loop runs K guard-free copies of the body
  // while i + (K-1)*step still satisfies the condition, and the original loop finishes the remaining iterations.
  SymbolicRemainder,
};

struct LoopUnrollResult {
  ir::Module module;
  std::size_t transformed_loops = 0;
  std::vector<std::string> notes;
};

LoopUnrollResult unroll_loops(const ir::Module& module, LoopUnrollMode mode, std::size_t factor = 2);

}  // namespace nexus::compiler::passes
