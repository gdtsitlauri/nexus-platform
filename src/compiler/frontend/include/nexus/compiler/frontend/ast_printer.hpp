#pragma once

#include <string>

#include "nexus/compiler/frontend/ast.hpp"

namespace nexus::compiler::frontend {

std::string print_program(const Program& program);

}  // namespace nexus::compiler::frontend
