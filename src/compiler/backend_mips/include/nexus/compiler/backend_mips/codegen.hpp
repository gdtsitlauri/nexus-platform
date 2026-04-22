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

CodegenResult lower_module(const ir::Module& module);

}  // namespace nexus::compiler::backend_mips
