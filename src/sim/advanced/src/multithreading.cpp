#include "nexus/sim/advanced/multithreading.hpp"

#include <algorithm>
#include <limits>

#include "nexus/sim/functional/interpreter.hpp"

namespace nexus::sim::advanced {

namespace {

using nexus::mips::isa::Opcode;

struct ThreadState {
  std::vector<functional::Event> events;
  std::size_t next = 0;
  std::size_t ready = 0;       // first cycle at which the next instruction may issue
  bool waiting_on_miss = false;
  bool done() const { return next >= events.size(); }
};

}  // namespace

std::string policy_name(MtPolicy policy) {
  switch (policy) {
    case MtPolicy::Single: return "single";
    case MtPolicy::Coarse: return "coarse";
    case MtPolicy::Fine: return "fine";
    case MtPolicy::Smt: return "smt";
  }
  return "?";
}

MtResult run_multithreaded(const std::vector<const mips::loader::LoadedProgram*>& programs, const MtOptions& options) {
  MtResult result;
  if (programs.empty()) {
    result.error = "no threads";
    return result;
  }
  std::vector<ThreadState> threads(programs.size());
  for (std::size_t t = 0; t < programs.size(); ++t) {
    functional::RunOptions run_options;
    run_options.record_events = true;
    run_options.max_instructions = options.max_instructions;
    auto run = functional::run_program(*programs[t], run_options);
    if (!run.success) {
      result.error = "thread " + std::to_string(t) + ": " + run.error;
      return result;
    }
    threads[t].events = std::move(run.events);
    result.threads.push_back(MtThread{.exit_code = run.exit_code, .instructions = threads[t].events.size()});
  }

  std::vector<std::int64_t> tags(options.cache_lines, -1);
  auto latency_of = [&](std::size_t t, const functional::Event& e, bool& miss) -> std::size_t {
    miss = false;
    switch (e.opcode) {
      case Opcode::Mult: return 4;
      case Opcode::Div: return 12;
      case Opcode::Lw:
      case Opcode::Sw: {
        if (e.address < 0) return 1;
        // separate address spaces: the thread number selects a distinct region of physical memory
        const std::int64_t block = ((static_cast<std::int64_t>(t) << 26) + e.address / 4) /
                                   static_cast<std::int64_t>(options.line_words);
        const auto line = static_cast<std::size_t>(block % static_cast<std::int64_t>(options.cache_lines));
        if (tags[line] == block) {
          ++result.cache_hits;
          return 1;
        }
        tags[line] = block;
        ++result.cache_misses;
        miss = true;
        return 1 + options.miss_penalty;
      }
      default: return 1;
    }
  };
  auto issue = [&](std::size_t t, std::size_t cycle) {
    auto& th = threads[t];
    bool miss = false;
    const auto lat = latency_of(t, th.events[th.next], miss);
    th.ready = cycle + lat;
    th.waiting_on_miss = miss;
    if (miss) ++result.threads[t].misses;
    ++th.next;
    ++result.issued;
    if (th.done()) result.threads[t].finish_cycle = th.ready;
  };
  auto all_done = [&] {
    return std::all_of(threads.begin(), threads.end(), [](const ThreadState& th) { return th.done(); });
  };

  const std::size_t n = threads.size();
  std::size_t cycle = 0;
  std::size_t current = 0;      // single / coarse: the running thread
  std::size_t rotate = 0;       // fine / smt: round-robin pointer
  std::size_t blocked_until = 0;  // coarse: pipeline refill after a switch
  while (!all_done()) {
    std::size_t issued_now = 0;
    switch (options.policy) {
      case MtPolicy::Single: {
        while (threads[current].done()) ++current;
        auto& th = threads[current];
        if (th.ready <= cycle) {
          // the next thread starts only after the previous one has finished completely
          if (current == 0 || threads[current - 1].ready <= cycle) {
            issue(current, cycle);
            issued_now = 1;
          }
        }
        break;
      }
      case MtPolicy::Coarse: {
        if (cycle < blocked_until) break;
        if (threads[current].done() || (threads[current].waiting_on_miss && threads[current].ready > cycle)) {
          // switch on a miss (or when the thread has finished) to the next thread that has work
          for (std::size_t k = 1; k <= n; ++k) {
            const std::size_t candidate = (current + k) % n;
            if (!threads[candidate].done() && candidate != current &&
                threads[candidate].ready <= cycle + options.switch_penalty) {
              current = candidate;
              ++result.switches;
              blocked_until = cycle + options.switch_penalty;
              break;
            }
          }
          if (cycle < blocked_until) break;
        }
        auto& th = threads[current];
        if (!th.done() && th.ready <= cycle) {
          issue(current, cycle);
          issued_now = 1;
        }
        break;
      }
      case MtPolicy::Fine:
      case MtPolicy::Smt: {
        const std::size_t width = options.policy == MtPolicy::Fine ? 1 : std::max<std::size_t>(1, options.issue_width);
        for (std::size_t k = 0; k < n && issued_now < width; ++k) {
          const std::size_t t = (rotate + k) % n;
          if (!threads[t].done() && threads[t].ready <= cycle) {
            issue(t, cycle);
            ++issued_now;
            rotate = (t + 1) % n;
          }
        }
        break;
      }
    }
    (void)issued_now;
    ++cycle;
  }
  for (const auto& th : threads) result.cycles = std::max(result.cycles, th.ready);
  result.cycles = std::max(result.cycles, cycle);
  result.success = true;
  return result;
}

}  // namespace nexus::sim::advanced
