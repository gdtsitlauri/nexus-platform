#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "nexus/compiler/frontend/ast.hpp"
#include "nexus/compiler/frontend/diagnostic.hpp"

namespace nexus::compiler::frontend {

struct ParseResult {
  std::unique_ptr<Program> program;
  std::vector<Diagnostic> diagnostics;
};

ParseResult parse_source(std::string_view source_text);

}  // namespace nexus::compiler::frontend
