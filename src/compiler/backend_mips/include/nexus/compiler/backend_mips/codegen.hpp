#pragma once

#include <optional>
#include <string>
#include <vector>

#include "nexus/compiler/ir/ir.hpp"
#include "nexus/mips/assembler_support/assembly.hpp"

namespace nexus::compiler::backend_mips {

struct CodegenResult {
  std::optional<mips::assembler_support::TextProgram> program;
  std::vector<std::string> diagnostics;
};

enum class RegisterAllocationMode {
  // Every IR value and local lives in a stack slot (the original, easy-to-read lowering).
  StackOnly,
  // Linear-scan allocation of values and scalar locals to $t4-$t9 / $s0-$s7.
  LinearScan,
};

struct CodegenOptions {
  RegisterAllocationMode register_allocation = RegisterAllocationMode::StackOnly;
};

CodegenResult lower_module(const ir::Module& module, const CodegenOptions& options = {});

}  // namespace nexus::compiler::backend_mips
