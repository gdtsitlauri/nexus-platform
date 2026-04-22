#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "nexus/compiler/ir/ir.hpp"

namespace nexus::compiler::analysis {

struct ConstantValue {
  ir::Type type{};
  bool known = false;
  std::int64_t int_value = 0;
  bool bool_value = false;
};

std::string constant_to_string(const ConstantValue& value);

struct FunctionSummary {
  std::string name;
  bool pure = false;
  bool tiny_candidate = false;
  bool writes_memory = false;
  bool calls_other_functions = false;
  std::optional<ConstantValue> constant_return;
  std::string return_summary;
};

struct InterproceduralResult {
  std::vector<FunctionSummary> summaries;
};

InterproceduralResult analyze_interprocedural(const ir::Module& module);
std::string print_interprocedural(const ir::Module& module, const InterproceduralResult& result);
std::optional<ConstantValue> evaluate_function_with_constants(
    const ir::Function& function,
    const std::vector<ConstantValue>& arguments);

}  // namespace nexus::compiler::analysis
