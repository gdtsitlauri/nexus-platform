#include "nexus/compiler/experimental_parallel_parsing/bison_lr.hpp"

#include <sstream>
#include <string>

#include "bison_lr_driver.hpp"
#include "flex_bison_parser.hpp"

struct yy_buffer_state;
using NexusFbBufferState = yy_buffer_state*;

extern NexusFbBufferState nexus_fb__scan_string(const char* text);
extern void nexus_fb__delete_buffer(NexusFbBufferState buffer);
extern int nexus_fb_lex_destroy(void);
extern void nexus_fb_reset_column(void);
extern int nexus_fb_lineno;

namespace nexus::compiler::experimental_parallel_parsing {

using frontend::Diagnostic;
using frontend::DiagnosticSeverity;
using frontend::SourceLocation;
using frontend::SourceSpan;

SourceSpan to_span(const NEXUS_FB_LTYPE& location) {
  return SourceSpan{
      .begin =
          SourceLocation{
              .line = static_cast<std::size_t>(location.first_line),
              .column = static_cast<std::size_t>(location.first_column),
          },
      .end =
          SourceLocation{
              .line = static_cast<std::size_t>(location.last_line),
              .column = static_cast<std::size_t>(location.last_column + 1),
          },
  };
}

}  // namespace nexus::compiler::experimental_parallel_parsing

void nexus_fb_error(
    NEXUS_FB_LTYPE* loc,
    nexus::compiler::experimental_parallel_parsing::ParseState* state,
    const char* msg) {
  using nexus::compiler::frontend::Diagnostic;
  using nexus::compiler::frontend::DiagnosticSeverity;
  using nexus::compiler::frontend::SourceSpan;
  state->result.diagnostics.push_back(
      Diagnostic{
          .severity = DiagnosticSeverity::Error,
          .span = loc == nullptr ? SourceSpan{} : nexus::compiler::experimental_parallel_parsing::to_span(*loc),
          .message = std::string("bison-lr: ") + msg,
      });
}

namespace nexus::compiler::experimental_parallel_parsing {

BisonLrParseResult parse_source_bison_lr(std::string_view source_text) {
  ParseState state;
  state.source_text.assign(source_text.begin(), source_text.end());

  nexus_fb_lineno = 1;
  nexus_fb_reset_column();
  NexusFbBufferState buffer = nexus_fb__scan_string(state.source_text.c_str());
  const int parse_status = nexus_fb_parse(&state);
  nexus_fb__delete_buffer(buffer);
  nexus_fb_lex_destroy();

  if (parse_status != 0 && state.result.diagnostics.empty()) {
    state.result.diagnostics.push_back(
        Diagnostic{
            .severity = DiagnosticSeverity::Error,
            .span = SourceSpan{},
            .message = "bison-lr: parse failed without a specific diagnostic",
        });
  }

  return state.result;
}

std::string print_bison_lr_summary(const BisonLrParseResult& result) {
  std::ostringstream output;
  output << "experimental bison-lr parse summary:\n";
  output << "  functions: " << result.summary.function_count << '\n';
  output << "  var-decls: " << result.summary.variable_decl_count << '\n';
  output << "  returns: " << result.summary.return_count << '\n';
  output << "  ifs: " << result.summary.if_count << '\n';
  output << "  whiles: " << result.summary.while_count << '\n';
  return output.str();
}

}  // namespace nexus::compiler::experimental_parallel_parsing
