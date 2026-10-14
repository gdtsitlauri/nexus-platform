#include "nexus/os/scheduling.hpp"

#include <algorithm>
#include <deque>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace nexus::os {

const char* state_name(ProcState s) {
  switch (s) {
    case ProcState::New:
      return "new";
    case ProcState::Ready:
      return "ready";
    case ProcState::Running:
      return "running";
    case ProcState::Blocked:
      return "blocked";
    case ProcState::ReadySuspend:
      return "ready/suspend";
    case ProcState::BlockedSuspend:
      return "blocked/suspend";
    case ProcState::Exit:
      return "exit";
  }
  return "?";
}

bool legal_transition(ProcState from, ProcState to) {
  using S = ProcState;
  switch (from) {
    case S::New:
      return to == S::Ready || to == S::ReadySuspend;  // admit
    case S::Ready:
      return to == S::Running || to == S::ReadySuspend || to == S::Exit;  // dispatch, suspend, release by parent
    case S::Running:
      return to == S::Ready || to == S::Blocked || to == S::Exit || to == S::ReadySuspend;  // timeout, wait, release
    case S::Blocked:
      return to == S::Ready || to == S::BlockedSuspend || to == S::Exit;  // event occurs, suspend
    case S::ReadySuspend:
      return to == S::Ready;  // activate
    case S::BlockedSuspend:
      return to == S::ReadySuspend || to == S::Blocked;  // event occurs, activate
    case S::Exit:
      return false;
  }
  return false;
}

bool ProcessControlBlock::transition(ProcState to) {
  if (!legal_transition(state, to)) {
    return false;
  }
  history.emplace_back(state, to);
  state = to;
  return true;
}

