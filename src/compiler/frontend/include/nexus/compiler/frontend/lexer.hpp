#pragma once

#include <string_view>
#include <vector>

#include "nexus/compiler/frontend/diagnostic.hpp"
#include "nexus/compiler/frontend/token.hpp"

namespace nexus::compiler::frontend {

struct LexResult {
  std::vector<Token> tokens;
  std::vector<Diagnostic> diagnostics;
};

LexResult lex_source(std::string_view source_text);

}  // namespace nexus::compiler::frontend
