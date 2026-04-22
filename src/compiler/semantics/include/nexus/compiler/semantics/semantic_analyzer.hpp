#pragma once

#include <vector>

#include "nexus/compiler/frontend/ast.hpp"
#include "nexus/compiler/frontend/diagnostic.hpp"

namespace nexus::compiler::semantics {

struct SemanticResult {
  std::vector<frontend::Diagnostic> diagnostics;
};

SemanticResult analyze_program(const frontend::Program& program);

}  // namespace nexus::compiler::semantics
