#pragma once

#include <string>

#include "nexus/compiler/ir/ir.hpp"

namespace nexus::compiler::ir {

std::string print_module(const Module& module);
std::string print_function(const Function& function);

}  // namespace nexus::compiler::ir
