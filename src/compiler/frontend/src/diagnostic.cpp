#include "nexus/compiler/frontend/diagnostic.hpp"

#include <sstream>

namespace nexus::compiler::frontend {

namespace {

std::string_view line_text_at(std::string_view source_text, std::size_t line_number) {
  std::size_t current_line = 1;
  std::size_t line_start = 0;

  for (std::size_t index = 0; index < source_text.size(); ++index) {
    if (current_line == line_number) {
      std::size_t line_end = source_text.find('\n', line_start);
      if (line_end == std::string_view::npos) {
        line_end = source_text.size();
      }
      return source_text.substr(line_start, line_end - line_start);
    }

    if (source_text[index] == '\n') {
      ++current_line;
      line_start = index + 1;
    }
  }

  if (current_line == line_number) {
    return source_text.substr(line_start);
  }

  return {};
}

}  // namespace

std::string_view diagnostic_severity_name(DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::Error:
      return "error";
    case DiagnosticSeverity::Warning:
      return "warning";
    case DiagnosticSeverity::Note:
      return "note";
  }

  return "error";
}

std::string format_diagnostic(
    std::string_view file_name,
    std::string_view source_text,
    const Diagnostic& diagnostic) {
  std::ostringstream output;
  output << file_name << ':' << diagnostic.span.begin.line << ':' << diagnostic.span.begin.column
         << ": " << diagnostic_severity_name(diagnostic.severity) << ": " << diagnostic.message;

  const std::string_view line_text = line_text_at(source_text, diagnostic.span.begin.line);
  if (!line_text.empty()) {
    output << '\n' << line_text << '\n';
    for (std::size_t index = 1; index < diagnostic.span.begin.column; ++index) {
      output << ' ';
    }
    output << '^';
  }

  return output.str();
}

std::string format_diagnostics(
    std::string_view file_name,
    std::string_view source_text,
    const std::vector<Diagnostic>& diagnostics) {
  std::ostringstream output;
  for (std::size_t index = 0; index < diagnostics.size(); ++index) {
    if (index != 0) {
      output << '\n';
    }
    output << format_diagnostic(file_name, source_text, diagnostics[index]);
  }
  return output.str();
}

}  // namespace nexus::compiler::frontend
