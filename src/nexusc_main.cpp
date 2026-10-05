#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "nexus/common/banner.hpp"
#include "nexus/compiler/analysis/cfg.hpp"
#include "nexus/compiler/analysis/dominators.hpp"
#include "nexus/compiler/analysis/affine_analysis.hpp"
#include "nexus/compiler/analysis/interprocedural.hpp"
#include "nexus/compiler/analysis/liveness.hpp"
#include "nexus/compiler/analysis/region_flow.hpp"
#include "nexus/compiler/analysis/symbolic.hpp"
#include "nexus/compiler/analysis/alias_analysis.hpp"
#include "nexus/compiler/backend_mips/codegen.hpp"
#include "nexus/compiler/backend_mips/regalloc.hpp"
#include "nexus/compiler/experimental_parallel_parsing/bison_lr.hpp"
#include "nexus/compiler/experimental_parallel_parsing/prototype.hpp"
#include "nexus/compiler/frontend/ast_printer.hpp"
#include "nexus/compiler/frontend/diagnostic.hpp"
#include "nexus/compiler/frontend/lexer.hpp"
#include "nexus/compiler/frontend/parser.hpp"
#include "nexus/compiler/ir/lowering.hpp"
#include "nexus/compiler/isa_styles/isa_styles.hpp"
#include "nexus/compiler/ir/printer.hpp"
#include "nexus/compiler/passes/interprocedural_pass.hpp"
#include "nexus/compiler/passes/affine_stripmine.hpp"
#include "nexus/compiler/passes/loop_unroll.hpp"
#include "nexus/compiler/semantics/semantic_analyzer.hpp"
#include "nexus/mips/assembler_support/printer.hpp"
#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/functional/interpreter.hpp"

