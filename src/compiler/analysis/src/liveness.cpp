#include "nexus/compiler/analysis/liveness.hpp"

#include <sstream>

namespace nexus::compiler::analysis {

namespace {

std::pair<DataFlowSet, DataFlowSet> build_use_def_sets(const ir::BasicBlock& block) {
  DataFlowSet uses;
  DataFlowSet defs;

  auto note_use = [&uses, &defs](DataFlowEntityId value) {
    if (!defs.contains(value)) {
      uses.insert(value);
    }
  };

  auto note_def = [&defs](DataFlowEntityId value) { defs.insert(value); };

  for (const ir::Instruction& instruction : block.instructions) {
    switch (instruction.kind) {
      case ir::InstructionKind::LoadLocal:
        note_use(instruction.local);
        break;
      case ir::InstructionKind::StoreLocal:
        note_def(instruction.local);
        break;
      case ir::InstructionKind::LoadElement:
        note_use(instruction.local);
        break;
      case ir::InstructionKind::StoreElement:
        note_use(instruction.local);
        break;
      case ir::InstructionKind::Call:
        for (const ir::CallArgument& argument : instruction.call_arguments) {
          if (argument.kind == ir::CallArgumentKind::LocalRef) {
            note_use(argument.local);
          }
        }
        break;
      case ir::InstructionKind::ConstInt:
      case ir::InstructionKind::ConstBool:
      case ir::InstructionKind::Unary:
      case ir::InstructionKind::Binary:
        break;
    }
  }

  return {uses, defs};
}

std::string format_local_set(const ir::Function& function, const DataFlowSet& values) {
  if (values.empty()) {
    return "{}";
  }

  std::ostringstream output;
  output << '{';
  std::size_t index = 0;
  for (DataFlowEntityId value : values) {
    if (index != 0) {
      output << ", ";
    }
    output << ir::local_info(function, value).name << ": "
           << ir::type_to_string(ir::local_info(function, value).type);
    ++index;
  }
  output << '}';
  return output.str();
}

}  // namespace

LivenessResult analyze_liveness(const ir::Function& function, const ControlFlowGraph& cfg) {
  LivenessResult result;
  result.use_sets.resize(function.blocks.size());
  result.def_sets.resize(function.blocks.size());

  DataFlowProblem problem;
  problem.direction = DataFlowDirection::Backward;
  problem.boundary_value = {};
  problem.initial_value = {};
  problem.gen_sets.resize(function.blocks.size());
  problem.kill_sets.resize(function.blocks.size());

  for (const ir::BasicBlock& block : function.blocks) {
    auto [uses, defs] = build_use_def_sets(block);
    result.use_sets[block.id] = uses;
    result.def_sets[block.id] = defs;
    problem.gen_sets[block.id] = std::move(uses);
    problem.kill_sets[block.id] = std::move(defs);
  }

  result.flow = solve_data_flow(cfg, problem);
  return result;
}

std::string print_liveness(
    const ir::Function& function,
    const ControlFlowGraph& cfg,
    const LivenessResult& liveness) {
  std::ostringstream output;
  output << "func " << function.name << " liveness:\n";
  output << "  iterations: " << liveness.flow.iterations << '\n';
  for (ir::BlockId block = 0; block < cfg.successors.size(); ++block) {
    output << "  bb" << block << '.' << function.blocks[block].label << ":\n";
    output << "    use: " << format_local_set(function, liveness.use_sets[block]) << '\n';
    output << "    def: " << format_local_set(function, liveness.def_sets[block]) << '\n';
    output << "    in:  " << format_local_set(function, liveness.flow.in_sets[block]) << '\n';
    output << "    out: " << format_local_set(function, liveness.flow.out_sets[block]) << '\n';
  }
  return output.str();
}

}  // namespace nexus::compiler::analysis
