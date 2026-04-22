#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "nexus/compiler/ir/ir.hpp"

namespace nexus::compiler::passes {

struct StripMineResult {
  ir::Module module;
  std::size_t transformed_loops = 0;
  std::vector<std::string> notes;
};

StripMineResult strip_mine_loops(const ir::Module& module, std::size_t tile_factor = 2);

}  // namespace nexus::compiler::passes
