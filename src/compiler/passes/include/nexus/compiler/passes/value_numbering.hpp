#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "nexus/compiler/ir/ir.hpp"

namespace nexus::compiler::passes {

// Local value numbering (NEY709 (f), expression optimisation): inside every basic block, two computations with
// the same operator and the same operand *values* get the same value number, and the second one is removed; its
// uses read the first result instead.  Commutative operators are normalised (a+b = b+a).  Loads are numbered by
// the local and its current version: a store bumps the version (and the stored value becomes the value of the next
// load: store-to-load forwarding); a call bumps the versions of the arrays it receives by reference.
struct ValueNumberingResult {
  ir::Module module;
  std::size_t removed = 0;          // instructions removed in total
  std::size_t forwarded_loads = 0;  // of which loads replaced by the value just stored
  std::vector<std::string> notes;
};

ValueNumberingResult number_values(const ir::Module& module);

}  // namespace nexus::compiler::passes
