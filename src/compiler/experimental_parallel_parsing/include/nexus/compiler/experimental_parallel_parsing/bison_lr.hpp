#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/compiler/frontend/diagnostic.hpp"

namespace nexus::compiler::experimental_parallel_parsing {

struct BisonLrSummary {
  std::size_t function_count = 0;
  std::size_t variable_decl_count = 0;
  std::size_t return_count = 0;
  std::size_t if_count = 0;
  std::size_t while_count = 0;
};

struct BisonLrParseResult {
  BisonLrSummary summary{};
  std::vector<frontend::Diagnostic> diagnostics;
};

BisonLrParseResult parse_source_bison_lr(std::string_view source_text);
std::string print_bison_lr_summary(const BisonLrParseResult& result);

}  // namespace nexus::compiler::experimental_parallel_parsing
