#pragma once

#include <string>
#include <vector>

#include "nexus/compiler/ir/ir.hpp"

namespace nexus::compiler::passes {

struct InterproceduralFoldResult {
  ir::Module module;
  std::size_t replaced_calls = 0;
  std::vector<std::string> notes;
};

InterproceduralFoldResult fold_interprocedural_constants(const ir::Module& module);

}  // namespace nexus::compiler::passes
