#include "nexus/sim/advanced/model.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "nexus/mips/isa/instruction.hpp"

namespace nexus::sim::advanced {

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
  std::vector<std::int32_t> memory;
  std::size_t pc = 0;
};

struct DynamicInstruction {
  std::size_t sequence = 0;
  std::size_t pc = 0;
  LoadedInstruction instruction{};
  std::size_t next_pc = 0;
  bool conditional_branch = false;
  bool actual_taken = false;
  std::string label;
};

struct Packet {
  std::vector<const DynamicInstruction*> instructions;
};

struct Usage {
  std::vector<Register> reads;
  std::vector<Register> writes;
  bool reads_hi = false;
  bool reads_lo = false;
  bool writes_hi = false;
  bool writes_lo = false;
};

std::int32_t& reg(MachineState& state, Register reg_name) {
  return state.regs[mips::isa::register_index(reg_name)];
}

std::uint32_t as_u32(std::int32_t value) { return static_cast<std::uint32_t>(value); }

std::int32_t& checked_word_at(MachineState& state, std::int32_t address, std::string& error) {
  static std::int32_t dummy = 0;
  if (address < 0 || (address % 4) != 0) {
    error = "invalid word address " + std::to_string(address);
    return dummy;
  }

  const std::size_t index = static_cast<std::size_t>(address / 4);
  if (index >= state.memory.size()) {
    error = "memory access out of bounds at address " + std::to_string(address);
    return dummy;
  }

  return state.memory[index];
}

std::string instruction_label(std::size_t sequence, std::size_t pc, const LoadedInstruction& instruction) {
  std::ostringstream label;
  label << "I" << sequence << "@pc" << pc << ':' << mips::isa::opcode_name(instruction.opcode);
  return label.str();
}

bool is_conditional_branch(const LoadedInstruction& instruction) {
  return instruction.opcode == Opcode::Beq || instruction.opcode == Opcode::Bne;
}

bool is_control(const LoadedInstruction& instruction) {
  switch (instruction.opcode) {
    case Opcode::Beq:
    case Opcode::Bne:
    case Opcode::J:
    case Opcode::Jal:
    case Opcode::Jr:
      return true;
    default:
      return false;
  }
}

bool is_memory(const LoadedInstruction& instruction) {
  return instruction.opcode == Opcode::Lw || instruction.opcode == Opcode::Sw;
}

