#pragma once

#include <string>

#include "nexus/compiler/ir/ir.hpp"

namespace nexus::compiler::ir {

std::string print_module(const Module& module);
std::string print_function(const Function& function);
// Three-address code as a quadruple table: (op, arg1, arg2, result).
std::string print_quadruples(const Module& module);

}  // namespace nexus::compiler::ir
