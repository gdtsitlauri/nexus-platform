#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/compiler/frontend/ast.hpp"
#include "nexus/compiler/frontend/diagnostic.hpp"

namespace nexus::compiler::experimental_parallel_parsing {

enum class ExperimentalParseMode {
  Parallel,
};

struct PartitionSummary {
  std::size_t index = 0;
  frontend::SourceSpan span{};
  std::size_t line_count = 0;
};

struct ExperimentalParseResult {
  std::unique_ptr<frontend::Program> program;
  std::vector<frontend::Diagnostic> diagnostics;
  std::vector<PartitionSummary> partitions;
  std::size_t launched_tasks = 0;
};

ExperimentalParseResult parse_source_experimental(
    std::string_view source_text,
    ExperimentalParseMode mode = ExperimentalParseMode::Parallel);

std::string_view experimental_mode_name(ExperimentalParseMode mode);
std::string print_partition_summary(const ExperimentalParseResult& result);

}  // namespace nexus::compiler::experimental_parallel_parsing