namespace {

struct LoadedSource {
  std::string file_name;
  std::string text;
};

void print_usage(std::ostream& stream) {
  stream << "Usage:\n"
         << "  nexusc --help\n"
         << "  nexusc lex <file>\n"
         << "  nexusc parse <file>\n"
         << "  nexusc ast <file>\n"
         << "  nexusc check <file>\n"
         << "  nexusc ir <file>\n"
         << "  nexusc cfg <file>\n"
         << "  nexusc dom <file>\n"
         << "  nexusc analysis liveness <file>\n"
         << "  nexusc analysis regalloc <file>\n"
         << "  nexusc experimental-parse <file> --mode parallel|bison-lr\n"
         << "  nexusc opt <file> --analysis symbolic|region-liveness|affine|alias|interproc\n"
         << "  nexusc opt <file> --pass unroll|unroll-symbolic|strip-mine|interproc-constfold\n"
         << "  nexusc compile <file> -S [-o out.s]\n"
         << "  nexusc compile <file> -S --regalloc none|linear-scan [-o out.s]\n"
         << "  nexusc compile <file> --emit-ir\n"
         << "  nexusc isa <file> [--style stack|accumulator|register-memory|all] [--listing]\n";
}

bool load_source_file(const std::string& file_name, LoadedSource& loaded_source) {
  std::ifstream input(file_name);
  if (!input) {
    std::cerr << "error: failed to open source file '" << file_name << "'\n";
    return false;
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  loaded_source = LoadedSource{.file_name = file_name, .text = buffer.str()};
  return true;
}

int print_frontend_diagnostics(
    const LoadedSource& loaded_source,
    const std::vector<nexus::compiler::frontend::Diagnostic>& diagnostics) {
  if (diagnostics.empty()) {
    return 0;
  }

  std::cerr << nexus::compiler::frontend::format_diagnostics(
                   loaded_source.file_name, loaded_source.text, diagnostics)
            << '\n';
  return 1;
}

struct CheckedProgram {
  std::unique_ptr<nexus::compiler::frontend::Program> program;
};

std::optional<CheckedProgram> parse_and_check(const LoadedSource& loaded_source) {
  auto parsed = nexus::compiler::frontend::parse_source(loaded_source.text);
  if (!parsed.diagnostics.empty()) {
    print_frontend_diagnostics(loaded_source, parsed.diagnostics);
    return std::nullopt;
  }

  const auto checked = nexus::compiler::semantics::analyze_program(*parsed.program);
  if (!checked.diagnostics.empty()) {
    print_frontend_diagnostics(loaded_source, checked.diagnostics);
    return std::nullopt;
  }

  return CheckedProgram{.program = std::move(parsed.program)};
}

std::optional<nexus::compiler::ir::Module> lower_checked_program(
    const LoadedSource& loaded_source,
    const nexus::compiler::frontend::Program& program) {
  const auto lowered = nexus::compiler::ir::lower_program(program);
  if (!lowered.diagnostics.empty()) {
    print_frontend_diagnostics(loaded_source, lowered.diagnostics);
    return std::nullopt;
  }
  return std::move(*lowered.module);
}

int print_backend_diagnostics(const std::vector<std::string>& diagnostics) {
  for (const auto& diagnostic : diagnostics) {
    std::cerr << "error: " << diagnostic << '\n';
  }
  return diagnostics.empty() ? 0 : 1;
}

std::optional<std::string> lower_to_assembly_text(
    const nexus::compiler::ir::Module& module,
    const nexus::compiler::backend_mips::CodegenOptions& codegen_options = {}) {
  const auto backend_result = nexus::compiler::backend_mips::lower_module(module, codegen_options);
  if (!backend_result.diagnostics.empty()) {
    print_backend_diagnostics(backend_result.diagnostics);
    return std::nullopt;
  }

  return nexus::mips::assembler_support::print_program(*backend_result.program);
}

bool write_output_file(const std::string& file_name, const std::string& text) {
  std::ofstream output(file_name);
  if (!output) {
    std::cerr << "error: failed to open output file '" << file_name << "'\n";
    return false;
  }
  output << text;
  return true;
}

int run_lex(const LoadedSource& loaded_source) {
  const auto result = nexus::compiler::frontend::lex_source(loaded_source.text);
  if (!result.diagnostics.empty()) {
    return print_frontend_diagnostics(loaded_source, result.diagnostics);
  }

  std::cout << nexus::compiler::frontend::dump_tokens(result.tokens);
  return 0;
}

int run_parse(const LoadedSource& loaded_source) {
  const auto result = nexus::compiler::frontend::parse_source(loaded_source.text);
  if (!result.diagnostics.empty()) {
    return print_frontend_diagnostics(loaded_source, result.diagnostics);
  }

  std::cout << "Parse succeeded: " << result.program->functions.size() << " function(s)\n";
  return 0;
}

int run_ast(const LoadedSource& loaded_source) {
  const auto result = nexus::compiler::frontend::parse_source(loaded_source.text);
  if (!result.diagnostics.empty()) {
    return print_frontend_diagnostics(loaded_source, result.diagnostics);
  }

  std::cout << nexus::compiler::frontend::print_program(*result.program);
  return 0;
}

int run_check(const LoadedSource& loaded_source) {
  const auto checked_program = parse_and_check(loaded_source);
  if (!checked_program.has_value()) {
    return 1;
  }

  std::cout << "Semantic check succeeded: " << checked_program->program->functions.size()
            << " function(s) validated\n";
  return 0;
}

int run_ir(const LoadedSource& loaded_source) {
  const auto checked_program = parse_and_check(loaded_source);
  if (!checked_program.has_value()) {
    return 1;
  }

  const auto module = lower_checked_program(loaded_source, *checked_program->program);
  if (!module.has_value()) {
    return 1;
  }

  std::cout << nexus::compiler::ir::print_module(*module);
  return 0;
}

int run_cfg(const LoadedSource& loaded_source) {
  const auto checked_program = parse_and_check(loaded_source);
  if (!checked_program.has_value()) {
    return 1;
  }

  const auto module = lower_checked_program(loaded_source, *checked_program->program);
  if (!module.has_value()) {
    return 1;
  }

  for (std::size_t index = 0; index < module->functions.size(); ++index) {
    if (index != 0) {
      std::cout << '\n';
    }
    const auto cfg = nexus::compiler::analysis::build_cfg(module->functions[index]);
    std::cout << nexus::compiler::analysis::print_cfg(module->functions[index], cfg);
  }
  return 0;
}

int run_dom(const LoadedSource& loaded_source) {
  const auto checked_program = parse_and_check(loaded_source);
  if (!checked_program.has_value()) {
    return 1;
  }

  const auto module = lower_checked_program(loaded_source, *checked_program->program);
  if (!module.has_value()) {
    return 1;
  }

  for (std::size_t index = 0; index < module->functions.size(); ++index) {
    if (index != 0) {
      std::cout << '\n';
    }
    const auto cfg = nexus::compiler::analysis::build_cfg(module->functions[index]);
    const auto dom = nexus::compiler::analysis::compute_dominators(cfg);
    std::cout << nexus::compiler::analysis::print_dominators(module->functions[index], cfg, dom);
  }
  return 0;
}

int run_analysis(const LoadedSource& loaded_source, const std::string& analysis_name) {
  const auto checked_program = parse_and_check(loaded_source);
  if (!checked_program.has_value()) {
    return 1;
  }

  const auto module = lower_checked_program(loaded_source, *checked_program->program);
  if (!module.has_value()) {
    return 1;
  }

  if (analysis_name == "interproc") {
    const auto summaries = nexus::compiler::analysis::analyze_interprocedural(*module);
    std::cout << nexus::compiler::analysis::print_interprocedural(*module, summaries);
    return 0;
  }

  for (std::size_t index = 0; index < module->functions.size(); ++index) {
    if (index != 0) {
      std::cout << '\n';
    }
    if (analysis_name == "liveness") {
      const auto cfg = nexus::compiler::analysis::build_cfg(module->functions[index]);
      const auto live = nexus::compiler::analysis::analyze_liveness(module->functions[index], cfg);
      std::cout << nexus::compiler::analysis::print_liveness(module->functions[index], cfg, live);
      continue;
    }
    if (analysis_name == "regalloc") {
      const auto allocation = nexus::compiler::backend_mips::allocate_registers_linear_scan(module->functions[index]);
      std::cout << nexus::compiler::backend_mips::print_register_allocation(module->functions[index], allocation);
      continue;
    }
    if (analysis_name == "symbolic") {
      const auto cfg = nexus::compiler::analysis::build_cfg(module->functions[index]);
      const auto symbolic = nexus::compiler::analysis::analyze_symbolic(module->functions[index], cfg);
      std::cout << nexus::compiler::analysis::print_symbolic(module->functions[index], cfg, symbolic);
      continue;
    }
    if (analysis_name == "region-liveness") {
      const auto cfg = nexus::compiler::analysis::build_cfg(module->functions[index]);
      const auto region = nexus::compiler::analysis::analyze_region_liveness(module->functions[index], cfg);
      std::cout << nexus::compiler::analysis::print_region_liveness(module->functions[index], region);
      continue;
    }
    if (analysis_name == "alias") {
      const auto alias = nexus::compiler::analysis::analyze_aliases(module->functions[index]);
      std::cout << nexus::compiler::analysis::print_aliases(module->functions[index], alias);
      continue;
    }
    if (analysis_name == "affine") {
      const auto affine = nexus::compiler::analysis::analyze_affine_loop(module->functions[index]);
      std::cout << nexus::compiler::analysis::print_affine_loop(module->functions[index], affine);
      continue;
    }
    std::cerr << "error: unknown analysis '" << analysis_name << "'\n";
    return 1;
  }
  return 0;
}

int run_experimental_parse(const LoadedSource& loaded_source, const std::string& mode_name) {
  if (mode_name == "parallel") {
    const auto result = nexus::compiler::experimental_parallel_parsing::parse_source_experimental(
        loaded_source.text,
        nexus::compiler::experimental_parallel_parsing::ExperimentalParseMode::Parallel);
    if (!result.diagnostics.empty()) {
      return print_frontend_diagnostics(loaded_source, result.diagnostics);
    }

    std::cout << nexus::compiler::experimental_parallel_parsing::print_partition_summary(result);
    std::cout << "Experimental parallel parse succeeded: " << result.program->functions.size()
              << " function(s)\n";
    return 0;
  }

  if (mode_name == "bison-lr") {
    const auto result = nexus::compiler::experimental_parallel_parsing::parse_source_bison_lr(
        loaded_source.text);
    if (!result.diagnostics.empty()) {
      return print_frontend_diagnostics(loaded_source, result.diagnostics);
    }

    std::cout << nexus::compiler::experimental_parallel_parsing::print_bison_lr_summary(result);
    std::cout << "Experimental bison-lr parse succeeded: " << result.summary.function_count
              << " function(s)\n";
    return 0;
  }

  std::cerr << "error: unsupported experimental parse mode '" << mode_name
            << "', expected 'parallel' or 'bison-lr'\n";
  return 1;
}

struct OptOptions {
  std::optional<std::string> analysis;
  std::optional<std::string> pass;
};

int run_opt(const LoadedSource& loaded_source, const OptOptions& options) {
  if (options.analysis.has_value() == options.pass.has_value()) {
    std::cerr << "error: nexusc opt requires exactly one of --analysis or --pass\n";
    return 1;
  }

  if (options.analysis.has_value()) {
    return run_analysis(loaded_source, *options.analysis);
  }

  const auto checked_program = parse_and_check(loaded_source);
  if (!checked_program.has_value()) {
    return 1;
  }
  const auto module = lower_checked_program(loaded_source, *checked_program->program);
  if (!module.has_value()) {
    return 1;
  }

  if (*options.pass == "unroll" || *options.pass == "unroll-symbolic") {
    const auto unrolled = nexus::compiler::passes::unroll_loops(
        *module,
        *options.pass == "unroll"
            ? nexus::compiler::passes::LoopUnrollMode::Concrete
            : nexus::compiler::passes::LoopUnrollMode::Symbolic);
    std::cout << nexus::compiler::ir::print_module(unrolled.module);
    for (const auto& note : unrolled.notes) {
      std::cout << "; " << note << '\n';
    }
    return 0;
  }

  if (*options.pass == "strip-mine") {
    const auto strip_mined = nexus::compiler::passes::strip_mine_loops(*module, 2);
    std::cout << nexus::compiler::ir::print_module(strip_mined.module);
    for (const auto& note : strip_mined.notes) {
      std::cout << "; " << note << '\n';
    }
    return 0;
  }

  if (*options.pass == "interproc-constfold") {
    const auto folded = nexus::compiler::passes::fold_interprocedural_constants(*module);
    std::cout << nexus::compiler::ir::print_module(folded.module);
    for (const auto& note : folded.notes) {
      std::cout << "; " << note << '\n';
    }
    return 0;
  }

  std::cerr << "error: unknown pass '" << *options.pass << "'\n";
  return 1;
}

struct IsaRow {
  std::string name;
  std::int32_t exit_code = 0;
  std::size_t static_instructions = 0;
  std::size_t code_bytes = 0;
  std::size_t dynamic_instructions = 0;
  std::size_t memory_reads = 0;
  std::size_t memory_writes = 0;
  std::string note;
};

std::optional<IsaRow> run_mips_row(
    const nexus::compiler::ir::Module& module,
    nexus::compiler::backend_mips::RegisterAllocationMode mode,
    const std::string& name) {
  const auto assembly = lower_to_assembly_text(module, {.register_allocation = mode});
  if (!assembly.has_value()) {
    return std::nullopt;
  }
  const auto loaded = nexus::mips::loader::load_program_from_text(*assembly);
  if (!loaded.diagnostics.empty() || !loaded.program.has_value()) {
    std::cerr << "error: generated MIPS failed to load\n";
    return std::nullopt;
  }
  const auto run = nexus::sim::functional::run_program(*loaded.program, {.trace = true, .max_instructions = 5'000'000});
  if (!run.success) {
    std::cerr << "error: MIPS run failed: " << run.error << '\n';
    return std::nullopt;
  }
  IsaRow row;
  row.name = name;
  row.exit_code = run.exit_code;
  row.static_instructions = loaded.program->instructions.size();
  row.code_bytes = 4 * row.static_instructions;
  row.dynamic_instructions = run.executed_instructions;
  for (const auto& line : run.trace_lines) {
    row.memory_reads += line.find(" lw ") != std::string::npos ? 1U : 0U;
    row.memory_writes += line.find(" sw ") != std::string::npos ? 1U : 0U;
  }
  return row;
}

int run_isa(const LoadedSource& loaded_source, const std::string& style_name, bool listing) {
  using nexus::compiler::isa_styles::IsaStyle;
  const auto checked_program = parse_and_check(loaded_source);
  if (!checked_program.has_value()) {
    return 1;
  }
  const auto module = lower_checked_program(loaded_source, *checked_program->program);
  if (!module.has_value()) {
    return 1;
  }

  std::vector<IsaStyle> styles;
  if (style_name == "all") {
    styles = {IsaStyle::Stack, IsaStyle::Accumulator, IsaStyle::RegisterMemory};
  } else if (style_name == "stack") {
    styles = {IsaStyle::Stack};
  } else if (style_name == "accumulator") {
    styles = {IsaStyle::Accumulator};
  } else if (style_name == "register-memory") {
    styles = {IsaStyle::RegisterMemory};
  } else {
    std::cerr << "error: unknown ISA style '" << style_name << "'\n";
    return 1;
  }

  std::vector<IsaRow> rows;
  for (const IsaStyle style : styles) {
    const auto result = nexus::compiler::isa_styles::compile_and_run(*module, style);
    if (listing) {
      std::cout << "== " << nexus::compiler::isa_styles::isa_style_name(style) << " listing ==\n"
                << result.program.listing << '\n';
    }
    if (!result.run.success) {
      std::cerr << "error: " << nexus::compiler::isa_styles::isa_style_name(style) << " run failed: " << result.run.error
                << '\n';
      return 1;
    }
    IsaRow row{
        .name = std::string(nexus::compiler::isa_styles::isa_style_name(style)),
        .exit_code = result.run.exit_code,
        .static_instructions = result.program.static_instructions,
        .code_bytes = result.program.code_bytes,
        .dynamic_instructions = result.run.dynamic_instructions,
        .memory_reads = result.run.memory_reads,
        .memory_writes = result.run.memory_writes,
        .note = style == IsaStyle::Stack ? "max operand stack " + std::to_string(result.run.max_operand_stack) : "",
    };
    rows.push_back(row);
  }
  if (style_name == "all") {
    using nexus::compiler::backend_mips::RegisterAllocationMode;
    for (const auto& [mode, name] : {std::pair{RegisterAllocationMode::StackOnly, "load-store (MIPS)"},
                                     std::pair{RegisterAllocationMode::LinearScan, "load-store (MIPS+regalloc)"}}) {
      const auto row = run_mips_row(*module, mode, name);
      if (!row.has_value()) {
        return 1;
      }
      rows.push_back(*row);
    }
  }

  std::cout << "ISA comparison for " << loaded_source.file_name << ":\n";
  std::cout << std::left << std::setw(28) << "style" << std::right << std::setw(6) << "exit" << std::setw(9) << "static"
            << std::setw(8) << "bytes" << std::setw(10) << "dynamic" << std::setw(9) << "reads" << std::setw(9)
            << "writes" << "  notes\n";
  bool agree = true;
  for (const auto& row : rows) {
    std::cout << std::left << std::setw(28) << row.name << std::right << std::setw(6) << row.exit_code << std::setw(9)
              << row.static_instructions << std::setw(8) << row.code_bytes << std::setw(10) << row.dynamic_instructions
              << std::setw(9) << row.memory_reads << std::setw(9) << row.memory_writes << "  " << row.note << '\n';
    agree = agree && row.exit_code == rows.front().exit_code;
  }
  std::cout << (agree ? "All ISA styles agree on the program result.\n" : "ISA styles DISAGREE on the program result.\n");
  return agree ? 0 : 1;
}

struct CompileOptions {
  bool emit_ir = false;
  bool emit_assembly = false;
  std::optional<std::string> output_file;
  nexus::compiler::backend_mips::CodegenOptions codegen{};
};

int run_compile(const LoadedSource& loaded_source, const CompileOptions& options) {
  const auto checked_program = parse_and_check(loaded_source);
  if (!checked_program.has_value()) {
    return 1;
  }

  const auto module = lower_checked_program(loaded_source, *checked_program->program);
  if (!module.has_value()) {
    return 1;
  }

  if (options.emit_ir) {
    std::cout << nexus::compiler::ir::print_module(*module);
    return 0;
  }

  const auto assembly = lower_to_assembly_text(*module, options.codegen);
  if (!assembly.has_value()) {
    return 1;
  }

  if (options.output_file.has_value()) {
    if (!write_output_file(*options.output_file, *assembly)) {
      return 1;
    }
    std::cout << "Wrote MIPS assembly to '" << *options.output_file << "'\n";
    return 0;
  }

  std::cout << *assembly;
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 1) {
    std::cout << nexus::common::banner_text("nexusc") << '\n'
              << "Commands: lex <file>, parse <file>, ast <file>, check <file>, ir <file>, "
                 "cfg <file>, dom <file>, analysis <name> <file>, opt <file>, "
                 "experimental-parse <file>, compile <file>, --help\n";
    return 0;
  }

  const std::string command = argv[1];
  if (command == "--help" || command == "help") {
    print_usage(std::cout);
    return 0;
  }

  if (command == "analysis") {
    if (argc != 4) {
      print_usage(std::cerr);
      return 1;
    }

    LoadedSource loaded_source;
    if (!load_source_file(argv[3], loaded_source)) {
      return 1;
    }
    return run_analysis(loaded_source, argv[2]);
  }

  if (command == "experimental-parse") {
    if (argc != 5 || std::string_view(argv[3]) != "--mode") {
      print_usage(std::cerr);
      return 1;
    }

    LoadedSource loaded_source;
    if (!load_source_file(argv[2], loaded_source)) {
      return 1;
    }
    return run_experimental_parse(loaded_source, argv[4]);
  }

  if (command == "opt") {
    if (argc < 5) {
      print_usage(std::cerr);
      return 1;
    }

    LoadedSource loaded_source;
    if (!load_source_file(argv[2], loaded_source)) {
      return 1;
    }

    OptOptions options;
    for (int index = 3; index < argc; ++index) {
      const std::string option = argv[index];
      if ((option == "--analysis" || option == "--pass") && index + 1 < argc) {
        if (option == "--analysis") {
          options.analysis = argv[++index];
        } else {
          options.pass = argv[++index];
        }
        continue;
      }
      std::cerr << "error: unknown opt option '" << option << "'\n";
      print_usage(std::cerr);
      return 1;
    }
    return run_opt(loaded_source, options);
  }

  if (command == "isa") {
    if (argc < 3) {
      print_usage(std::cerr);
      return 1;
    }
    LoadedSource loaded_source;
    if (!load_source_file(argv[2], loaded_source)) {
      return 1;
    }
    std::string style = "all";
    bool listing = false;
    for (int index = 3; index < argc; ++index) {
      const std::string option = argv[index];
      if (option == "--style" && index + 1 < argc) {
        style = argv[++index];
      } else if (option == "--listing") {
        listing = true;
      } else {
        std::cerr << "error: unknown isa option '" << option << "'\n";
        return 1;
      }
    }
    return run_isa(loaded_source, style, listing);
  }

  if (command == "compile") {
    if (argc < 3) {
      print_usage(std::cerr);
      return 1;
    }

    LoadedSource loaded_source;
    if (!load_source_file(argv[2], loaded_source)) {
      return 1;
    }

    CompileOptions options;
    for (int index = 3; index < argc; ++index) {
      const std::string option = argv[index];
      if (option == "--emit-ir") {
        options.emit_ir = true;
        continue;
      }
      if (option == "-S") {
        options.emit_assembly = true;
        continue;
      }
      if (option == "--regalloc") {
        if (index + 1 >= argc) {
          std::cerr << "error: missing mode after --regalloc\n";
          return 1;
        }
        const std::string mode = argv[++index];
        if (mode == "linear-scan") {
          options.codegen.register_allocation = nexus::compiler::backend_mips::RegisterAllocationMode::LinearScan;
        } else if (mode == "none") {
          options.codegen.register_allocation = nexus::compiler::backend_mips::RegisterAllocationMode::StackOnly;
        } else {
          std::cerr << "error: unknown --regalloc mode '" << mode << "', expected 'none' or 'linear-scan'\n";
          return 1;
        }
        continue;
      }
      if (option == "-o") {
        if (index + 1 >= argc) {
          std::cerr << "error: missing file name after -o\n";
          return 1;
        }
        options.output_file = argv[++index];
        continue;
      }

      std::cerr << "error: unknown compile option '" << option << "'\n";
      print_usage(std::cerr);
      return 1;
    }

    if (options.emit_ir && options.output_file.has_value()) {
      std::cerr << "error: --emit-ir does not support -o in Phase 4\n";
      return 1;
    }

    return run_compile(loaded_source, options);
  }

  if (argc != 3) {
    print_usage(std::cerr);
    return 1;
  }

  LoadedSource loaded_source;
  if (!load_source_file(argv[2], loaded_source)) {
    return 1;
  }

  if (command == "lex") {
    return run_lex(loaded_source);
  }
  if (command == "parse") {
    return run_parse(loaded_source);
  }
  if (command == "ast") {
    return run_ast(loaded_source);
  }
  if (command == "check") {
    return run_check(loaded_source);
  }
  if (command == "ir") {
    return run_ir(loaded_source);
  }
  if (command == "cfg") {
    return run_cfg(loaded_source);
  }
  if (command == "dom") {
    return run_dom(loaded_source);
  }

  std::cerr << "error: unknown command '" << command << "'\n";
  print_usage(std::cerr);
  return 1;
}
