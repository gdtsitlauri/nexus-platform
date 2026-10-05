#include "nexus/sim/simt/model.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <set>
#include <sstream>

#include "nexus/mips/isa/instruction.hpp"

namespace nexus::sim::simt {

namespace {

using mips::isa::Opcode;
using mips::isa::Register;
using mips::loader::LoadedInstruction;
using mips::loader::LoadedProgram;

constexpr std::int32_t kHaltReturnAddress = std::numeric_limits<std::int32_t>::min();
constexpr std::size_t kGlobalWords = 1U << 16;      // shared global memory: bytes [0, 256 KiB)
constexpr std::size_t kLocalWordsPerThread = 4096;  // private stack: 16 KiB per thread
constexpr std::int32_t kStackTop = static_cast<std::int32_t>((kGlobalWords + kLocalWordsPerThread) * 4);
constexpr std::int32_t kStackBottom = static_cast<std::int32_t>(kGlobalWords * 4);
constexpr std::size_t kNoPc = std::numeric_limits<std::size_t>::max();

struct Thread {
  std::array<std::int32_t, 32> regs{};
  std::int32_t hi = 0;
  std::int32_t lo = 0;
  bool exited = false;
};

struct StackEntry {
  std::size_t pc = 0;
  std::vector<bool> mask;
  std::size_t reconverge = kNoPc;
};

struct Warp {
  std::vector<std::size_t> lanes;  // thread ids
  std::vector<StackEntry> stack;
  bool done = false;
};

bool is_branch(Opcode opcode) { return opcode == Opcode::Beq || opcode == Opcode::Bne; }

// Immediate post-dominators on the instruction-level CFG.  Calls (jal) fall through to the return
// point and every jr flows to a virtual exit, so post-dominance is computed per function body.
std::vector<std::size_t> immediate_post_dominators(const LoadedProgram& program) {
  const std::size_t count = program.instructions.size();
  const std::size_t exit = count;
  std::vector<std::vector<std::size_t>> successors(count + 1);
  for (std::size_t pc = 0; pc < count; ++pc) {
    const auto& in = program.instructions[pc];
    if (is_branch(in.opcode)) {
      successors[pc] = {in.target, pc + 1};
    } else if (in.opcode == Opcode::J) {
      successors[pc] = {in.target};
    } else if (in.opcode == Opcode::Jr) {
      successors[pc] = {exit};
    } else {
      successors[pc] = {pc + 1 < count ? pc + 1 : exit};
    }
  }
  // Iterative post-dominator sets as bitsets (programs are small).
  std::vector<std::vector<bool>> pdom(count + 1, std::vector<bool>(count + 1, true));
  pdom[exit].assign(count + 1, false);
  pdom[exit][exit] = true;
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t reverse = count; reverse-- > 0;) {
      std::vector<bool> next(count + 1, true);
      for (const std::size_t successor : successors[reverse]) {
        for (std::size_t node = 0; node <= count; ++node) {
          next[node] = next[node] && pdom[successor][node];
        }
      }
      next[reverse] = true;
      if (next != pdom[reverse]) {
        pdom[reverse] = std::move(next);
        changed = true;
      }
    }
  }
  std::vector<std::size_t> ipdom(count, kNoPc);
  for (std::size_t pc = 0; pc < count; ++pc) {
    // the immediate post-dominator is the strict post-dominator post-dominated by all others
    for (std::size_t candidate = 0; candidate <= count; ++candidate) {
      if (candidate == pc || !pdom[pc][candidate]) {
        continue;
      }
      bool closest = true;
      for (std::size_t other = 0; other <= count && closest; ++other) {
        if (other != pc && other != candidate && pdom[pc][other] && !pdom[candidate][other]) {
          closest = false;
        }
      }
      if (closest) {
        ipdom[pc] = candidate == exit ? kNoPc : candidate;
        break;
      }
    }
  }
  return ipdom;
}

class Machine {
 public:
  Machine(const LoadedProgram& program, const RunOptions& options, RunResult& result)
      : program_(program),
        options_(options),
        result_(result),
        ipdom_(immediate_post_dominators(program)),
        global_(kGlobalWords, 0),
        local_(kLocalWordsPerThread * options.threads, 0),
        threads_(options.threads) {}

