#include "nexus/sim/pipeline/model.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "nexus/common/arithmetic.hpp"
#include "nexus/mips/isa/instruction.hpp"
#include "nexus/sim/io/system.hpp"

namespace nexus::sim::pipeline {

namespace {

using mips::isa::Opcode;
using mips::isa::Register;
using mips::loader::LoadedInstruction;
using mips::loader::LoadedProgram;

constexpr std::int32_t kHaltReturnAddress = std::numeric_limits<std::int32_t>::min();
constexpr std::size_t kStackGuardWords = 16;

struct MachineState {
  std::array<std::int32_t, 32> regs{};
  std::int32_t hi = 0;
  std::int32_t lo = 0;
  std::size_t pc = 0;
};

struct IFIDRegister {
  bool valid = false;
  LoadedInstruction instruction{};
  std::size_t pc = 0;
  std::size_t sequence = 0;
  bool predicted_taken = false;
  std::size_t predicted_target = 0;
  std::size_t fallthrough_pc = 0;
  std::string label;
};

struct IDEXRegister {
  bool valid = false;
  LoadedInstruction instruction{};
  std::size_t pc = 0;
  std::size_t sequence = 0;
  std::int32_t rs_value = 0;
  std::int32_t rt_value = 0;
  bool predicted_taken = false;
  std::size_t predicted_target = 0;
  std::size_t fallthrough_pc = 0;
  std::string label;
  // Set when the instruction is held in ID/EX by a memory freeze or load interlock.  While it
  // waits, older producers drain through write-back and leave the forwarding window, so the held
  // instruction must re-read the register file before it finally executes.
  bool held = false;
};

struct EXMEMRegister {
  bool valid = false;
  LoadedInstruction instruction{};
  std::size_t pc = 0;
  std::size_t sequence = 0;
  std::int32_t alu_result = 0;
  std::int32_t store_value = 0;
  bool reg_write = false;
  std::optional<Register> destination;
  bool mem_read = false;
  bool mem_write = false;
  bool write_hi_lo = false;
  std::int32_t hi_value = 0;
  std::int32_t lo_value = 0;
  bool halt = false;
  bool memory_started = false;
  std::size_t memory_remaining = 0;
  bool memory_cache_hit = false;
  std::optional<std::int32_t> load_value;
  std::string label;
};

struct MEMWBRegister {
  bool valid = false;
  LoadedInstruction instruction{};
  std::size_t pc = 0;
  std::size_t sequence = 0;
  bool reg_write = false;
  std::optional<Register> destination;
  std::int32_t write_value = 0;
  bool write_hi_lo = false;
  std::int32_t hi_value = 0;
  std::int32_t lo_value = 0;
  bool halt = false;
  std::string label;
};

struct FetchPacket {
  bool valid = false;
  LoadedInstruction instruction{};
  std::size_t pc = 0;
  std::size_t sequence = 0;
  bool predicted_taken = false;
  std::size_t predicted_target = 0;
  std::size_t fallthrough_pc = 0;
  std::string label;
};

struct TimelineRecord {
  std::string label;
  std::vector<std::string> cells;
};

struct ForwardResult {
  std::int32_t value = 0;
  bool forwarded = false;
  std::string description;
};

struct ExecuteOutcome {
  EXMEMRegister pipeline{};
  bool redirect = false;
  std::size_t redirect_pc = 0;
  bool flush_ifid = false;
  bool flush_fetch = false;
  bool interrupt_return = false;
  std::size_t branch_predictions = 0;
  std::size_t branch_mispredictions = 0;
  std::vector<std::string> events;
  std::string error;
};

std::int32_t& reg(MachineState& state, Register reg_name) {
  return state.regs[mips::isa::register_index(reg_name)];
}

std::uint32_t as_u32(std::int32_t value) { return static_cast<std::uint32_t>(value); }

std::string make_label(std::size_t sequence, std::size_t pc, const LoadedInstruction& instruction) {
  std::ostringstream label;
  label << "I" << sequence << "@pc" << pc << ':' << mips::isa::opcode_name(instruction.opcode);
  return label.str();
}

bool is_conditional_branch(const LoadedInstruction& instruction) {
  return instruction.opcode == Opcode::Beq || instruction.opcode == Opcode::Bne;
}

bool is_load(const LoadedInstruction& instruction) { return instruction.opcode == Opcode::Lw; }

bool is_memory_op(const EXMEMRegister& stage) { return stage.valid && (stage.mem_read || stage.mem_write); }

bool reads_hi(const LoadedInstruction& instruction) { return instruction.opcode == Opcode::Mfhi; }

bool reads_lo(const LoadedInstruction& instruction) { return instruction.opcode == Opcode::Mflo; }

std::optional<Register> destination_register(const LoadedInstruction& instruction) {
  switch (instruction.opcode) {
    case Opcode::Add:
    case Opcode::Addu:
    case Opcode::Sub:
    case Opcode::And:
    case Opcode::Or:
    case Opcode::Xor:
    case Opcode::Slt:
    case Opcode::Sltu:
    case Opcode::Sll:
    case Opcode::Mflo:
    case Opcode::Mfhi:
      return instruction.rd;
    case Opcode::Addiu:
    case Opcode::Ori:
    case Opcode::Xori:
    case Opcode::Sltiu:
    case Opcode::Lui:
    case Opcode::Lw:
      return instruction.rt;
    case Opcode::Jal:
      return Register::RA;
    default:
      return std::nullopt;
  }
}

bool uses_rs(const LoadedInstruction& instruction) {
  switch (instruction.opcode) {
    case Opcode::Add:
    case Opcode::Addu:
    case Opcode::Addiu:
    case Opcode::Sub:
    case Opcode::And:
    case Opcode::Or:
    case Opcode::Ori:
    case Opcode::Xor:
    case Opcode::Xori:
    case Opcode::Slt:
    case Opcode::Sltu:
    case Opcode::Sltiu:
    case Opcode::Lw:
    case Opcode::Sw:
    case Opcode::Beq:
    case Opcode::Bne:
    case Opcode::Jr:
    case Opcode::Mult:
    case Opcode::Div:
      return true;
    default:
      return false;
  }
}

bool uses_rt(const LoadedInstruction& instruction) {
  switch (instruction.opcode) {
    case Opcode::Add:
    case Opcode::Addu:
    case Opcode::Sub:
    case Opcode::And:
    case Opcode::Or:
    case Opcode::Xor:
    case Opcode::Slt:
    case Opcode::Sltu:
    case Opcode::Sll:
    case Opcode::Sw:
    case Opcode::Beq:
    case Opcode::Bne:
    case Opcode::Mult:
    case Opcode::Div:
      return true;
    default:
      return false;
  }
}

bool uses_register(const LoadedInstruction& instruction, Register reg_name) {
  if (reg_name == Register::Zero) {
    return false;
  }
  return (uses_rs(instruction) && instruction.rs == reg_name) ||
      (uses_rt(instruction) && instruction.rt == reg_name);
}

bool writes_register_stage_exmem(const EXMEMRegister& stage, Register reg_name) {
  return stage.valid && stage.reg_write && stage.destination.has_value() &&
      *stage.destination == reg_name && reg_name != Register::Zero;
}

bool writes_register_stage_memwb(const MEMWBRegister& stage, Register reg_name) {
  return stage.valid && stage.reg_write && stage.destination.has_value() &&
      *stage.destination == reg_name && reg_name != Register::Zero;
}

ForwardResult forward_register_operand(
    Register reg_name,
    std::int32_t original,
    std::string operand_name,
    const EXMEMRegister& ex_mem,
    const MEMWBRegister& mem_wb,
    RunResult& result) {
  if (reg_name == Register::Zero) {
    return ForwardResult{.value = 0, .description = ""};
  }

  if (writes_register_stage_exmem(ex_mem, reg_name) && !ex_mem.mem_read) {
    result.forwarding_events += 1;
    return ForwardResult{
        .value = ex_mem.alu_result,
        .forwarded = true,
        .description = operand_name + "<-EX/MEM(" + ex_mem.label + ")"};
  }

  if (writes_register_stage_memwb(mem_wb, reg_name)) {
    result.forwarding_events += 1;
    return ForwardResult{
        .value = mem_wb.write_value,
        .forwarded = true,
        .description = operand_name + "<-MEM/WB(" + mem_wb.label + ")"};
  }

  return ForwardResult{.value = original, .description = ""};
}

ForwardResult forward_hi_operand(
    std::int32_t original,
    const EXMEMRegister& ex_mem,
    const MEMWBRegister& mem_wb,
    RunResult& result) {
  if (ex_mem.valid && ex_mem.write_hi_lo) {
    result.forwarding_events += 1;
    return ForwardResult{
        .value = ex_mem.hi_value,
        .forwarded = true,
        .description = "HI<-EX/MEM(" + ex_mem.label + ")"};
  }

  if (mem_wb.valid && mem_wb.write_hi_lo) {
    result.forwarding_events += 1;
    return ForwardResult{
        .value = mem_wb.hi_value,
        .forwarded = true,
        .description = "HI<-MEM/WB(" + mem_wb.label + ")"};
  }

  return ForwardResult{.value = original, .description = ""};
}

ForwardResult forward_lo_operand(
    std::int32_t original,
    const EXMEMRegister& ex_mem,
    const MEMWBRegister& mem_wb,
    RunResult& result) {
  if (ex_mem.valid && ex_mem.write_hi_lo) {
    result.forwarding_events += 1;
    return ForwardResult{
        .value = ex_mem.lo_value,
        .forwarded = true,
        .description = "LO<-EX/MEM(" + ex_mem.label + ")"};
  }

  if (mem_wb.valid && mem_wb.write_hi_lo) {
    result.forwarding_events += 1;
    return ForwardResult{
        .value = mem_wb.lo_value,
        .forwarded = true,
        .description = "LO<-MEM/WB(" + mem_wb.label + ")"};
  }

  return ForwardResult{.value = original, .description = ""};
}

bool predict_taken(PredictorKind predictor, const LoadedInstruction& instruction, std::size_t pc) {
  if (!is_conditional_branch(instruction)) {
    return false;
  }

  switch (predictor) {
    case PredictorKind::StaticNotTaken:
      return false;
    case PredictorKind::StaticTaken:
      return true;
    case PredictorKind::StaticBackwardTakenForwardNotTaken:
      return instruction.target < pc;
  }

  return false;
}

FetchPacket make_fetch_packet(
    const LoadedInstruction& instruction,
    std::size_t pc,
    std::size_t sequence,
    PredictorKind predictor) {
  const bool taken = predict_taken(predictor, instruction, pc);
  return FetchPacket{
      .valid = true,
      .instruction = instruction,
      .pc = pc,
      .sequence = sequence,
      .predicted_taken = taken,
      .predicted_target = taken ? instruction.target : pc + 1,
      .fallthrough_pc = pc + 1,
      .label = make_label(sequence, pc, instruction),
  };
}

std::string describe_stage(const std::optional<std::string>& label, const std::string& suffix = "") {
  if (!label.has_value()) {
    return "-";
  }
  return *label + suffix;
}

void ensure_timeline_size(std::vector<TimelineRecord>& records, std::size_t sequence, std::string label) {
  if (records.size() <= sequence) {
    records.resize(sequence + 1);
  }
  if (records[sequence].label.empty()) {
    records[sequence].label = std::move(label);
  }
}

void note_timeline_cell(
    std::vector<TimelineRecord>& records,
    std::size_t sequence,
    std::string label,
    std::size_t cycle_index,
    std::string cell) {
  ensure_timeline_size(records, sequence, std::move(label));
  if (records[sequence].cells.size() < cycle_index) {
    records[sequence].cells.resize(cycle_index, ".");
  }
  records[sequence].cells[cycle_index - 1] = std::move(cell);
}

std::vector<std::string> build_timeline_output(
    const std::vector<TimelineRecord>& records,
    std::size_t cycles) {
  std::vector<std::string> lines;
  lines.push_back("timeline[pipeline]:");

  std::size_t label_width = std::string("instruction").size();
  for (const auto& record : records) {
    label_width = std::max(label_width, record.label.size());
  }

  std::ostringstream header;
  header << std::left << std::setw(static_cast<int>(label_width)) << "instruction";
  for (std::size_t cycle = 1; cycle <= cycles; ++cycle) {
    header << " | " << std::setw(4) << cycle;
  }
  lines.push_back(header.str());

  for (const auto& record : records) {
    if (record.label.empty()) {
      continue;
    }
    std::ostringstream row;
    row << std::left << std::setw(static_cast<int>(label_width)) << record.label;
    for (std::size_t cycle = 0; cycle < cycles; ++cycle) {
      const std::string cell = cycle < record.cells.size() ? record.cells[cycle] : ".";
      row << " | " << std::setw(4) << cell;
    }
    lines.push_back(row.str());
  }

  return lines;
}

std::string join_events(const std::vector<std::string>& events) {
  if (events.empty()) {
    return "-";
  }

  std::ostringstream output;
  for (std::size_t index = 0; index < events.size(); ++index) {
    if (index != 0) {
      output << "; ";
    }
    output << events[index];
  }
  return output.str();
}

std::string make_trace_line(
    std::size_t cycle,
    const std::optional<std::string>& if_stage,
    const std::optional<std::string>& id_stage,
    const std::optional<std::string>& ex_stage,
    const std::optional<std::string>& mem_stage,
    const std::optional<std::string>& wb_stage,
    const std::vector<std::string>& events) {
  std::ostringstream output;
  output << "trace[pipeline]: cycle=" << cycle << " IF=" << describe_stage(if_stage)
         << " ID=" << describe_stage(id_stage) << " EX=" << describe_stage(ex_stage)
         << " MEM=" << describe_stage(mem_stage) << " WB=" << describe_stage(wb_stage)
         << " events=" << join_events(events);
  return output.str();
}

IFIDRegister to_if_id(const FetchPacket& packet) {
  return IFIDRegister{
      .valid = packet.valid,
      .instruction = packet.instruction,
      .pc = packet.pc,
      .sequence = packet.sequence,
      .predicted_taken = packet.predicted_taken,
      .predicted_target = packet.predicted_target,
      .fallthrough_pc = packet.fallthrough_pc,
      .label = packet.label,
  };
}

IDEXRegister decode_if_id(const IFIDRegister& stage, const MachineState& state) {
  return IDEXRegister{
      .valid = stage.valid,
      .instruction = stage.instruction,
      .pc = stage.pc,
      .sequence = stage.sequence,
      .rs_value = stage.valid ? state.regs[mips::isa::register_index(stage.instruction.rs)] : 0,
      .rt_value = stage.valid ? state.regs[mips::isa::register_index(stage.instruction.rt)] : 0,
      .predicted_taken = stage.predicted_taken,
      .predicted_target = stage.predicted_target,
      .fallthrough_pc = stage.fallthrough_pc,
      .label = stage.label,
  };
}

ExecuteOutcome execute_stage(
    const IDEXRegister& id_ex,
    const EXMEMRegister& ex_mem,
    const MEMWBRegister& mem_wb,
    const MachineState& state,
    bool interrupt_in_service,
    RunResult& result) {
  ExecuteOutcome outcome;
  if (!id_ex.valid) {
    return outcome;
  }

  outcome.pipeline.valid = true;
  outcome.pipeline.instruction = id_ex.instruction;
  outcome.pipeline.pc = id_ex.pc;
  outcome.pipeline.sequence = id_ex.sequence;
  outcome.pipeline.label = id_ex.label;

  const auto rs =
      forward_register_operand(id_ex.instruction.rs, id_ex.rs_value, "rs", ex_mem, mem_wb, result);
  const auto rt =
      forward_register_operand(id_ex.instruction.rt, id_ex.rt_value, "rt", ex_mem, mem_wb, result);
  const auto hi = forward_hi_operand(state.hi, ex_mem, mem_wb, result);
  const auto lo = forward_lo_operand(state.lo, ex_mem, mem_wb, result);

  if (rs.forwarded) {
    outcome.events.push_back("forward(" + rs.description + ")");
  }
  if (rt.forwarded) {
    outcome.events.push_back("forward(" + rt.description + ")");
  }
  if (reads_hi(id_ex.instruction) && hi.forwarded) {
    outcome.events.push_back("forward(" + hi.description + ")");
  }
  if (reads_lo(id_ex.instruction) && lo.forwarded) {
    outcome.events.push_back("forward(" + lo.description + ")");
  }

  const std::int32_t lhs = rs.value;
  const std::int32_t rhs = rt.value;

  switch (id_ex.instruction.opcode) {
    case Opcode::Add:
    case Opcode::Addu:
      outcome.pipeline.reg_write = true;
      outcome.pipeline.destination = id_ex.instruction.rd;
      outcome.pipeline.alu_result = lhs + rhs;
      return outcome;
    case Opcode::Addiu:
      outcome.pipeline.reg_write = true;
      outcome.pipeline.destination = id_ex.instruction.rt;
      outcome.pipeline.alu_result = lhs + id_ex.instruction.immediate;
      return outcome;
    case Opcode::Sub:
      outcome.pipeline.reg_write = true;
      outcome.pipeline.destination = id_ex.instruction.rd;
      outcome.pipeline.alu_result = lhs - rhs;
      return outcome;
    case Opcode::And:
      outcome.pipeline.reg_write = true;
      outcome.pipeline.destination = id_ex.instruction.rd;
      outcome.pipeline.alu_result = lhs & rhs;
      return outcome;
    case Opcode::Or:
      outcome.pipeline.reg_write = true;
      outcome.pipeline.destination = id_ex.instruction.rd;
      outcome.pipeline.alu_result = lhs | rhs;
      return outcome;
    case Opcode::Ori:
      outcome.pipeline.reg_write = true;
      outcome.pipeline.destination = id_ex.instruction.rt;
      outcome.pipeline.alu_result = static_cast<std::int32_t>(
          as_u32(lhs) | (static_cast<std::uint32_t>(id_ex.instruction.immediate) & 0xffffU));
      return outcome;
    case Opcode::Xor:
      outcome.pipeline.reg_write = true;
      outcome.pipeline.destination = id_ex.instruction.rd;
      outcome.pipeline.alu_result = lhs ^ rhs;
      return outcome;
    case Opcode::Xori:
      outcome.pipeline.reg_write = true;
      outcome.pipeline.destination = id_ex.instruction.rt;
      outcome.pipeline.alu_result = static_cast<std::int32_t>(
          as_u32(lhs) ^ (static_cast<std::uint32_t>(id_ex.instruction.immediate) & 0xffffU));
      return outcome;
    case Opcode::Slt:
      outcome.pipeline.reg_write = true;
      outcome.pipeline.destination = id_ex.instruction.rd;
      outcome.pipeline.alu_result = lhs < rhs ? 1 : 0;
      return outcome;
    case Opcode::Sltu:
      outcome.pipeline.reg_write = true;
      outcome.pipeline.destination = id_ex.instruction.rd;
      outcome.pipeline.alu_result = as_u32(lhs) < as_u32(rhs) ? 1 : 0;
      return outcome;
    case Opcode::Sltiu:
      outcome.pipeline.reg_write = true;
      outcome.pipeline.destination = id_ex.instruction.rt;
      outcome.pipeline.alu_result = as_u32(lhs) < as_u32(id_ex.instruction.immediate) ? 1 : 0;
      return outcome;
    case Opcode::Sll:
      outcome.pipeline.reg_write = true;
      outcome.pipeline.destination = id_ex.instruction.rd;
      outcome.pipeline.alu_result = static_cast<std::int32_t>(
          static_cast<std::uint32_t>(rhs) << id_ex.instruction.immediate);
      return outcome;
    case Opcode::Lui:
      outcome.pipeline.reg_write = true;
      outcome.pipeline.destination = id_ex.instruction.rt;
      outcome.pipeline.alu_result = static_cast<std::int32_t>(
          (static_cast<std::uint32_t>(id_ex.instruction.immediate) & 0xffffU) << 16U);
      return outcome;
    case Opcode::Lw:
      outcome.pipeline.mem_read = true;
      outcome.pipeline.reg_write = true;
      outcome.pipeline.destination = id_ex.instruction.rt;
      outcome.pipeline.alu_result = lhs + id_ex.instruction.immediate;
      return outcome;
    case Opcode::Sw:
      outcome.pipeline.mem_write = true;
      outcome.pipeline.alu_result = lhs + id_ex.instruction.immediate;
      outcome.pipeline.store_value = rhs;
      return outcome;
    case Opcode::Beq:
    case Opcode::Bne: {
      const bool actual_taken =
          id_ex.instruction.opcode == Opcode::Beq ? (lhs == rhs) : (lhs != rhs);
      const std::size_t actual_target = actual_taken ? id_ex.instruction.target : id_ex.fallthrough_pc;
      outcome.branch_predictions = 1;
      if (actual_target != id_ex.predicted_target) {
        outcome.branch_mispredictions = 1;
        outcome.redirect = true;
        outcome.redirect_pc = actual_target;
        outcome.flush_ifid = true;
        outcome.flush_fetch = true;
        outcome.events.push_back("mispredict(" + std::string(mips::isa::opcode_name(id_ex.instruction.opcode)) +
                                 " -> pc=" + std::to_string(actual_target) + ")");
      } else {
        outcome.events.push_back("predict(correct)");
      }
      return outcome;
    }
    case Opcode::J:
      outcome.redirect = true;
      outcome.redirect_pc = id_ex.instruction.target;
      outcome.flush_ifid = true;
      outcome.flush_fetch = true;
      outcome.events.push_back("redirect(j -> pc=" + std::to_string(id_ex.instruction.target) + ")");
      return outcome;
    case Opcode::Jal:
      outcome.pipeline.reg_write = true;
      outcome.pipeline.destination = Register::RA;
      outcome.pipeline.alu_result = static_cast<std::int32_t>(id_ex.pc + 1);
      outcome.redirect = true;
      outcome.redirect_pc = id_ex.instruction.target;
      outcome.flush_ifid = true;
      outcome.flush_fetch = true;
      outcome.events.push_back("redirect(jal -> pc=" + std::to_string(id_ex.instruction.target) + ")");
      return outcome;
    case Opcode::Jr: {
      const std::int32_t target = lhs;
      if (id_ex.instruction.rs == Register::RA && target == kHaltReturnAddress) {
        outcome.pipeline.halt = true;
        outcome.redirect = true;
        outcome.redirect_pc = std::numeric_limits<std::size_t>::max();
        outcome.flush_ifid = true;
        outcome.flush_fetch = true;
        outcome.events.push_back("halt(jr $ra sentinel)");
        return outcome;
      }
      if (target < 0) {
        outcome.error = "jr target out of range: " + std::to_string(target);
        return outcome;
      }
      outcome.redirect = true;
      outcome.redirect_pc = static_cast<std::size_t>(target);
      outcome.flush_ifid = true;
      outcome.flush_fetch = true;
      if (interrupt_in_service && id_ex.instruction.rs == Register::RA) {
        outcome.interrupt_return = true;
        outcome.events.push_back("iret(pc=" + std::to_string(target) + ")");
      } else {
        outcome.events.push_back("redirect(jr -> pc=" + std::to_string(target) + ")");
      }
      return outcome;
    }
    case Opcode::Mult: {
      const auto product = common::multiply_signed(lhs, rhs, common::MultiplierStyle::ShiftAdd);
      outcome.pipeline.write_hi_lo = true;
      outcome.pipeline.hi_value = product.hi;
      outcome.pipeline.lo_value = product.lo;
      return outcome;
    }
    case Opcode::Div: {
      const auto division = common::divide_signed(lhs, rhs, common::DividerStyle::Restoring);
      if (division.divide_by_zero) {
        outcome.error = "division by zero";
        return outcome;
      }
      outcome.pipeline.write_hi_lo = true;
      outcome.pipeline.hi_value = division.remainder;
      outcome.pipeline.lo_value = division.quotient;
      return outcome;
    }
    case Opcode::Mflo:
      outcome.pipeline.reg_write = true;
      outcome.pipeline.destination = id_ex.instruction.rd;
      outcome.pipeline.alu_result = lo.value;
      return outcome;
    case Opcode::Mfhi:
      outcome.pipeline.reg_write = true;
      outcome.pipeline.destination = id_ex.instruction.rd;
      outcome.pipeline.alu_result = hi.value;
      return outcome;
  }

  return outcome;
}

std::string load_use_event(Register reg_name) {
  std::ostringstream output;
  output << "stall(load-use on " << mips::isa::register_name(reg_name) << ")";
  return output.str();
}

std::string memory_stall_event(const EXMEMRegister& stage) {
  std::ostringstream output;
  output << "stall(memory " << (stage.memory_cache_hit ? "hit" : "miss") << " on " << stage.label << ")";
  return output.str();
}

}  // namespace

std::string_view predictor_name(PredictorKind predictor) {
  switch (predictor) {
    case PredictorKind::StaticNotTaken:
      return "static-not-taken";
    case PredictorKind::StaticTaken:
      return "static-taken";
    case PredictorKind::StaticBackwardTakenForwardNotTaken:
      return "static-btfnt";
  }
  return "unknown";
}

RunResult run_program(const LoadedProgram& program, const RunOptions& options) {
  RunResult result;
  result.cache_mode = options.cache_mode;

  MachineState state;
  state.pc = program.entry_point;
  const std::size_t stack_top_words =
      options.memory_words > kStackGuardWords ? options.memory_words - kStackGuardWords : options.memory_words;
  reg(state, Register::SP) = static_cast<std::int32_t>(stack_top_words * sizeof(std::int32_t));
  reg(state, Register::RA) = kHaltReturnAddress;

  memory::System memory_system({
      .memory_words = options.memory_words,
      .flat_latency = options.memory_latency,
      .cache =
          {
              .mode = options.cache_mode,
              .sets = options.cache_sets,
              .line_words = options.cache_line_words,
              .ways = options.cache_ways,
              .hit_latency = options.cache_hit_latency,
              .miss_penalty = options.cache_miss_penalty,
          },
      .l2_cache = options.l2_cache_mode == memory::CacheMode::Off
          ? std::optional<memory::CacheConfig>{}
          : std::optional<memory::CacheConfig>(memory::CacheConfig{
                .mode = options.l2_cache_mode,
                .sets = options.l2_cache_sets,
                .line_words = options.l2_cache_line_words,
                .ways = options.l2_cache_ways,
                .hit_latency = options.l2_cache_hit_latency,
                .miss_penalty = options.l2_cache_miss_penalty,
            }),
      .io =
          {
              .enable_console = options.io_demo,
              .enable_timer = options.interrupt_demo,
              .enable_dma = options.dma_demo,
          },
  });

  std::optional<std::size_t> interrupt_handler_pc;
  if (options.interrupt_demo) {
    const auto found = program.labels.find("interrupt_handler");
    if (found == program.labels.end()) {
      result.error =
          "interrupt demo requires an 'interrupt_handler' label in the loaded program";
      result.registers = state.regs;
      return result;
    }
    interrupt_handler_pc = found->second;
  }

  IFIDRegister if_id;
  IDEXRegister id_ex;
  EXMEMRegister ex_mem;
  MEMWBRegister mem_wb;
  std::vector<TimelineRecord> timeline_records;
  std::size_t next_sequence = 0;

  bool interrupt_in_service = false;
  bool interrupt_pending_dispatch = false;
  std::optional<std::int32_t> interrupt_saved_ra;

  auto append_system_line = [&result](const std::string& line) {
    result.system_lines.push_back(line);
  };

  while (true) {
    const bool pipeline_empty = !if_id.valid && !id_ex.valid && !ex_mem.valid && !mem_wb.valid;
    if (pipeline_empty && state.pc >= program.instructions.size()) {
      result.error = result.success ? "" : "program counter ran past loaded instructions";
      break;
    }

    if (result.cycles >= options.max_cycles) {
      result.error = "pipeline cycle limit exceeded";
      break;
    }

    result.cycles += 1;
    const std::size_t cycle = result.cycles;
    std::vector<std::string> events;

    if (mem_wb.valid) {
      if (mem_wb.write_hi_lo) {
        state.hi = mem_wb.hi_value;
        state.lo = mem_wb.lo_value;
      }
      if (mem_wb.reg_write && mem_wb.destination.has_value() && *mem_wb.destination != Register::Zero) {
        reg(state, *mem_wb.destination) = mem_wb.write_value;
      }
      state.regs[0] = 0;
      result.retired_instructions += 1;
      if (mem_wb.halt) {
        result.success = true;
        result.exit_code = reg(state, Register::V0);
      }
    }

    if (id_ex.valid && id_ex.held) {
      id_ex.rs_value = state.regs[mips::isa::register_index(id_ex.instruction.rs)];
      id_ex.rt_value = state.regs[mips::isa::register_index(id_ex.instruction.rt)];
      id_ex.held = false;
    }

    const auto tick = memory_system.tick(cycle);
    if (!tick.error.empty()) {
      result.error = tick.error;
      break;
    }
    if (tick.interrupt_raised) {
      interrupt_pending_dispatch = true;
    }
    result.dma_words_copied += tick.dma_words_copied;
    for (const auto& event : tick.events) {
      events.push_back(event);
      append_system_line(event);
    }

    if (options.interrupt_demo && !interrupt_in_service && memory_system.io_system().has_pending_interrupt()) {
      interrupt_pending_dispatch = true;
    }

    const bool drain_for_interrupt =
        interrupt_pending_dispatch && !interrupt_in_service && interrupt_handler_pc.has_value();
    if (drain_for_interrupt && !if_id.valid && !id_ex.valid && !ex_mem.valid) {
      const auto source = memory_system.io_system().pending_interrupt();
      memory_system.io_system().acknowledge_interrupt();
      interrupt_pending_dispatch = false;
      interrupt_in_service = true;
      const std::int32_t return_pc = static_cast<std::int32_t>(state.pc);
      interrupt_saved_ra = reg(state, Register::RA);
      reg(state, Register::RA) = return_pc;
      state.pc = *interrupt_handler_pc;
      result.interrupts_handled += 1;
      const std::string line =
          "interrupt[" + std::string(io::interrupt_source_name(source)) + "]: dispatch -> pc=" +
          std::to_string(*interrupt_handler_pc) + " return=" + std::to_string(return_pc);
      events.push_back(line);
      append_system_line(line);
    }

    bool stall = false;
    std::optional<Register> stalled_on;
    bool memory_freeze = false;
    bool completed_load_interlock = false;
    bool suppress_fetch = drain_for_interrupt;

    MEMWBRegister next_mem_wb;
    EXMEMRegister next_ex_mem = ex_mem;

    if (is_memory_op(ex_mem)) {
      if (!next_ex_mem.memory_started) {
        const auto access = ex_mem.mem_read
            ? memory_system.read_word(ex_mem.alu_result, cycle)
            : memory_system.write_word(ex_mem.alu_result, ex_mem.store_value, cycle);
        if (!access.success) {
          result.error = access.error;
          break;
        }

        next_ex_mem.memory_started = true;
        next_ex_mem.memory_cache_hit = access.cache_hit;
        next_ex_mem.memory_remaining = access.latency_cycles > 0 ? access.latency_cycles - 1 : 0;
        if (access.from_io) {
          next_ex_mem.memory_cache_hit = false;
        }
        if (ex_mem.mem_read) {
          next_ex_mem.load_value = access.value;
        }
        for (const auto& event : access.events) {
          events.push_back(event);
          append_system_line(event);
        }

        if (access.latency_cycles > 1) {
          memory_freeze = true;
          suppress_fetch = true;
          result.stall_cycles += 1;
          events.push_back(memory_stall_event(next_ex_mem));
        }
      } else if (next_ex_mem.memory_remaining > 0) {
        --next_ex_mem.memory_remaining;
        memory_freeze = true;
        suppress_fetch = true;
        result.stall_cycles += 1;
        events.push_back(memory_stall_event(next_ex_mem));
      }

      if (next_ex_mem.memory_started && next_ex_mem.memory_remaining == 0) {
        next_mem_wb.valid = true;
        next_mem_wb.instruction = next_ex_mem.instruction;
        next_mem_wb.pc = next_ex_mem.pc;
        next_mem_wb.sequence = next_ex_mem.sequence;
        next_mem_wb.reg_write = next_ex_mem.reg_write;
        next_mem_wb.destination = next_ex_mem.destination;
        next_mem_wb.write_value = next_ex_mem.mem_read ? *next_ex_mem.load_value : next_ex_mem.alu_result;
        next_mem_wb.write_hi_lo = next_ex_mem.write_hi_lo;
        next_mem_wb.hi_value = next_ex_mem.hi_value;
        next_mem_wb.lo_value = next_ex_mem.lo_value;
        next_mem_wb.halt = next_ex_mem.halt;
        next_mem_wb.label = next_ex_mem.label;
        next_ex_mem = EXMEMRegister{};
      }
    } else if (ex_mem.valid) {
      next_mem_wb.valid = true;
      next_mem_wb.instruction = ex_mem.instruction;
      next_mem_wb.pc = ex_mem.pc;
      next_mem_wb.sequence = ex_mem.sequence;
      next_mem_wb.reg_write = ex_mem.reg_write;
      next_mem_wb.destination = ex_mem.destination;
      next_mem_wb.write_value = ex_mem.alu_result;
      next_mem_wb.write_hi_lo = ex_mem.write_hi_lo;
      next_mem_wb.hi_value = ex_mem.hi_value;
      next_mem_wb.lo_value = ex_mem.lo_value;
      next_mem_wb.halt = ex_mem.halt;
      next_mem_wb.label = ex_mem.label;
      next_ex_mem = EXMEMRegister{};
    }

    if (!memory_freeze && next_mem_wb.valid && ex_mem.valid && ex_mem.mem_read && next_mem_wb.destination.has_value() &&
        *next_mem_wb.destination != Register::Zero && id_ex.valid &&
        uses_register(id_ex.instruction, *next_mem_wb.destination)) {
      completed_load_interlock = true;
      suppress_fetch = true;
      result.stall_cycles += 1;
      events.push_back(load_use_event(*next_mem_wb.destination));
    }

    // The load-use interlock is only evaluated when the back end is not frozen by a multi-cycle
    // memory access.  Otherwise the stall path below would overwrite the frozen EX/MEM register
    // (the access in flight would be lost, e.g. a store silently dropped under cache misses).
    if (!memory_freeze && !completed_load_interlock && id_ex.valid && is_load(id_ex.instruction)) {
      const auto load_dest = destination_register(id_ex.instruction);
      if (load_dest.has_value() && *load_dest != Register::Zero && if_id.valid &&
          uses_register(if_id.instruction, *load_dest)) {
        stall = true;
        stalled_on = *load_dest;
        result.stall_cycles += 1;
        result.load_use_stalls += 1;
      }
    }

    ExecuteOutcome execute;
    if (!memory_freeze && !completed_load_interlock) {
      execute = execute_stage(id_ex, ex_mem, mem_wb, state, interrupt_in_service, result);
      if (!execute.error.empty()) {
        result.error = execute.error;
        break;
      }
      result.branch_predictions += execute.branch_predictions;
      result.branch_mispredictions += execute.branch_mispredictions;
      for (const auto& event : execute.events) {
        events.push_back(event);
      }
    }

    std::optional<FetchPacket> fetched;
    std::size_t sequential_next_pc = state.pc;
    if (!stall && !memory_freeze && !completed_load_interlock && !suppress_fetch &&
        state.pc < program.instructions.size()) {
      fetched = make_fetch_packet(program.instructions[state.pc], state.pc, next_sequence++, options.predictor);
      sequential_next_pc = fetched->predicted_target;
    }

    IDEXRegister next_id_ex;
    if (!stall && !memory_freeze && !completed_load_interlock && if_id.valid) {
      next_id_ex = decode_if_id(if_id, state);
    } else if (memory_freeze || completed_load_interlock) {
      next_id_ex = id_ex;
      next_id_ex.held = next_id_ex.valid;
    }

    IFIDRegister next_if_id;
    std::size_t next_pc = state.pc;

    std::optional<std::string> if_stage;
    std::optional<std::string> id_stage;
    std::optional<std::string> ex_stage;
    std::optional<std::string> mem_stage;
    std::optional<std::string> wb_stage;

    const bool execute_redirect = !memory_freeze && execute.redirect;
    const bool fetch_is_correct_redirect =
        execute_redirect && fetched.has_value() &&
        execute.redirect_pc != std::numeric_limits<std::size_t>::max() && fetched->pc == execute.redirect_pc;
    const bool flush_ifid_now = execute_redirect && execute.flush_ifid && if_id.valid;
    const bool flush_fetch_now =
        execute_redirect && execute.flush_fetch && fetched.has_value() && !fetch_is_correct_redirect;

    if (fetched.has_value()) {
      if_stage = fetched->label;
      note_timeline_cell(
          timeline_records,
          fetched->sequence,
          fetched->label,
          cycle,
          flush_fetch_now ? "IF!" : "IF");
    }

    if (if_id.valid) {
      id_stage = if_id.label;
      note_timeline_cell(
          timeline_records,
          if_id.sequence,
          if_id.label,
          cycle,
          memory_freeze ? "ID*" : (flush_ifid_now ? "ID!" : (stall ? "ID*" : "ID")));
    }

    if (id_ex.valid) {
      ex_stage = id_ex.label;
      note_timeline_cell(
          timeline_records,
          id_ex.sequence,
          id_ex.label,
          cycle,
          (memory_freeze || completed_load_interlock) ? "EX*" : "EX");
    }

    if (ex_mem.valid) {
      mem_stage = ex_mem.label;
      note_timeline_cell(
          timeline_records,
          ex_mem.sequence,
          ex_mem.label,
          cycle,
          is_memory_op(ex_mem) && (ex_mem.memory_started || memory_freeze) ? "MEM*" : "MEM");
    }

    if (mem_wb.valid) {
      wb_stage = mem_wb.label;
      note_timeline_cell(timeline_records, mem_wb.sequence, mem_wb.label, cycle, "WB");
    }

    if (stall && stalled_on.has_value()) {
      events.push_back(load_use_event(*stalled_on));
      next_if_id = if_id;
      next_pc = state.pc;
      next_ex_mem = execute.pipeline;
    } else if (memory_freeze || completed_load_interlock) {
      next_if_id = if_id;
      next_pc = state.pc;
    } else if (execute_redirect) {
      std::size_t flushed = 0;
      if (flush_ifid_now) {
        flushed += 1;
        events.push_back("flush(ID:" + if_id.label + ")");
      }
      if (flush_fetch_now) {
        flushed += 1;
        events.push_back("flush(IF:" + fetched->label + ")");
      }
      result.flushes += flushed;
      next_id_ex = IDEXRegister{};
      next_ex_mem = execute.pipeline;
      if (execute.interrupt_return) {
        interrupt_in_service = false;
        if (interrupt_saved_ra.has_value()) {
          reg(state, Register::RA) = *interrupt_saved_ra;
          interrupt_saved_ra.reset();
        }
        append_system_line("interrupt:return -> pc=" + std::to_string(execute.redirect_pc));
      }
      if (execute.redirect_pc == std::numeric_limits<std::size_t>::max()) {
        next_if_id = IFIDRegister{};
        next_pc = program.instructions.size();
      } else if (fetch_is_correct_redirect) {
        next_if_id = to_if_id(*fetched);
        next_pc = fetched->predicted_target;
      } else {
        next_if_id = IFIDRegister{};
        next_pc = execute.redirect_pc;
      }
    } else {
      next_if_id = fetched.has_value() ? to_if_id(*fetched) : IFIDRegister{};
      next_ex_mem = execute.pipeline;
      next_pc = fetched.has_value() ? sequential_next_pc : state.pc;
    }

    if (options.trace) {
      result.trace_lines.push_back(
          make_trace_line(cycle, if_stage, id_stage, ex_stage, mem_stage, wb_stage, events));
    }

    mem_wb = next_mem_wb;
    ex_mem = next_ex_mem;
    id_ex = next_id_ex;
    if_id = next_if_id;
    state.pc = next_pc;
    state.regs[0] = 0;

    if (result.success && !if_id.valid && !id_ex.valid && !ex_mem.valid && !mem_wb.valid) {
      break;
    }
  }

  if (options.timeline) {
    result.timeline_lines = build_timeline_output(timeline_records, result.cycles);
  }

  for (const auto& line : memory_system.io_system().take_output_lines()) {
    result.system_lines.push_back(line);
  }

  const auto& stats = memory_system.statistics();
  result.memory_accesses = stats.accesses;
  result.memory_reads = stats.reads;
  result.memory_writes = stats.writes;
  result.cache_hits = stats.cache_hits;
  result.cache_misses = stats.cache_misses;
  result.l1_hits = stats.l1_hits;
  result.l1_misses = stats.l1_misses;
  result.l2_hits = stats.l2_hits;
  result.l2_misses = stats.l2_misses;
  result.io_reads = stats.io_reads;
  result.io_writes = stats.io_writes;
  result.dma_words_copied = std::max(result.dma_words_copied, stats.dma_words);
  result.registers = state.regs;
  return result;
}

}  // namespace nexus::sim::pipeline
