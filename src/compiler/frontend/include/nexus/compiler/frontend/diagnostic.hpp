#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace nexus::compiler::frontend {

struct SourceLocation {
  std::size_t line = 1;
  std::size_t column = 1;
};

struct SourceSpan {
  SourceLocation begin{};
  SourceLocation end{};
};

enum class DiagnosticSeverity {
  Error,
  Warning,
  Note,
};

struct Diagnostic {
  DiagnosticSeverity severity = DiagnosticSeverity::Error;
  SourceSpan span{};
  std::string message;
};

std::string_view diagnostic_severity_name(DiagnosticSeverity severity);
std::string format_diagnostic(
    std::string_view file_name,
    std::string_view source_text,
    const Diagnostic& diagnostic);
std::string format_diagnostics(
    std::string_view file_name,
    std::string_view source_text,
    const std::vector<Diagnostic>& diagnostics);

}  // namespace nexus::compiler::frontend