bool is_hi_lo_op(const LoadedInstruction& instruction) {
  switch (instruction.opcode) {
    case Opcode::Mult:
    case Opcode::Div:
    case Opcode::Mflo:
    case Opcode::Mfhi:
      return true;
    default:
      return false;
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

Usage usage_for(const LoadedInstruction& instruction) {
  Usage usage;
  if (uses_rs(instruction) && instruction.rs != Register::Zero) {
    usage.reads.push_back(instruction.rs);
  }
  if (uses_rt(instruction) && instruction.rt != Register::Zero) {
    usage.reads.push_back(instruction.rt);
  }

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
      if (instruction.rd != Register::Zero) {
        usage.writes.push_back(instruction.rd);
      }
      break;
    case Opcode::Addiu:
    case Opcode::Ori:
    case Opcode::Xori:
    case Opcode::Sltiu:
    case Opcode::Lui:
    case Opcode::Lw:
      if (instruction.rt != Register::Zero) {
        usage.writes.push_back(instruction.rt);
      }
      break;
    case Opcode::Jal:
      usage.writes.push_back(Register::RA);
      break;
    default:
      break;
  }

  if (instruction.opcode == Opcode::Mflo) {
    usage.reads_lo = true;
  }
  if (instruction.opcode == Opcode::Mfhi) {
    usage.reads_hi = true;
  }
  if (instruction.opcode == Opcode::Mult || instruction.opcode == Opcode::Div) {
    usage.writes_hi = true;
    usage.writes_lo = true;
  }

  return usage;
}

bool contains_register(const std::vector<Register>& regs, Register reg_name) {
  return std::find(regs.begin(), regs.end(), reg_name) != regs.end();
}

bool has_pair_hazard(const LoadedInstruction& first, const LoadedInstruction& second) {
  if (is_control(first) || is_control(second)) {
    return true;
  }

  if (is_memory(first) && is_memory(second)) {
    return true;
  }

  if (is_hi_lo_op(first) || is_hi_lo_op(second)) {
    return true;
  }

  const Usage first_usage = usage_for(first);
  const Usage second_usage = usage_for(second);

  for (const auto written : first_usage.writes) {
    if (contains_register(second_usage.reads, written) || contains_register(second_usage.writes, written)) {
      return true;
    }
  }

  for (const auto written : second_usage.writes) {
    if (contains_register(first_usage.reads, written) || contains_register(first_usage.writes, written)) {
      return true;
    }
  }

  if ((first_usage.writes_hi || first_usage.writes_lo || first_usage.reads_hi || first_usage.reads_lo) &&
      (second_usage.writes_hi || second_usage.writes_lo || second_usage.reads_hi || second_usage.reads_lo)) {
    return true;
  }

  return false;
}

bool has_reorder_dependency(const LoadedInstruction& earlier, const LoadedInstruction& later) {
  if (is_control(earlier) || is_control(later)) {
    return true;
  }
  if (is_memory(earlier) && is_memory(later)) {
    return true;
  }
  if (is_hi_lo_op(earlier) || is_hi_lo_op(later)) {
    return true;
  }

  const Usage earlier_usage = usage_for(earlier);
  const Usage later_usage = usage_for(later);

  for (const auto written : earlier_usage.writes) {
    if (contains_register(later_usage.reads, written) || contains_register(later_usage.writes, written)) {
      return true;
    }
  }

  for (const auto written : later_usage.writes) {
    if (contains_register(earlier_usage.reads, written)) {
      return true;
    }
  }

  if ((earlier_usage.writes_hi || earlier_usage.writes_lo) &&
      (later_usage.reads_hi || later_usage.reads_lo || later_usage.writes_hi || later_usage.writes_lo)) {
    return true;
  }

  if ((later_usage.writes_hi || later_usage.writes_lo) &&
      (earlier_usage.reads_hi || earlier_usage.reads_lo)) {
    return true;
  }

  return false;
}

bool predict_branch(PredictorKind predictor, const TwoBitPredictor& two_bit, std::size_t pc) {
  switch (predictor) {
    case PredictorKind::StaticNotTaken:
      return false;
    case PredictorKind::TwoBit:
      return two_bit.predict(pc);
  }

  return false;
}

void update_predictor(PredictorKind predictor, TwoBitPredictor& two_bit, std::size_t pc, bool taken) {
  if (predictor == PredictorKind::TwoBit) {
    two_bit.update(pc, taken);
  }
}

std::string packet_text(const Packet& packet, std::size_t issue_width, bool fill_nops) {
  std::ostringstream output;
  output << '[';
  for (std::size_t index = 0; index < packet.instructions.size(); ++index) {
    if (index > 0) {
      output << ", ";
    }
    output << packet.instructions[index]->label;
  }
  if (fill_nops) {
    for (std::size_t index = packet.instructions.size(); index < issue_width; ++index) {
      if (index > 0) {
        output << ", ";
      }
      output << "nop";
    }
  }
  output << ']';
  return output.str();
}

std::string trace_line(
    std::size_t cycle,
    SchedulerKind scheduler,
    std::size_t issue_width,
    const Packet& packet,
    const std::vector<std::string>& events,
    bool fill_nops) {
  std::ostringstream output;
  output << "trace[advanced]: cycle=" << cycle << " scheduler=" << scheduler_name(scheduler)
         << " issue_width=" << issue_width << " packet=" << packet_text(packet, issue_width, fill_nops)
         << " events=";
  if (events.empty()) {
    output << '-';
  } else {
    for (std::size_t index = 0; index < events.size(); ++index) {
      if (index > 0) {
        output << ", ";
      }
      output << events[index];
    }
  }
  return output.str();
}

std::vector<DynamicInstruction> execute_trace(
    const LoadedProgram& program,
    const RunOptions& options,
    RunResult& result) {
  std::vector<DynamicInstruction> trace;
  MachineState state;
  state.memory.assign(options.memory_words, 0);
  state.pc = program.entry_point;
  const std::size_t stack_top_words =
      state.memory.size() > kStackGuardWords ? state.memory.size() - kStackGuardWords : state.memory.size();
  reg(state, Register::SP) = static_cast<std::int32_t>(stack_top_words * sizeof(std::int32_t));
  reg(state, Register::RA) = kHaltReturnAddress;

  while (state.pc < program.instructions.size()) {
    if (trace.size() >= options.max_instructions) {
      result.error = "advanced sandbox instruction limit exceeded";
      result.registers = state.regs;
      return {};
    }

    const LoadedInstruction& instruction = program.instructions[state.pc];
    DynamicInstruction dynamic{
        .sequence = trace.size(),
        .pc = state.pc,
        .instruction = instruction,
        .next_pc = state.pc + 1,
        .conditional_branch = is_conditional_branch(instruction),
        .actual_taken = false,
        .label = instruction_label(trace.size(), state.pc, instruction),
    };

    std::string error;

    switch (instruction.opcode) {
      case Opcode::Add:
      case Opcode::Addu:
        reg(state, instruction.rd) = reg(state, instruction.rs) + reg(state, instruction.rt);
        break;
      case Opcode::Addiu:
        reg(state, instruction.rt) = reg(state, instruction.rs) + instruction.immediate;
        break;
      case Opcode::Sub:
        reg(state, instruction.rd) = reg(state, instruction.rs) - reg(state, instruction.rt);
        break;
      case Opcode::And:
        reg(state, instruction.rd) = reg(state, instruction.rs) & reg(state, instruction.rt);
        break;
      case Opcode::Or:
        reg(state, instruction.rd) = reg(state, instruction.rs) | reg(state, instruction.rt);
        break;
      case Opcode::Ori:
        reg(state, instruction.rt) =
            static_cast<std::int32_t>(as_u32(reg(state, instruction.rs)) |
                                      (static_cast<std::uint32_t>(instruction.immediate) & 0xffffU));
        break;
      case Opcode::Xor:
        reg(state, instruction.rd) = reg(state, instruction.rs) ^ reg(state, instruction.rt);
        break;
      case Opcode::Xori:
        reg(state, instruction.rt) =
            static_cast<std::int32_t>(as_u32(reg(state, instruction.rs)) ^
                                      (static_cast<std::uint32_t>(instruction.immediate) & 0xffffU));
        break;
      case Opcode::Slt:
        reg(state, instruction.rd) = reg(state, instruction.rs) < reg(state, instruction.rt) ? 1 : 0;
        break;
      case Opcode::Sltu:
        reg(state, instruction.rd) =
            as_u32(reg(state, instruction.rs)) < as_u32(reg(state, instruction.rt)) ? 1 : 0;
        break;
      case Opcode::Sltiu:
        reg(state, instruction.rt) =
            as_u32(reg(state, instruction.rs)) < as_u32(instruction.immediate) ? 1 : 0;
        break;
      case Opcode::Sll:
        reg(state, instruction.rd) =
            static_cast<std::int32_t>(as_u32(reg(state, instruction.rt)) << instruction.immediate);
        break;
      case Opcode::Lui:
        reg(state, instruction.rt) =
            static_cast<std::int32_t>((static_cast<std::uint32_t>(instruction.immediate) & 0xffffU) << 16U);
        break;
      case Opcode::Lw: {
        const std::int32_t address = reg(state, instruction.rs) + instruction.immediate;
        reg(state, instruction.rt) = checked_word_at(state, address, error);
        if (!error.empty()) {
          result.error = error;
          result.registers = state.regs;
          return {};
        }
        break;
      }
      case Opcode::Sw: {
        const std::int32_t address = reg(state, instruction.rs) + instruction.immediate;
        checked_word_at(state, address, error) = reg(state, instruction.rt);
        if (!error.empty()) {
          result.error = error;
          result.registers = state.regs;
          return {};
        }
        break;
      }
      case Opcode::Beq:
        if (reg(state, instruction.rs) == reg(state, instruction.rt)) {
          dynamic.next_pc = instruction.target;
          dynamic.actual_taken = true;
        }
        break;
      case Opcode::Bne:
        if (reg(state, instruction.rs) != reg(state, instruction.rt)) {
          dynamic.next_pc = instruction.target;
          dynamic.actual_taken = true;
        }
        break;
      case Opcode::J:
        dynamic.next_pc = instruction.target;
        break;
      case Opcode::Jal:
        reg(state, Register::RA) = static_cast<std::int32_t>(state.pc + 1);
        dynamic.next_pc = instruction.target;
        break;
      case Opcode::Jr: {
        const std::int32_t target = reg(state, instruction.rs);
        if (instruction.rs == Register::RA && target == kHaltReturnAddress) {
          state.regs[0] = 0;
          trace.push_back(dynamic);
          result.success = true;
          result.exit_code = reg(state, Register::V0);
          result.registers = state.regs;
          return trace;
        }
        if (target < 0 || static_cast<std::size_t>(target) >= program.instructions.size()) {
          result.error = "jr target out of range: " + std::to_string(target);
          result.registers = state.regs;
          return {};
        }
        dynamic.next_pc = static_cast<std::size_t>(target);
        break;
      }
      case Opcode::Mult: {
        const std::int64_t wide = static_cast<std::int64_t>(reg(state, instruction.rs)) *
            static_cast<std::int64_t>(reg(state, instruction.rt));
        state.lo = static_cast<std::int32_t>(wide & 0xffffffffLL);
        state.hi = static_cast<std::int32_t>((wide >> 32) & 0xffffffffLL);
        break;
      }
      case Opcode::Div:
        if (reg(state, instruction.rt) == 0) {
          result.error = "division by zero";
          result.registers = state.regs;
          return {};
        }
        state.lo = reg(state, instruction.rs) / reg(state, instruction.rt);
        state.hi = reg(state, instruction.rs) % reg(state, instruction.rt);
        break;
      case Opcode::Mflo:
        reg(state, instruction.rd) = state.lo;
        break;
      case Opcode::Mfhi:
        reg(state, instruction.rd) = state.hi;
        break;
    }

    state.regs[0] = 0;
    trace.push_back(dynamic);
    state.pc = dynamic.next_pc;
  }

  result.error = "program counter ran past loaded instructions";
  result.registers = state.regs;
  return {};
}

std::size_t process_branch_event(
    const DynamicInstruction& dynamic,
    PredictorKind predictor_kind,
    TwoBitPredictor& predictor,
    RunResult& result,
    std::vector<std::string>& events) {
  if (!dynamic.conditional_branch) {
    return 0;
  }

  const bool predicted_taken = predict_branch(predictor_kind, predictor, dynamic.pc);
  result.branch_predictions += 1;
  if (predicted_taken != dynamic.actual_taken) {
    result.branch_mispredictions += 1;
    std::ostringstream line;
    line << "mispredict(" << mips::isa::opcode_name(dynamic.instruction.opcode) << " -> pc="
         << dynamic.next_pc << ")";
    events.push_back(line.str());
  } else {
    events.push_back("predict(correct)");
  }

  update_predictor(predictor_kind, predictor, dynamic.pc, dynamic.actual_taken);

  if (predicted_taken != dynamic.actual_taken) {
    return 1;
  }
  return 0;
}

enum class UnitKind {
  Alu,
  Memory,
  Branch,
  Multiply,
  Divide,
  HiLoMove,
};

struct ScoreboardEntry {
  const DynamicInstruction* dynamic = nullptr;
  Usage usage{};
  UnitKind unit = UnitKind::Alu;
  std::size_t latency = 1;
  bool issued = false;
  bool executing = false;
  bool completed = false;
  std::size_t issue_cycle = 0;
  std::size_t finish_cycle = 0;
};

std::string_view unit_name(UnitKind unit) {
  switch (unit) {
    case UnitKind::Alu:
      return "alu";
    case UnitKind::Memory:
      return "mem";
    case UnitKind::Branch:
      return "branch";
    case UnitKind::Multiply:
      return "mul";
    case UnitKind::Divide:
      return "div";
    case UnitKind::HiLoMove:
      return "hilo";
  }
  return "unknown";
}

UnitKind unit_for(const LoadedInstruction& instruction) {
  switch (instruction.opcode) {
    case Opcode::Lw:
    case Opcode::Sw:
      return UnitKind::Memory;
    case Opcode::Beq:
    case Opcode::Bne:
    case Opcode::J:
    case Opcode::Jal:
    case Opcode::Jr:
      return UnitKind::Branch;
    case Opcode::Mult:
      return UnitKind::Multiply;
    case Opcode::Div:
      return UnitKind::Divide;
    case Opcode::Mflo:
    case Opcode::Mfhi:
      return UnitKind::HiLoMove;
    default:
      return UnitKind::Alu;
  }
}

std::size_t unit_latency(const LoadedInstruction& instruction) {
  switch (instruction.opcode) {
    case Opcode::Lw:
    case Opcode::Sw:
      return 2;
    case Opcode::Mult:
      return 3;
    case Opcode::Div:
      return 4;
    default:
      return 1;
  }
}

std::size_t unit_index(UnitKind unit) {
  return static_cast<std::size_t>(unit);
}

bool earlier_waiting_reader(
    const std::vector<ScoreboardEntry>& entries,
    std::size_t writer_index,
    Register reg_name) {
  if (reg_name == Register::Zero) {
    return false;
  }
  for (std::size_t index = 0; index < writer_index; ++index) {
    const auto& entry = entries[index];
    if (!entry.issued || entry.completed || entry.executing) {
      continue;
    }
    if (contains_register(entry.usage.reads, reg_name)) {
      return true;
    }
  }
  return false;
}

bool earlier_waiting_hi_lo_reader(
    const std::vector<ScoreboardEntry>& entries,
    std::size_t writer_index,
    bool check_hi,
    bool check_lo) {
  for (std::size_t index = 0; index < writer_index; ++index) {
    const auto& entry = entries[index];
    if (!entry.issued || entry.completed || entry.executing) {
      continue;
    }
    if ((check_hi && entry.usage.reads_hi) || (check_lo && entry.usage.reads_lo)) {
      return true;
    }
  }
  return false;
}

RunResult analyze_scoreboard(
    const std::vector<DynamicInstruction>& trace,
    const RunOptions& options,
    const RunResult& seed) {
  RunResult result = seed;
  result.executed_instructions = trace.size();
  TwoBitPredictor predictor;

  std::vector<ScoreboardEntry> entries;
  entries.reserve(trace.size());
  for (const auto& dynamic : trace) {
    entries.push_back(
        ScoreboardEntry{
            .dynamic = &dynamic,
            .usage = usage_for(dynamic.instruction),
            .unit = unit_for(dynamic.instruction),
            .latency = unit_latency(dynamic.instruction),
        });
  }

  std::array<std::optional<std::size_t>, 32> reg_writer{};
  std::optional<std::size_t> hi_writer;
  std::optional<std::size_t> lo_writer;
  std::array<std::optional<std::size_t>, 6> busy_units{};
  std::size_t next_issue = 0;
  std::size_t completed = 0;
  std::size_t cycle = 0;
  std::size_t pending_flush_cycles = 0;

  while (completed < entries.size()) {
    cycle += 1;
    Packet packet;
    std::vector<std::string> events;

    for (std::size_t index = 0; index < entries.size(); ++index) {
      auto& entry = entries[index];
      if (!entry.issued || entry.completed || !entry.executing || entry.finish_cycle > cycle) {
        continue;
      }

      bool war_blocked = false;
      for (const auto reg_name : entry.usage.writes) {
        if (earlier_waiting_reader(entries, index, reg_name)) {
          war_blocked = true;
          events.push_back("wait-war(" + std::string(entry.dynamic->label) + ")");
          break;
        }
      }
      if (!war_blocked &&
          earlier_waiting_hi_lo_reader(entries, index, entry.usage.writes_hi, entry.usage.writes_lo)) {
        war_blocked = true;
        events.push_back("wait-war-hilo(" + std::string(entry.dynamic->label) + ")");
      }
      if (war_blocked) {
        entry.finish_cycle += 1;
        continue;
      }

      entry.completed = true;
      entry.executing = false;
      busy_units[unit_index(entry.unit)].reset();
      for (const auto reg_name : entry.usage.writes) {
        if (reg_writer[mips::isa::register_index(reg_name)] == index) {
          reg_writer[mips::isa::register_index(reg_name)].reset();
        }
      }
      if (entry.usage.writes_hi && hi_writer == index) {
        hi_writer.reset();
      }
      if (entry.usage.writes_lo && lo_writer == index) {
        lo_writer.reset();
      }
      completed += 1;
      events.push_back("wb(" + entry.dynamic->label + ")");
    }

    for (std::size_t index = 0; index < entries.size(); ++index) {
      auto& entry = entries[index];
      if (!entry.issued || entry.completed || entry.executing) {
        continue;
      }

      bool operands_ready = true;
      for (const auto reg_name : entry.usage.reads) {
        const auto writer = reg_writer[mips::isa::register_index(reg_name)];
        if (writer.has_value() && *writer != index) {
          operands_ready = false;
          break;
        }
      }
      if (operands_ready && entry.usage.reads_hi && hi_writer.has_value() && *hi_writer != index) {
        operands_ready = false;
      }
      if (operands_ready && entry.usage.reads_lo && lo_writer.has_value() && *lo_writer != index) {
        operands_ready = false;
      }
      if (!operands_ready) {
        events.push_back("wait-raw(" + std::string(entry.dynamic->label) + ")");
        continue;
      }

      entry.executing = true;
      entry.finish_cycle = cycle + entry.latency;
      events.push_back(
          "ex(" + entry.dynamic->label + "," + std::string(unit_name(entry.unit)) + ")");

      const std::size_t mispredicts =
          process_branch_event(*entry.dynamic, options.predictor, predictor, result, events);
      if (mispredicts > 0) {
        pending_flush_cycles += mispredicts * options.mispredict_penalty;
        result.speculative_flush_cycles += mispredicts * options.mispredict_penalty;
      }
    }

    if (pending_flush_cycles > 0) {
      events.push_back("spec-flush");
      pending_flush_cycles -= 1;
    } else if (next_issue < entries.size()) {
      auto& entry = entries[next_issue];
      const auto unit_slot = unit_index(entry.unit);
      bool can_issue = !busy_units[unit_slot].has_value();
      if (can_issue) {
        for (const auto reg_name : entry.usage.writes) {
          if (reg_writer[mips::isa::register_index(reg_name)].has_value()) {
            can_issue = false;
            break;
          }
        }
      }
      if (can_issue && entry.usage.writes_hi && hi_writer.has_value()) {
        can_issue = false;
      }
      if (can_issue && entry.usage.writes_lo && lo_writer.has_value()) {
        can_issue = false;
      }

      if (can_issue) {
        entry.issued = true;
        entry.issue_cycle = cycle;
        busy_units[unit_slot] = next_issue;
        for (const auto reg_name : entry.usage.writes) {
          reg_writer[mips::isa::register_index(reg_name)] = next_issue;
        }
        if (entry.usage.writes_hi) {
          hi_writer = next_issue;
        }
        if (entry.usage.writes_lo) {
          lo_writer = next_issue;
        }
        packet.instructions.push_back(entry.dynamic);
        result.issued_packets += 1;
        result.issued_slots += 1;
        events.push_back("issue(" + entry.dynamic->label + ")");
        next_issue += 1;
      } else {
        events.push_back("issue-stall(" + std::string(unit_name(entry.unit)) + ")");
      }
    }

    if (options.trace) {
      result.trace_lines.push_back(
          trace_line(cycle, SchedulerKind::Scoreboard, 1, packet, events, false));
    }
  }

  result.cycles = cycle;
  return result;
}

RunResult analyze_inorder(
    const std::vector<DynamicInstruction>& trace,
    const RunOptions& options,
    const RunResult& seed) {
  RunResult result = seed;
  result.executed_instructions = trace.size();
  TwoBitPredictor predictor;
  std::size_t cycle = 0;

  for (std::size_t index = 0; index < trace.size();) {
    Packet packet;
    packet.instructions.push_back(&trace[index]);

    if (options.issue_width >= 2 && (index + 1) < trace.size() &&
        !is_control(trace[index].instruction) && trace[index + 1].pc == (trace[index].pc + 1) &&
        !has_pair_hazard(trace[index].instruction, trace[index + 1].instruction)) {
      packet.instructions.push_back(&trace[index + 1]);
    }

    cycle += 1;
    result.issued_packets += 1;
    result.issued_slots += packet.instructions.size();
    std::vector<std::string> events;
    std::size_t packet_mispredictions = 0;

    for (const auto* dynamic : packet.instructions) {
      packet_mispredictions += process_branch_event(*dynamic, options.predictor, predictor, result, events);
    }

    if (options.trace) {
      result.trace_lines.push_back(
          trace_line(
              cycle,
              SchedulerKind::InOrder,
              options.issue_width,
              packet,
              events,
              false));
    }

    if (packet_mispredictions > 0) {
      result.speculative_flush_cycles += packet_mispredictions * options.mispredict_penalty;
      for (std::size_t mispredict = 0; mispredict < packet_mispredictions; ++mispredict) {
        for (std::size_t step = 0; step < options.mispredict_penalty; ++step) {
          cycle += 1;
          if (options.trace) {
            result.trace_lines.push_back(
                trace_line(
                    cycle,
                    SchedulerKind::InOrder,
                    options.issue_width,
                    Packet{},
                    {std::string("spec-flush(") + std::to_string(step + 1U) + "/" +
                        std::to_string(options.mispredict_penalty) + ")"},
                    false));
          }
        }
      }
    }

    index += packet.instructions.size();
  }

  result.cycles = cycle;
  return result;
}

std::vector<std::vector<const DynamicInstruction*>> partition_blocks(const std::vector<DynamicInstruction>& trace) {
  std::vector<std::vector<const DynamicInstruction*>> blocks;
  std::vector<const DynamicInstruction*> current;

  for (std::size_t index = 0; index < trace.size(); ++index) {
    current.push_back(&trace[index]);
    const bool block_end =
        is_control(trace[index].instruction) ||
        ((index + 1) < trace.size() && trace[index + 1].pc != (trace[index].pc + 1));
    if (block_end) {
      blocks.push_back(current);
      current.clear();
    }
  }

  if (!current.empty()) {
    blocks.push_back(current);
  }

  return blocks;
}

std::vector<Packet> schedule_vliw_block(const std::vector<const DynamicInstruction*>& block) {
  const std::size_t count = block.size();
  std::vector<std::vector<std::size_t>> successors(count);
  std::vector<std::size_t> indegree(count, 0);

  for (std::size_t earlier = 0; earlier < count; ++earlier) {
    for (std::size_t later = earlier + 1; later < count; ++later) {
      if (has_reorder_dependency(block[earlier]->instruction, block[later]->instruction)) {
        successors[earlier].push_back(later);
        indegree[later] += 1;
      }
    }
  }

  std::vector<bool> scheduled(count, false);
  std::vector<Packet> packets;
  std::size_t scheduled_count = 0;

  while (scheduled_count < count) {
    std::vector<std::size_t> ready;
    for (std::size_t index = 0; index < count; ++index) {
      if (!scheduled[index] && indegree[index] == 0) {
        ready.push_back(index);
      }
    }

    std::sort(ready.begin(), ready.end());
    const std::size_t first = ready.front();
    Packet packet;
    packet.instructions.push_back(block[first]);

    std::optional<std::size_t> second;
    for (const auto candidate : ready) {
      if (candidate == first) {
        continue;
      }
      if (!has_pair_hazard(block[first]->instruction, block[candidate]->instruction)) {
        second = candidate;
        break;
      }
    }

    std::vector<std::size_t> chosen{first};
    if (second.has_value()) {
      chosen.push_back(*second);
      packet.instructions.push_back(block[*second]);
    }

    for (const auto index : chosen) {
      scheduled[index] = true;
      scheduled_count += 1;
    }
    for (const auto index : chosen) {
      for (const auto succ : successors[index]) {
        indegree[succ] -= 1;
      }
    }

    packets.push_back(packet);
  }

  return packets;
}

RunResult analyze_vliw(
    const std::vector<DynamicInstruction>& trace,
    const RunOptions& options,
    const RunResult& seed) {
  RunResult result = seed;
  result.executed_instructions = trace.size();
  TwoBitPredictor predictor;
  std::size_t cycle = 0;

  for (const auto& block : partition_blocks(trace)) {
    for (const auto& packet : schedule_vliw_block(block)) {
      cycle += 1;
      result.issued_packets += 1;
      result.issued_slots += packet.instructions.size();
      std::vector<std::string> events;
      std::size_t packet_mispredictions = 0;

      for (const auto* dynamic : packet.instructions) {
        packet_mispredictions += process_branch_event(*dynamic, options.predictor, predictor, result, events);
      }

      if (options.trace) {
        result.trace_lines.push_back(
            trace_line(
                cycle,
                SchedulerKind::VliwLite,
                2,
                packet,
              events,
              true));
      }

      if (packet_mispredictions > 0) {
        result.speculative_flush_cycles += packet_mispredictions * options.mispredict_penalty;
        for (std::size_t mispredict = 0; mispredict < packet_mispredictions; ++mispredict) {
          for (std::size_t step = 0; step < options.mispredict_penalty; ++step) {
            cycle += 1;
            if (options.trace) {
              result.trace_lines.push_back(
                  trace_line(
                      cycle,
                      SchedulerKind::VliwLite,
                      2,
                      Packet{},
                      {std::string("spec-flush(") + std::to_string(step + 1U) + "/" +
                          std::to_string(options.mispredict_penalty) + ")"},
                      false));
            }
          }
        }
      }
    }
  }

  result.cycles = cycle;
  return result;
}

}  // namespace

