#include "nexus/sim/parallel/model.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "nexus/mips/isa/instruction.hpp"

namespace nexus::sim::parallel {

namespace {

using mips::isa::Opcode;
using mips::isa::Register;
using mips::loader::LoadedInstruction;
using mips::loader::LoadedProgram;

constexpr std::int32_t kHaltReturnAddress = std::numeric_limits<std::int32_t>::min();
constexpr std::size_t kStackGuardWords = 16;
constexpr std::int32_t kSyncBaseAddress = 0x00020000;
constexpr std::int32_t kLockAcquireAddress = kSyncBaseAddress;
constexpr std::int32_t kLockReleaseAddress = kSyncBaseAddress + 4;
constexpr std::int32_t kBarrierAddress = kSyncBaseAddress + 8;
constexpr std::int32_t kAtomicFetchIncAddress = kSyncBaseAddress + 12;

enum class CacheState {
  Invalid,
  Shared,
  Modified,
};

struct CacheLine {
  CacheState state = CacheState::Invalid;
  std::int32_t value = 0;
};

struct DirectoryEntry {
  std::optional<std::size_t> owner;
  std::set<std::size_t> sharers;
};

struct PendingStore {
  std::int32_t address = 0;
  std::int32_t value = 0;
};

struct CoreState {
  std::array<std::int32_t, 32> regs{};
  std::int32_t hi = 0;
  std::int32_t lo = 0;
  std::size_t pc = 0;
  bool started = false;
  bool halted = false;
  bool waiting_at_barrier = false;
  std::size_t stall_cycles = 0;
  std::deque<PendingStore> store_buffer;
  std::size_t retired = 0;
  std::int32_t exit_code = 0;
};

struct MemoryAccessResult {
  bool ok = true;
  std::int32_t value = 0;
  std::size_t latency = 1;
  bool cache_hit = false;
  bool cache_miss = false;
  bool repeated = false;
  std::string error;
  std::vector<std::string> events;
};

struct SharedState {
  std::vector<std::int32_t> memory;
  std::vector<std::map<std::int32_t, CacheLine>> caches;
  std::map<std::int32_t, DirectoryEntry> directory;
  std::optional<std::size_t> lock_owner;
  std::size_t active_cores = 0;
  std::size_t barrier_target = 0;
  std::vector<bool> barrier_waiting;
  std::int32_t atomic_counter = 0;
};

std::int32_t& reg(CoreState& core, Register reg_name) {
  return core.regs[mips::isa::register_index(reg_name)];
}

std::uint32_t as_u32(std::int32_t value) { return static_cast<std::uint32_t>(value); }

std::string format_core_label(std::size_t core_id, std::size_t pc, const LoadedInstruction& instruction) {
  std::ostringstream stream;
  stream << "core" << core_id << "@pc" << pc << ':' << mips::isa::opcode_name(instruction.opcode);
  return stream.str();
}

std::optional<std::size_t> started_label_pc(const LoadedProgram& program, std::size_t core_id) {
  const auto it = program.labels.find("core" + std::to_string(core_id));
  if (it == program.labels.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<std::size_t> word_index(std::int32_t address, const SharedState& shared, std::string& error) {
  if (address < 0 || (address % 4) != 0) {
    error = "invalid word address " + std::to_string(address);
    return std::nullopt;
  }
  const std::size_t index = static_cast<std::size_t>(address / 4);
  if (index >= shared.memory.size()) {
    error = "memory access out of bounds at address " + std::to_string(address);
    return std::nullopt;
  }
  return index;
}

std::size_t hop_cost(InterconnectKind kind, std::size_t cores, std::size_t source, std::size_t target) {
  switch (kind) {
    case InterconnectKind::Mesh: {
      std::size_t width = 1;
      while (width * width < cores) {
        ++width;
      }
      const std::size_t sx = source % width;
      const std::size_t sy = source / width;
      const std::size_t tx = target % width;
      const std::size_t ty = target / width;
      // one router traversal per hop plus injection
      return 1 + (sx > tx ? sx - tx : tx - sx) + (sy > ty ? sy - ty : ty - sy);
    }
    case InterconnectKind::Ring: {
      const std::size_t forward = source > target ? source - target : target - source;
      return 1 + std::min(forward, cores - forward);
    }
    case InterconnectKind::Bus:
      return 3;
    case InterconnectKind::Switch:
      return 2;
    case InterconnectKind::NoCLite: {
      const std::size_t sx = source % 2;
      const std::size_t sy = source / 2;
      const std::size_t tx = target % 2;
      const std::size_t ty = target / 2;
      const std::size_t manhattan =
          (sx > tx ? sx - tx : tx - sx) + (sy > ty ? sy - ty : ty - sy);
      return 1 + manhattan;
    }
  }
  return 1;
}

std::size_t coherence_penalty(
    const RunOptions& options,
    RunResult& result,
    std::size_t source_core,
    const std::vector<std::size_t>& targets) {
  if (targets.empty()) {
    return 0;
  }

  std::size_t penalty = 0;
  for (const std::size_t target : targets) {
    penalty += hop_cost(options.interconnect, options.cores, source_core, target);
    result.interconnect_messages += 1;
  }
  result.interconnect_cycles += penalty;
  return penalty;
}

void note_event(RunOptions const& options, RunResult& result, const std::string& line) {
  if (options.trace) {
    result.trace_lines.push_back(line);
  }
  result.system_lines.push_back(line);
}

void invalidate_other_caches(
    SharedState& shared,
    RunOptions const& options,
    RunResult& result,
    std::size_t writer_core,
    std::int32_t address,
    std::vector<std::string>& events) {
  std::vector<std::size_t> invalidated_cores;
  for (std::size_t core = 0; core < shared.caches.size(); ++core) {
    if (core == writer_core) {
      continue;
    }
    auto it = shared.caches[core].find(address);
    if (it == shared.caches[core].end() || it->second.state == CacheState::Invalid) {
      continue;
    }
    it->second.state = CacheState::Invalid;
    invalidated_cores.push_back(core);
    result.coherence_events += 1;
    result.invalidations += 1;
  }

  if (options.coherence == CoherenceKind::DirectoryLite) {
    result.directory_lookups += 1;
  }

  if (!invalidated_cores.empty()) {
    const std::size_t penalty = coherence_penalty(options, result, writer_core, invalidated_cores);
    std::ostringstream line;
    line << "coherence[" << coherence_name(options.coherence) << "]: core" << writer_core
         << " invalidated " << invalidated_cores.size() << " sharer(s) at address " << address
         << " penalty=" << penalty;
    events.push_back(line.str());
  }

  DirectoryEntry& entry = shared.directory[address];
  entry.owner = writer_core;
  entry.sharers.clear();
  entry.sharers.insert(writer_core);
}

MemoryAccessResult commit_store(
    SharedState& shared,
    RunOptions const& options,
    RunResult& result,
    std::size_t core_id,
    std::int32_t address,
    std::int32_t value) {
  MemoryAccessResult access;
  std::string error;
  const auto index = word_index(address, shared, error);
  if (!index.has_value()) {
    access.ok = false;
    access.error = error;
    return access;
  }

  shared.memory[*index] = value;
  CacheLine& line = shared.caches[core_id][address];
  if (line.state != CacheState::Invalid) {
    access.cache_hit = true;
    result.cache_hits += 1;
  } else {
    access.cache_miss = true;
    result.cache_misses += 1;
  }
  line.state = CacheState::Modified;
  line.value = value;

  invalidate_other_caches(shared, options, result, core_id, address, access.events);

  result.memory_accesses += 1;
  result.memory_writes += 1;
  access.latency = options.memory_latency + 1;
  return access;
}

MemoryAccessResult load_word(
    SharedState& shared,
    RunOptions const& options,
    RunResult& result,
    CoreState& core,
    std::size_t core_id,
    std::int32_t address) {
  MemoryAccessResult access;

  if (address == kAtomicFetchIncAddress) {
    access.value = shared.atomic_counter;
    shared.atomic_counter += 1;
    result.atomic_operations += 1;
    result.synchronization_events += 1;
    access.latency = 1;
    std::ostringstream line;
    line << "sync[atomic-fetch-inc]: core" << core_id << " value=" << access.value;
    access.events.push_back(line.str());
    return access;
  }

  if (options.consistency == ConsistencyKind::WeakLite) {
    for (auto it = core.store_buffer.rbegin(); it != core.store_buffer.rend(); ++it) {
      if (it->address == address) {
        access.value = it->value;
        access.cache_hit = true;
        result.cache_hits += 1;
        std::ostringstream line;
        line << "consistency[weak-lite]: core" << core_id
             << " forwarded buffered store at address " << address;
        access.events.push_back(line.str());
        return access;
      }
    }
  }

  auto cache_it = shared.caches[core_id].find(address);
  if (cache_it != shared.caches[core_id].end() && cache_it->second.state != CacheState::Invalid) {
    access.value = cache_it->second.value;
    access.cache_hit = true;
    result.cache_hits += 1;
    access.latency = 1;
    return access;
  }

  std::string error;
  const auto index = word_index(address, shared, error);
  if (!index.has_value()) {
    access.ok = false;
    access.error = error;
    return access;
  }

  access.value = shared.memory[*index];
  access.cache_miss = true;
  result.cache_misses += 1;
  result.memory_accesses += 1;
  result.memory_reads += 1;

  std::vector<std::size_t> sharers;
  for (std::size_t other = 0; other < shared.caches.size(); ++other) {
    if (other == core_id) {
      continue;
    }
    auto other_it = shared.caches[other].find(address);
    if (other_it != shared.caches[other].end() && other_it->second.state != CacheState::Invalid) {
      sharers.push_back(other);
      other_it->second.state = CacheState::Shared;
    }
  }

  if (options.coherence == CoherenceKind::DirectoryLite) {
    result.directory_lookups += 1;
    DirectoryEntry& entry = shared.directory[address];
    entry.owner.reset();
    entry.sharers.insert(core_id);
    entry.sharers.insert(sharers.begin(), sharers.end());
  }

  CacheLine& line = shared.caches[core_id][address];
  line.state = CacheState::Shared;
  line.value = access.value;

  const std::size_t penalty = coherence_penalty(options, result, core_id, sharers);
  access.latency = options.memory_latency + 1 + penalty;
  if (!sharers.empty()) {
    std::ostringstream line_text;
    line_text << "coherence[" << coherence_name(options.coherence) << "]: core" << core_id
              << " observed " << sharers.size() << " sharer(s) at address " << address;
    access.events.push_back(line_text.str());
  }
  return access;
}

MemoryAccessResult store_word(
    SharedState& shared,
    RunOptions const& options,
    RunResult& result,
    CoreState& core,
    std::size_t core_id,
    std::int32_t address,
    std::int32_t value) {
  MemoryAccessResult access;

  if (address == kLockAcquireAddress) {
    if (!shared.lock_owner.has_value() || *shared.lock_owner == core_id) {
      shared.lock_owner = core_id;
      result.lock_acquisitions += 1;
      result.synchronization_events += 1;
      access.latency = 1;
      std::ostringstream line;
      line << "sync[lock-acquire]: core" << core_id;
      access.events.push_back(line.str());
      return access;
    }
    result.lock_contentions += 1;
    access.repeated = true;
    access.latency = 1;
    std::ostringstream line;
    line << "sync[lock-contended]: core" << core_id << " owner=core" << *shared.lock_owner;
    access.events.push_back(line.str());
    return access;
  }

  if (address == kLockReleaseAddress) {
    if (shared.lock_owner == core_id) {
      shared.lock_owner.reset();
      result.synchronization_events += 1;
      access.latency = 1;
      std::ostringstream line;
      line << "sync[lock-release]: core" << core_id;
      access.events.push_back(line.str());
      return access;
    }
    access.ok = false;
    access.error = "core attempted to release a lock it does not own";
    return access;
  }

  if (address == kBarrierAddress) {
    if (!core.waiting_at_barrier) {
      core.waiting_at_barrier = true;
      shared.barrier_waiting[core_id] = true;
      result.barrier_arrivals += 1;
      result.synchronization_events += 1;
      std::ostringstream line;
      line << "sync[barrier-arrive]: core" << core_id;
      access.events.push_back(line.str());
    }
    access.latency = 1;
    return access;
  }

  if (options.consistency == ConsistencyKind::WeakLite) {
    core.store_buffer.push_back({.address = address, .value = value});
    access.latency = 1;
    std::ostringstream line;
    line << "consistency[weak-lite]: core" << core_id
         << " buffered store address=" << address << " value=" << value;
    access.events.push_back(line.str());
    return access;
  }

  return commit_store(shared, options, result, core_id, address, value);
}

bool should_flush_store_buffer(std::size_t cycle, std::size_t core_id) {
  return ((cycle + core_id) % 3U) == 0U;
}

bool drain_store_buffer(
    SharedState& shared,
    RunOptions const& options,
    RunResult& result,
    CoreState& core,
    std::size_t core_id) {
  if (core.store_buffer.empty()) {
    return true;
  }

  const PendingStore pending = core.store_buffer.front();
  core.store_buffer.pop_front();
  const MemoryAccessResult access = commit_store(shared, options, result, core_id, pending.address, pending.value);
  if (!access.ok) {
    result.success = false;
    result.error = access.error;
    return false;
  }
  result.store_buffer_flushes += 1;
  std::ostringstream line;
  line << "consistency[weak-lite]: drain core" << core_id << " address=" << pending.address
       << " value=" << pending.value;
  note_event(options, result, line.str());
  for (const auto& event : access.events) {
    note_event(options, result, event);
  }
  return true;
}

bool execute_instruction(
    SharedState& shared,
    RunOptions const& options,
    const LoadedProgram& program,
    RunResult& result,
    CoreState& core,
    std::size_t core_id) {
  if (core.pc >= program.instructions.size()) {
    result.error = "program counter ran past loaded instructions for core " + std::to_string(core_id);
    return false;
  }

  const LoadedInstruction& instruction = program.instructions[core.pc];
  const std::string label = format_core_label(core_id, core.pc, instruction);
  const std::size_t current_pc = core.pc;
  std::size_t next_pc = current_pc + 1;
  bool repeated = false;
  std::vector<std::string> events;

  switch (instruction.opcode) {
    case Opcode::Add:
    case Opcode::Addu:
      reg(core, instruction.rd) = reg(core, instruction.rs) + reg(core, instruction.rt);
      break;
    case Opcode::Addiu:
      reg(core, instruction.rt) = reg(core, instruction.rs) + instruction.immediate;
      break;
    case Opcode::Sub:
      reg(core, instruction.rd) = reg(core, instruction.rs) - reg(core, instruction.rt);
      break;
    case Opcode::And:
      reg(core, instruction.rd) = reg(core, instruction.rs) & reg(core, instruction.rt);
      break;
    case Opcode::Or:
      reg(core, instruction.rd) = reg(core, instruction.rs) | reg(core, instruction.rt);
      break;
    case Opcode::Ori:
      reg(core, instruction.rt) = static_cast<std::int32_t>(
          as_u32(reg(core, instruction.rs)) |
          (static_cast<std::uint32_t>(instruction.immediate) & 0xffffU));
      break;
    case Opcode::Xor:
      reg(core, instruction.rd) = reg(core, instruction.rs) ^ reg(core, instruction.rt);
      break;
    case Opcode::Xori:
      reg(core, instruction.rt) = static_cast<std::int32_t>(
          as_u32(reg(core, instruction.rs)) ^
          (static_cast<std::uint32_t>(instruction.immediate) & 0xffffU));
      break;
    case Opcode::Slt:
      reg(core, instruction.rd) = reg(core, instruction.rs) < reg(core, instruction.rt) ? 1 : 0;
      break;
    case Opcode::Sltu:
      reg(core, instruction.rd) = as_u32(reg(core, instruction.rs)) < as_u32(reg(core, instruction.rt)) ? 1 : 0;
      break;
    case Opcode::Sltiu:
      reg(core, instruction.rt) = as_u32(reg(core, instruction.rs)) < as_u32(instruction.immediate) ? 1 : 0;
      break;
    case Opcode::Sll:
      reg(core, instruction.rd) =
          static_cast<std::int32_t>(as_u32(reg(core, instruction.rt)) << instruction.immediate);
      break;
    case Opcode::Lui:
      reg(core, instruction.rt) =
          static_cast<std::int32_t>((static_cast<std::uint32_t>(instruction.immediate) & 0xffffU) << 16U);
      break;
    case Opcode::Lw: {
      const std::int32_t address = reg(core, instruction.rs) + instruction.immediate;
      const MemoryAccessResult access = load_word(shared, options, result, core, core_id, address);
      if (!access.ok) {
        result.error = access.error;
        return false;
      }
      reg(core, instruction.rt) = access.value;
      core.stall_cycles += access.latency > 0 ? access.latency - 1 : 0;
      events.insert(events.end(), access.events.begin(), access.events.end());
      break;
    }
    case Opcode::Sw: {
      const std::int32_t address = reg(core, instruction.rs) + instruction.immediate;
      const MemoryAccessResult access = store_word(shared, options, result, core, core_id, address, reg(core, instruction.rt));
      if (!access.ok) {
        result.error = access.error;
        return false;
      }
      repeated = access.repeated;
      core.stall_cycles += access.latency > 0 ? access.latency - 1 : 0;
      events.insert(events.end(), access.events.begin(), access.events.end());
      break;
    }
    case Opcode::Beq:
      if (reg(core, instruction.rs) == reg(core, instruction.rt)) {
        next_pc = instruction.target;
      }
      break;
    case Opcode::Bne:
      if (reg(core, instruction.rs) != reg(core, instruction.rt)) {
        next_pc = instruction.target;
      }
      break;
    case Opcode::J:
      next_pc = instruction.target;
      break;
    case Opcode::Jal:
      reg(core, Register::RA) = static_cast<std::int32_t>(current_pc + 1);
      next_pc = instruction.target;
      break;
    case Opcode::Jr: {
      const std::int32_t target = reg(core, instruction.rs);
      if (instruction.rs == Register::RA && target == kHaltReturnAddress) {
        core.halted = true;
        core.exit_code = reg(core, Register::V0);
      } else if (target < 0 || static_cast<std::size_t>(target) >= program.instructions.size()) {
        result.error = "jr target out of range for core " + std::to_string(core_id);
        return false;
      } else {
        next_pc = static_cast<std::size_t>(target);
      }
      break;
    }
    case Opcode::Mult: {
      const std::int64_t wide = static_cast<std::int64_t>(reg(core, instruction.rs)) *
          static_cast<std::int64_t>(reg(core, instruction.rt));
      core.lo = static_cast<std::int32_t>(wide & 0xffffffffLL);
      core.hi = static_cast<std::int32_t>((wide >> 32) & 0xffffffffLL);
      break;
    }
    case Opcode::Div:
      if (reg(core, instruction.rt) == 0) {
        result.error = "division by zero";
        return false;
      }
      core.lo = reg(core, instruction.rs) / reg(core, instruction.rt);
      core.hi = reg(core, instruction.rs) % reg(core, instruction.rt);
      break;
    case Opcode::Mflo:
      reg(core, instruction.rd) = core.lo;
      break;
    case Opcode::Mfhi:
      reg(core, instruction.rd) = core.hi;
      break;
  }

  core.regs[0] = 0;
  if (!core.halted && !repeated) {
    core.pc = next_pc;
  }
  core.retired += repeated ? 0 : 1;
  result.retired_instructions += repeated ? 0 : 1;

  if (options.trace) {
    std::ostringstream line;
    line << "trace[parallel]: cycle=" << result.cycles << ' ' << label;
    if (repeated) {
      line << " repeat";
    }
    if (core.halted) {
      line << " halt exit=" << core.exit_code;
    }
    if (!events.empty()) {
      line << " events=";
      for (std::size_t index = 0; index < events.size(); ++index) {
        if (index != 0) {
          line << " | ";
        }
        line << events[index];
      }
    }
    result.trace_lines.push_back(line.str());
  }
  for (const auto& event : events) {
    result.system_lines.push_back(event);
  }
  return true;
}

}  // namespace

std::string_view coherence_name(CoherenceKind kind) {
  switch (kind) {
    case CoherenceKind::SnoopingLite:
      return "snoop";
    case CoherenceKind::DirectoryLite:
      return "directory-lite";
  }
  return "unknown";
}

std::string_view consistency_name(ConsistencyKind kind) {
  switch (kind) {
    case ConsistencyKind::Sequential:
      return "sc";
    case ConsistencyKind::WeakLite:
      return "weak-lite";
  }
  return "unknown";
}

std::string_view interconnect_name(InterconnectKind kind) {
  switch (kind) {
    case InterconnectKind::Bus:
      return "bus";
    case InterconnectKind::Switch:
      return "switch";
    case InterconnectKind::NoCLite:
      return "noc-lite";
    case InterconnectKind::Mesh:
      return "mesh";
    case InterconnectKind::Ring:
      return "ring";
  }
  return "unknown";
}

RunResult run_program(const LoadedProgram& program, const RunOptions& options) {
  RunResult result;
  SharedState shared;
  shared.memory.assign(options.memory_words, 0);
  shared.caches.resize(options.cores);
  shared.barrier_waiting.assign(options.cores, false);

  std::vector<CoreState> cores(options.cores);
  std::size_t started_cores = 0;
  const std::size_t stack_span = options.cores == 0 ? options.memory_words : options.memory_words / (options.cores + 1);
  for (std::size_t core_id = 0; core_id < options.cores; ++core_id) {
    auto start_pc = started_label_pc(program, core_id);
    bool spmd = false;
    if (!start_pc.has_value()) {
      // SPMD entry: every core without its own coreN label runs `worker` with $a0 = core id
      // and $a1 = number of cores.
      const auto worker = program.labels.find("worker");
      if (worker != program.labels.end()) {
        start_pc = worker->second;
        spmd = true;
      }
    }
    if (!start_pc.has_value()) {
      if (core_id == 0) {
        start_pc = program.entry_point;
      } else {
        cores[core_id].halted = true;
        continue;
      }
    }
    cores[core_id].started = true;
    cores[core_id].pc = *start_pc;
    const std::size_t stack_top_words =
        options.memory_words > kStackGuardWords + (core_id * stack_span)
        ? options.memory_words - kStackGuardWords - (core_id * stack_span)
        : options.memory_words;
    reg(cores[core_id], Register::SP) = static_cast<std::int32_t>(stack_top_words * sizeof(std::int32_t));
    reg(cores[core_id], Register::RA) = kHaltReturnAddress;
    if (spmd) {
      reg(cores[core_id], Register::A0) = static_cast<std::int32_t>(core_id);
      reg(cores[core_id], Register::A1) = static_cast<std::int32_t>(options.cores);
    }
    started_cores += 1;
  }

  shared.active_cores = started_cores;
  shared.barrier_target = started_cores;
  result.active_cores = started_cores;
  result.core_exit_codes.assign(options.cores, 0);

  if (started_cores == 0) {
    result.error = "parallel mode found no active core entry points";
    return result;
  }

  while (result.cycles < options.max_cycles) {
    bool any_progress = false;
    result.cycles += 1;

    if (std::count(shared.barrier_waiting.begin(), shared.barrier_waiting.end(), true) == static_cast<int>(shared.barrier_target) &&
        shared.barrier_target > 0) {
      for (std::size_t core_id = 0; core_id < options.cores; ++core_id) {
        if (shared.barrier_waiting[core_id]) {
          cores[core_id].waiting_at_barrier = false;
          shared.barrier_waiting[core_id] = false;
        }
      }
      note_event(options, result, "sync[barrier-release]: released all participating cores");
    }

    for (std::size_t core_id = 0; core_id < options.cores; ++core_id) {
      CoreState& core = cores[core_id];
      if (!core.started || core.halted) {
        continue;
      }

      any_progress = true;

      if (core.waiting_at_barrier) {
        result.barrier_wait_cycles += 1;
        if (options.trace) {
          std::ostringstream line;
          line << "trace[parallel]: cycle=" << result.cycles << " core" << core_id << " barrier-wait";
          result.trace_lines.push_back(line.str());
        }
        continue;
      }

      if (core.stall_cycles > 0) {
        core.stall_cycles -= 1;
        if (options.trace) {
          std::ostringstream line;
          line << "trace[parallel]: cycle=" << result.cycles << " core" << core_id
               << " stall remaining=" << core.stall_cycles;
          result.trace_lines.push_back(line.str());
        }
        continue;
      }

      if (!execute_instruction(shared, options, program, result, core, core_id)) {
        return result;
      }

      if (core.halted) {
        result.core_exit_codes[core_id] = core.exit_code;
      } else if (core_id < options.core_cpi.size() && options.core_cpi[core_id] > 1) {
        core.stall_cycles += options.core_cpi[core_id] - 1;  // slower ("little") core
      }
    }

    if (options.consistency == ConsistencyKind::WeakLite) {
      for (std::size_t core_id = 0; core_id < options.cores; ++core_id) {
        if (cores[core_id].store_buffer.empty()) {
          continue;
        }
        if (should_flush_store_buffer(result.cycles, core_id)) {
          if (!drain_store_buffer(shared, options, result, cores[core_id], core_id)) {
            return result;
          }
        }
      }
    }

    const bool all_halted = std::all_of(cores.begin(), cores.end(), [](const CoreState& core) {
      return !core.started || core.halted;
    });
    if (all_halted) {
      result.success = true;
      result.exit_code = result.core_exit_codes.empty() ? 0 : result.core_exit_codes.front();
      return result;
    }

    if (!any_progress) {
      result.error = "parallel machine made no progress";
      return result;
    }
  }

  result.error = "parallel cycle limit exceeded";
  return result;
}

}  // namespace nexus::sim::parallel
