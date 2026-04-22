#pragma once

#include <memory>
#include <vector>

#include "nexus/compiler/frontend/ast.hpp"
#include "nexus/compiler/frontend/diagnostic.hpp"
#include "nexus/compiler/ir/ir.hpp"

namespace nexus::compiler::ir {

struct LoweringResult {
  std::unique_ptr<Module> module;
  std::vector<frontend::Diagnostic> diagnostics;
};

LoweringResult lower_program(const frontend::Program& program);

}  // namespace nexus::compiler::ir