std::string_view scheduler_name(SchedulerKind scheduler) {
  switch (scheduler) {
    case SchedulerKind::InOrder:
      return "inorder";
    case SchedulerKind::VliwLite:
      return "vliw-lite";
    case SchedulerKind::Scoreboard:
      return "scoreboard";
  }

  return "unknown";
}

RunResult run_program(const LoadedProgram& program, const RunOptions& options) {
  RunResult seed;
  auto trace = execute_trace(program, options, seed);
  if (!seed.success) {
    return seed;
  }

  if (options.issue_width == 0 || options.issue_width > 2) {
    seed.success = false;
    seed.error = "advanced issue width must be 1 or 2";
    return seed;
  }

  if (options.scheduler == SchedulerKind::VliwLite && options.issue_width != 2) {
    seed.success = false;
    seed.error = "vliw-lite requires issue width 2";
    return seed;
  }

  if (options.scheduler == SchedulerKind::Scoreboard && options.issue_width != 1) {
    seed.success = false;
    seed.error = "scoreboard requires issue width 1";
    return seed;
  }

  if (options.scheduler == SchedulerKind::VliwLite) {
    return analyze_vliw(trace, options, seed);
  }
  if (options.scheduler == SchedulerKind::Scoreboard) {
    return analyze_scoreboard(trace, options, seed);
  }
  return analyze_inorder(trace, options, seed);
}

}  // namespace nexus::sim::advanced