  bool run() {
    std::size_t entry = program_.entry_point;
    if (const auto kernel = program_.labels.find("kernel"); kernel != program_.labels.end()) {
      entry = kernel->second;
    }
    for (std::size_t id = 0; id < threads_.size(); ++id) {
      auto& regs = threads_[id].regs;
      regs[mips::isa::register_index(Register::A0)] = static_cast<std::int32_t>(id);
      regs[mips::isa::register_index(Register::A1)] = static_cast<std::int32_t>(threads_.size());
      regs[mips::isa::register_index(Register::SP)] = kStackTop - 64;
      regs[mips::isa::register_index(Register::RA)] = kHaltReturnAddress;
    }
    std::vector<Warp> warps;
    for (std::size_t first = 0; first < threads_.size(); first += options_.warp_size) {
      Warp warp;
      for (std::size_t lane = first; lane < std::min(first + options_.warp_size, threads_.size()); ++lane) {
        warp.lanes.push_back(lane);
      }
      warp.stack.push_back({entry, std::vector<bool>(warp.lanes.size(), true), kNoPc});
      warps.push_back(std::move(warp));
    }
    result_.warps = warps.size();
    result_.thread_results.assign(threads_.size(), 0);

    bool remaining = true;
    while (remaining) {
      remaining = false;
      for (std::size_t index = 0; index < warps.size(); ++index) {
        Warp& warp = warps[index];
        if (warp.done) {
          continue;
        }
        remaining = true;
        if (result_.warp_instructions >= options_.max_warp_instructions) {
          result_.error = "SIMT instruction limit exceeded";
          return false;
        }
        if (!step(warp, index)) {
          return false;
        }
        ++result_.cycles;
      }
    }
    for (std::size_t id = 0; id < threads_.size(); ++id) {
      result_.thread_results[id] = threads_[id].regs[mips::isa::register_index(Register::V0)];
    }
    return true;
  }

 private:
  std::int32_t& memory(std::size_t thread, std::int32_t address, bool& local, std::size_t& physical) {
    if (address < 0 || address % 4 != 0) {
      throw std::string("invalid word address " + std::to_string(address));
    }
    if (address >= kStackBottom && address < kStackTop) {
      const auto offset = static_cast<std::size_t>((address - kStackBottom) / 4);
      local = true;
      physical = offset * threads_.size() + thread;  // interleaved local memory
      return local_[physical];
    }
    if (static_cast<std::size_t>(address / 4) >= global_.size()) {
      throw std::string("global memory access out of bounds at " + std::to_string(address));
    }
    local = false;
    physical = static_cast<std::size_t>(address / 4);
    return global_[physical];
  }

  static std::int32_t& reg(Thread& thread, Register name) { return thread.regs[mips::isa::register_index(name)]; }

