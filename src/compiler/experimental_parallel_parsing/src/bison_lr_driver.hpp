#pragma once

#include <string>

#include "nexus/compiler/experimental_parallel_parsing/bison_lr.hpp"

namespace nexus::compiler::experimental_parallel_parsing {

struct ParseState {
  BisonLrParseResult result{};
  std::string source_text;
};

}  // namespace nexus::compiler::experimental_parallel_parsing
