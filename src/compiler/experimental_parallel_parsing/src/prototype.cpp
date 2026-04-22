#include "nexus/compiler/experimental_parallel_parsing/prototype.hpp"

#include <algorithm>
#include <future>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "nexus/compiler/frontend/lexer.hpp"
#include "nexus/compiler/frontend/parser.hpp"
#include "nexus/compiler/frontend/token.hpp"

namespace nexus::compiler::experimental_parallel_parsing {

namespace {

using frontend::Diagnostic;
using frontend::DiagnosticSeverity;
using frontend::Program;
using frontend::SourceLocation;
using frontend::SourceSpan;
using frontend::Token;
using frontend::TokenKind;

struct PartitionSlice {
  std::size_t index = 0;
  std::size_t begin_index = 0;
  std::size_t end_index = 0;
  SourceSpan span{};
  std::string text;
};

std::size_t location_to_index(std::string_view source_text, SourceLocation location) {
  std::size_t line = 1;
  std::size_t column = 1;
  for (std::size_t index = 0; index < source_text.size(); ++index) {
    if (line == location.line && column == location.column) {
      return index;
    }
    if (source_text[index] == '\n') {
      ++line;
      column = 1;
    } else {
      ++column;
    }
  }
  return source_text.size();
}

SourceSpan shift_span(SourceSpan span, const SourceSpan& base) {
  span.begin.line += base.begin.line - 1;
  span.end.line += base.begin.line - 1;
  if (span.begin.line == base.begin.line) {
    span.begin.column += base.begin.column - 1;
  }
  if (span.end.line == base.begin.line) {
    span.end.column += base.begin.column - 1;
  }
  return span;
}

std::vector<Diagnostic> shift_diagnostics(
    const std::vector<Diagnostic>& diagnostics,
    const SourceSpan& base) {
  std::vector<Diagnostic> shifted;
  shifted.reserve(diagnostics.size());
  for (const auto& diagnostic : diagnostics) {
    shifted.push_back(
        Diagnostic{
            .severity = diagnostic.severity,
            .span = shift_span(diagnostic.span, base),
            .message = diagnostic.message,
        });
  }
  return shifted;
}

std::size_t count_lines(std::string_view text) {
  return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + (text.empty() ? 0U : 1U);
}

std::optional<std::vector<PartitionSlice>> split_top_level_functions(
    std::string_view source_text,
    std::vector<Diagnostic>& diagnostics) {
  const auto lexed = frontend::lex_source(source_text);
  if (!lexed.diagnostics.empty()) {
    diagnostics = lexed.diagnostics;
    return std::nullopt;
  }

  std::vector<PartitionSlice> slices;
  std::size_t token_index = 0;

  while (token_index < lexed.tokens.size()) {
    const Token& token = lexed.tokens[token_index];
    if (token.kind == TokenKind::EndOfFile) {
      break;
    }

    if (token.kind != TokenKind::Fn) {
      diagnostics.push_back(
          Diagnostic{
              .severity = DiagnosticSeverity::Error,
              .span = token.span,
              .message = "experimental parallel parser expects top-level function declarations only",
          });
      return std::nullopt;
    }

    std::size_t scan = token_index;
    bool found_body = false;
    std::size_t brace_depth = 0;
    std::size_t end_token_index = token_index;

    for (; scan < lexed.tokens.size(); ++scan) {
      const TokenKind kind = lexed.tokens[scan].kind;
      if (kind == TokenKind::LeftBrace) {
        found_body = true;
        ++brace_depth;
      } else if (kind == TokenKind::RightBrace) {
        if (brace_depth == 0) {
          diagnostics.push_back(
              Diagnostic{
                  .severity = DiagnosticSeverity::Error,
                  .span = lexed.tokens[scan].span,
                  .message = "unexpected top-level '}' while partitioning function slices",
              });
          return std::nullopt;
        }
        --brace_depth;
        if (found_body && brace_depth == 0) {
          end_token_index = scan;
          break;
        }
      }
    }

    if (!found_body || brace_depth != 0) {
      diagnostics.push_back(
          Diagnostic{
              .severity = DiagnosticSeverity::Error,
              .span = token.span,
              .message = "failed to find a complete function body during experimental partitioning",
          });
      return std::nullopt;
    }

    const std::size_t begin_index = location_to_index(source_text, token.span.begin);
    const std::size_t end_index = location_to_index(source_text, lexed.tokens[end_token_index].span.end);
    slices.push_back(
        PartitionSlice{
            .index = slices.size(),
            .begin_index = begin_index,
            .end_index = end_index,
            .span = SourceSpan{.begin = token.span.begin, .end = lexed.tokens[end_token_index].span.end},
            .text = std::string(source_text.substr(begin_index, end_index - begin_index)),
        });
    token_index = end_token_index + 1;
  }

  return slices;
}

}  // namespace

std::string_view experimental_mode_name(ExperimentalParseMode mode) {
  switch (mode) {
    case ExperimentalParseMode::Parallel:
      return "parallel";
  }
  return "parallel";
}

ExperimentalParseResult parse_source_experimental(std::string_view source_text, ExperimentalParseMode mode) {
  ExperimentalParseResult result;
  if (mode != ExperimentalParseMode::Parallel) {
    result.diagnostics.push_back(
        Diagnostic{
            .severity = DiagnosticSeverity::Error,
            .span = SourceSpan{},
            .message = "unsupported experimental parse mode",
        });
    return result;
  }

  auto slices = split_top_level_functions(source_text, result.diagnostics);
  if (!slices.has_value()) {
    return result;
  }

  result.partitions.reserve(slices->size());
  std::vector<std::future<frontend::ParseResult>> futures;
  futures.reserve(slices->size());

  for (const auto& slice : *slices) {
    result.partitions.push_back(
        PartitionSummary{
            .index = slice.index,
            .span = slice.span,
            .line_count = count_lines(slice.text),
        });
    futures.push_back(std::async(
        std::launch::async,
        [text = slice.text]() {
          return frontend::parse_source(text);
        }));
  }
  result.launched_tasks = futures.size();

  auto program = std::make_unique<Program>(SourceSpan{}, std::vector<frontend::FunctionDecl>{});
  for (std::size_t index = 0; index < futures.size(); ++index) {
    auto parsed = futures[index].get();
    if (!parsed.diagnostics.empty()) {
      auto shifted = shift_diagnostics(parsed.diagnostics, (*slices)[index].span);
      result.diagnostics.insert(result.diagnostics.end(), shifted.begin(), shifted.end());
      continue;
    }
    if (!parsed.program) {
      result.diagnostics.push_back(
          Diagnostic{
              .severity = DiagnosticSeverity::Error,
              .span = (*slices)[index].span,
              .message = "experimental parser worker returned no program",
          });
      continue;
    }

    std::move(
        parsed.program->functions.begin(),
        parsed.program->functions.end(),
        std::back_inserter(program->functions));
  }

  if (!result.diagnostics.empty()) {
    return result;
  }

  result.program = std::move(program);
  return result;
}

std::string print_partition_summary(const ExperimentalParseResult& result) {
  std::string output;
  output += "experimental parallel parse partitions:\n";
  output += "  tasks: " + std::to_string(result.launched_tasks) + "\n";
  for (const auto& partition : result.partitions) {
    output += "  part" + std::to_string(partition.index) + ": lines " +
        std::to_string(partition.span.begin.line) + "-" + std::to_string(partition.span.end.line) +
        " (" + std::to_string(partition.line_count) + " line(s))\n";
  }
  return output;
}

}  // namespace nexus::compiler::experimental_parallel_parsing