  // Executes a non-control instruction for one lane.
  void execute(Thread& thread, std::size_t thread_id, const LoadedInstruction& in, std::vector<std::size_t>& segments,
               bool& any_local) {
    auto u32 = [](std::int32_t value) { return static_cast<std::uint32_t>(value); };
    auto s32 = [](std::uint32_t value) { return static_cast<std::int32_t>(value); };
    std::int32_t rs = reg(thread, in.rs);
    std::int32_t rt = reg(thread, in.rt);
    switch (in.opcode) {
      case Opcode::Add:
      case Opcode::Addu: reg(thread, in.rd) = s32(u32(rs) + u32(rt)); break;
      case Opcode::Sub: reg(thread, in.rd) = s32(u32(rs) - u32(rt)); break;
      case Opcode::And: reg(thread, in.rd) = rs & rt; break;
      case Opcode::Or: reg(thread, in.rd) = rs | rt; break;
      case Opcode::Xor: reg(thread, in.rd) = rs ^ rt; break;
      case Opcode::Slt: reg(thread, in.rd) = rs < rt ? 1 : 0; break;
      case Opcode::Sltu: reg(thread, in.rd) = u32(rs) < u32(rt) ? 1 : 0; break;
      case Opcode::Sll: reg(thread, in.rd) = s32(u32(rt) << static_cast<unsigned>(in.immediate)); break;
      case Opcode::Addiu: reg(thread, in.rt) = s32(u32(rs) + u32(in.immediate)); break;
      case Opcode::Ori: reg(thread, in.rt) = s32(u32(rs) | (u32(in.immediate) & 0xffffU)); break;
      case Opcode::Xori: reg(thread, in.rt) = s32(u32(rs) ^ (u32(in.immediate) & 0xffffU)); break;
      case Opcode::Sltiu: reg(thread, in.rt) = u32(rs) < u32(in.immediate) ? 1 : 0; break;
      case Opcode::Lui: reg(thread, in.rt) = s32((u32(in.immediate) & 0xffffU) << 16U); break;
      case Opcode::Mult: {
        const std::int64_t wide = static_cast<std::int64_t>(rs) * rt;
        thread.lo = static_cast<std::int32_t>(wide & 0xffffffffLL);
        thread.hi = static_cast<std::int32_t>((wide >> 32) & 0xffffffffLL);
        break;
      }
      case Opcode::Div:
        if (rt == 0) {
          throw std::string("division by zero in thread " + std::to_string(thread_id));
        }
        thread.lo = rs / rt;
        thread.hi = rs % rt;
        break;
      case Opcode::Mflo: reg(thread, in.rd) = thread.lo; break;
      case Opcode::Mfhi: reg(thread, in.rd) = thread.hi; break;
      case Opcode::Lw:
      case Opcode::Sw: {
        bool local = false;
        std::size_t physical = 0;
        std::int32_t& word = memory(thread_id, rs + in.immediate, local, physical);
        if (in.opcode == Opcode::Lw) {
          reg(thread, in.rt) = word;
        } else {
          word = rt;
        }
        // 128-byte segments; local memory lives in a separate address range
        segments.push_back((physical * 4) / 128 + (local ? (1ULL << 40U) : 0U));
        any_local = any_local || local;
        break;
      }
      default:
        throw std::string("unexpected opcode in SIMT execute");
    }
    thread.regs[0] = 0;
  }