Schedule schedule(const std::vector<Job>& jobs, Policy policy, int quantum, bool exponential) {
  if (quantum < 1) {
    throw std::runtime_error("schedule: quantum must be positive");
  }
  const std::size_t n = jobs.size();
  std::vector<int> remaining(n), first_run(n, -1), finish(n, -1), level(n, 0);
  for (std::size_t i = 0; i < n; ++i) {
    if (jobs[i].service < 1) {
      throw std::runtime_error("schedule: service times must be positive");
    }
    remaining[i] = jobs[i].service;
  }
  // arrival order: by time, then input order
  std::vector<std::size_t> by_arrival(n);
  std::iota(by_arrival.begin(), by_arrival.end(), 0);
  std::stable_sort(by_arrival.begin(), by_arrival.end(),
                   [&](std::size_t a, std::size_t b) { return jobs[a].arrival < jobs[b].arrival; });
  std::size_t next_arrival = 0;
  std::deque<std::size_t> queue;                // FCFS / RR
  std::vector<std::deque<std::size_t>> levels;  // feedback queues
  std::vector<std::size_t> ready;               // selection policies
  const std::size_t none = n;
  std::size_t running = none;
  int slice_used = 0;
  Schedule s;
  static const char* names[] = {"FCFS", "RR", "SPN", "SRT", "HRRN", "Priority", "Priority (preemptive)", "Feedback"};
  s.policy = names[static_cast<int>(policy)];
  if (policy == Policy::RR) {
    s.policy += " q=" + std::to_string(quantum);
  }
  if (policy == Policy::Feedback) {
    s.policy += exponential ? " q=" + std::to_string(quantum) + "*2^i" : " q=" + std::to_string(quantum);
  }
  auto add_ready = [&](std::size_t j) {
    switch (policy) {
      case Policy::FCFS:
      case Policy::RR:
        queue.push_back(j);
        break;
      case Policy::Feedback:
        if (levels.size() <= static_cast<std::size_t>(level[j])) {
          levels.resize(static_cast<std::size_t>(level[j]) + 1);
        }
        levels[static_cast<std::size_t>(level[j])].push_back(j);
        break;
      default:
        ready.push_back(j);
    }
  };
  auto quantum_of = [&](std::size_t j) {
    if (policy == Policy::Feedback && exponential) {
      return quantum << std::min(level[j], 20);
    }
    return quantum;
  };
  auto better = [&](std::size_t a, std::size_t b, int now) {  // true if a should run before b
    auto key = [&](std::size_t j) -> double {
      switch (policy) {
        case Policy::SPN:
          return jobs[j].service;
        case Policy::SRT:
          return remaining[j];
        case Policy::HRRN: {
          const double w = now - jobs[j].arrival - (jobs[j].service - remaining[j]);
          return -(w + jobs[j].service) / jobs[j].service;  // highest response ratio first
        }
        default:
          return jobs[j].priority;
      }
    };
    const double ka = key(a), kb = key(b);
    if (ka != kb) {
      return ka < kb;
    }
    if (jobs[a].arrival != jobs[b].arrival) {
      return jobs[a].arrival < jobs[b].arrival;
    }
    return a < b;
  };
  auto pick = [&](int now) -> std::size_t {
    switch (policy) {
      case Policy::FCFS:
      case Policy::RR: {
        if (queue.empty()) {
          return none;
        }
        const auto j = queue.front();
        queue.pop_front();
        return j;
      }
      case Policy::Feedback:
        for (auto& q : levels) {
          if (!q.empty()) {
            const auto j = q.front();
            q.pop_front();
            return j;
          }
        }
        return none;
      default: {
        if (ready.empty()) {
          return none;
        }
        auto it = ready.begin();
        for (auto k = ready.begin(); k != ready.end(); ++k) {
          if (better(*k, *it, now)) {
            it = k;
          }
        }
        const auto j = *it;
        ready.erase(it);
        return j;
      }
    }
  };
  const bool preemptive_select = policy == Policy::SRT || policy == Policy::PriorityPreemptive;
  std::size_t done = 0;
  std::size_t last_runner = none;
  for (int t = 0; done < n; ++t) {
    // 1. arrivals at time t
    while (next_arrival < n && jobs[by_arrival[next_arrival]].arrival <= t) {
      add_ready(by_arrival[next_arrival++]);
    }
    // 2. end of the running job's slice: completion or time-out
    if (running != none) {
      if (remaining[running] == 0) {
        finish[running] = t;
        ++done;
        running = none;
      } else if ((policy == Policy::RR || policy == Policy::Feedback) && slice_used >= quantum_of(running)) {
        const bool others = policy == Policy::RR ? !queue.empty()
                                                 : std::any_of(levels.begin(), levels.end(),
                                                               [](const auto& q) { return !q.empty(); });
        if (policy == Policy::Feedback) {
          ++level[running];  // demoted after using a full quantum
        }
        if (others || policy == Policy::Feedback) {
          add_ready(running);
          running = none;
        } else {
          slice_used = 0;  // alone in the system: RR keeps running it
        }
      } else if (preemptive_select && !ready.empty()) {
        // preempt if a ready job is now better
        std::size_t best = ready[0];
        for (const auto j : ready) {
          if (better(j, best, t)) {
            best = j;
          }
        }
        if (better(best, running, t)) {
          add_ready(running);
          running = none;
        }
      }
    }
    if (done == n) {
      break;
    }
    // 3. dispatch
    if (running == none) {
      running = pick(t);
      slice_used = 0;
      if (running != none && first_run[running] < 0) {
        first_run[running] = t;
      }
    }
    // 4. run one time unit
    const std::string who = running == none ? "" : jobs[running].name;
    if (s.gantt.empty() || s.gantt.back().name != who || s.gantt.back().end != t) {
      if (running != none && last_runner != none && last_runner != running) {
        ++s.context_switches;
      }
      s.gantt.push_back(Slice{who, t, t + 1});
    } else {
      s.gantt.back().end = t + 1;
    }
    if (running != none) {
      --remaining[running];
      ++slice_used;
      last_runner = running;
    }
  }
  double tat = 0, norm = 0, wait = 0;
  for (std::size_t i = 0; i < n; ++i) {
    JobResult r;
    r.name = jobs[i].name;
    r.finish = finish[i];
    r.turnaround = finish[i] - jobs[i].arrival;
    r.normalized = static_cast<double>(r.turnaround) / jobs[i].service;
    r.waiting = r.turnaround - jobs[i].service;
    r.response = first_run[i] - jobs[i].arrival;
    tat += r.turnaround;
    norm += r.normalized;
    wait += r.waiting;
    s.jobs.push_back(r);
  }
  if (n > 0) {
    s.mean_turnaround = tat / static_cast<double>(n);
    s.mean_normalized = norm / static_cast<double>(n);
    s.mean_waiting = wait / static_cast<double>(n);
  }
  return s;
}

std::string format_schedule(const Schedule& s) {
  std::ostringstream out;
  out << s.policy << "\n  Gantt:";
  for (const auto& g : s.gantt) {
    out << " [" << g.start << '-' << g.end << ' ' << (g.name.empty() ? "idle" : g.name) << ']';
  }
  out << "\n  job  finish  turnaround  Tr/Ts  waiting  response\n";
  for (const auto& j : s.jobs) {
    out << "  " << j.name << "\t" << j.finish << "\t" << j.turnaround << "\t" << j.normalized << "\t" << j.waiting
        << "\t" << j.response << '\n';
  }
  out << "  mean turnaround " << s.mean_turnaround << ", mean Tr/Ts " << s.mean_normalized << ", mean waiting "
      << s.mean_waiting << ", context switches " << s.context_switches << '\n';
  return out.str();
}

double best_nonpreemptive_mean_turnaround(const std::vector<Job>& jobs) {
  if (jobs.size() > 8) {
    throw std::runtime_error("best_nonpreemptive_mean_turnaround: at most 8 jobs");
  }
  std::vector<std::size_t> order(jobs.size());
  std::iota(order.begin(), order.end(), 0);
  double best = 1e300;
  do {
    int t = 0;
    double total = 0;
    for (const auto j : order) {
      t = std::max(t, jobs[j].arrival) + jobs[j].service;
      total += t - jobs[j].arrival;
    }
    best = std::min(best, total / static_cast<double>(jobs.size()));
  } while (std::next_permutation(order.begin(), order.end()));
  return best;
}

}  // namespace nexus::os