  bool step(Warp& warp, std::size_t warp_index) {
    // Pop entries that reached their reconvergence point or lost all their lanes.
    while (!warp.stack.empty()) {
      StackEntry& top = warp.stack.back();
      const bool empty = std::none_of(top.mask.begin(), top.mask.end(), [](bool on) { return on; });
      if (empty || (top.reconverge != kNoPc && top.pc == top.reconverge)) {
        warp.stack.pop_back();
        continue;
      }
      break;
    }
    if (warp.stack.empty()) {
      warp.done = true;
      return true;
    }
    StackEntry& top = warp.stack.back();
    if (top.pc >= program_.instructions.size()) {
      result_.error = "warp " + std::to_string(warp_index) + " ran past the program";
      return false;
    }
    const LoadedInstruction& in = program_.instructions[top.pc];
    std::size_t active = 0;
    for (std::size_t lane = 0; lane < warp.lanes.size(); ++lane) {
      active += top.mask[lane] ? 1U : 0U;
    }
    ++result_.warp_instructions;
    result_.thread_instructions += active;
    result_.max_stack_depth = std::max(result_.max_stack_depth, warp.stack.size());
    if (options_.trace && result_.trace_lines.size() < 5000) {
      std::ostringstream line;
      line << "trace[simt]: cycle=" << result_.cycles + 1 << " warp" << warp_index << " pc=" << top.pc << ' '
           << in.text << " mask=";
      for (std::size_t lane = 0; lane < warp.lanes.size(); ++lane) {
        line << (top.mask[lane] ? '1' : '0');
      }
      line << " depth=" << warp.stack.size();
      result_.trace_lines.push_back(line.str());
    }

    try {
      if (is_branch(in.opcode)) {
        std::vector<bool> taken(top.mask.size(), false);
        std::vector<bool> fallthrough(top.mask.size(), false);
        bool any_taken = false;
        bool any_fallthrough = false;
        for (std::size_t lane = 0; lane < warp.lanes.size(); ++lane) {
          if (!top.mask[lane]) {
            continue;
          }
          Thread& thread = threads_[warp.lanes[lane]];
          const bool equal = reg(thread, in.rs) == reg(thread, in.rt);
          const bool go = in.opcode == Opcode::Beq ? equal : !equal;
          (go ? taken : fallthrough)[lane] = true;
          any_taken = any_taken || go;
          any_fallthrough = any_fallthrough || !go;
        }
        if (!any_taken || !any_fallthrough) {
          ++result_.uniform_branches;
          top.pc = any_taken ? in.target : top.pc + 1;
          return true;
        }
        ++result_.divergent_branches;
        const std::size_t reconverge = ipdom_[top.pc];
        const std::size_t fall_pc = top.pc + 1;
        if (reconverge != kNoPc && reconverge == top.reconverge) {
          // Both paths meet where the current entry already reconverges (e.g. a loop exit): no
          // separate continuation entry is needed, which keeps loop divergence at bounded depth.
          const std::size_t target = in.target;
          warp.stack.pop_back();
          warp.stack.push_back({fall_pc, fallthrough, reconverge});
          warp.stack.push_back({target, taken, reconverge});
          return true;
        }
        top.pc = reconverge == kNoPc ? program_.instructions.size() : reconverge;  // continuation
        if (reconverge == kNoPc) {
          // paths only meet at function exit: the continuation entry is empty after both run
          std::fill(top.mask.begin(), top.mask.end(), false);
        }
        warp.stack.push_back({fall_pc, fallthrough, reconverge});
        warp.stack.push_back({in.target, taken, reconverge});
        return true;
      }
      if (in.opcode == Opcode::J) {
        top.pc = in.target;
        return true;
      }
      if (in.opcode == Opcode::Jal) {
        for (std::size_t lane = 0; lane < warp.lanes.size(); ++lane) {
          if (top.mask[lane]) {
            reg(threads_[warp.lanes[lane]], Register::RA) = static_cast<std::int32_t>(top.pc + 1);
          }
        }
        top.pc = in.target;
        return true;
      }
      if (in.opcode == Opcode::Jr) {
        std::optional<std::int32_t> target;
        for (std::size_t lane = 0; lane < warp.lanes.size(); ++lane) {
          if (!top.mask[lane]) {
            continue;
          }
          const std::int32_t lane_target = reg(threads_[warp.lanes[lane]], in.rs);
          if (target.has_value() && *target != lane_target) {
            result_.error = "divergent indirect jump in warp " + std::to_string(warp_index);
            return false;
          }
          target = lane_target;
        }
        if (target == kHaltReturnAddress) {
          for (std::size_t lane = 0; lane < warp.lanes.size(); ++lane) {
            if (top.mask[lane]) {
              threads_[warp.lanes[lane]].exited = true;
            }
          }
          // exited lanes leave every stack entry
          for (auto& entry : warp.stack) {
            for (std::size_t lane = 0; lane < warp.lanes.size(); ++lane) {
              if (threads_[warp.lanes[lane]].exited) {
                entry.mask[lane] = false;
              }
            }
          }
          return true;
        }
        if (!target.has_value() || *target < 0 || static_cast<std::size_t>(*target) >= program_.instructions.size()) {
          result_.error = "jr target out of range";
          return false;
        }
        top.pc = static_cast<std::size_t>(*target);
        return true;
      }
      std::vector<std::size_t> segments;
      bool any_local = false;
      for (std::size_t lane = 0; lane < warp.lanes.size(); ++lane) {
        if (top.mask[lane]) {
          execute(threads_[warp.lanes[lane]], warp.lanes[lane], in, segments, any_local);
        }
      }
      if (in.opcode == Opcode::Lw || in.opcode == Opcode::Sw) {
        std::sort(segments.begin(), segments.end());
        ++result_.memory_requests;
        result_.memory_transactions += static_cast<std::size_t>(std::unique(segments.begin(), segments.end()) - segments.begin());
        (any_local ? result_.local_requests : result_.global_requests) += 1;
      }
      top.pc += 1;
    } catch (const std::string& error) {
      result_.error = error;
      return false;
    }
    return true;
  }

  const LoadedProgram& program_;
  const RunOptions& options_;
  RunResult& result_;
  std::vector<std::size_t> ipdom_;
  std::vector<std::int32_t> global_;
  std::vector<std::int32_t> local_;
  std::vector<Thread> threads_;
};

}  // namespace

RunResult run_kernel(const LoadedProgram& program, const RunOptions& options) {
  RunResult result;
  if (options.threads == 0 || options.warp_size == 0) {
    result.error = "threads and warp size must be positive";
    return result;
  }
  Machine machine(program, options, result);
  result.success = machine.run();
  return result;
}

}  // namespace nexus::sim::simt
