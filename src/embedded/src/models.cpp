#include "nexus/embedded/models.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace nexus::embedded {

double utilization(const std::vector<Task>& tasks) {
  double u = 0.0;
  for (const auto& t : tasks) {
    u += static_cast<double>(t.wcet) / t.period;
  }
  return u;
}

double liu_layland_bound(std::size_t n) {
  const double k = static_cast<double>(n);
  return k * (std::pow(2.0, 1.0 / k) - 1.0);
}

bool rm_liu_layland(const std::vector<Task>& tasks) { return utilization(tasks) <= liu_layland_bound(tasks.size()) + 1e-12; }

bool rm_hyperbolic(const std::vector<Task>& tasks) {
  double p = 1.0;
  for (const auto& t : tasks) {
    p *= static_cast<double>(t.wcet) / t.period + 1.0;
  }
  return p <= 2.0 + 1e-12;
}

std::vector<std::optional<int>> response_times(const std::vector<Task>& tasks) {
  std::vector<std::optional<int>> out;
  for (std::size_t i = 0; i < tasks.size(); ++i) {
    int r = tasks[i].wcet;
    for (;;) {
      int next = tasks[i].wcet;
      for (std::size_t j = 0; j < i; ++j) {
        next += ((r + tasks[j].period - 1) / tasks[j].period) * tasks[j].wcet;  // ceil(R / T_j) C_j
      }
      if (next > tasks[i].effective_deadline()) {
        out.emplace_back(std::nullopt);
        break;
      }
      if (next == r) {
        out.emplace_back(r);
        break;
      }
      r = next;
    }
  }
  return out;
}

namespace {

long hyperperiod_of(const std::vector<Task>& tasks) {
  long h = 1;
  for (const auto& t : tasks) {
    h = std::lcm(h, static_cast<long>(t.period));
    if (h > 1000000) {
      throw std::runtime_error("hyperperiod too large");
    }
  }
  return h;
}

}  // namespace

bool edf_schedulable(const std::vector<Task>& tasks) {
  if (utilization(tasks) > 1.0 + 1e-12) {
    return false;
  }
  const bool implicit = std::all_of(tasks.begin(), tasks.end(), [](const Task& t) { return t.effective_deadline() == t.period; });
  if (implicit) {
    return true;
  }
  // processor-demand criterion at every absolute deadline up to the hyperperiod
  const long h = hyperperiod_of(tasks);
  for (long t = 1; t <= h; ++t) {
    long demand = 0;
    for (const auto& k : tasks) {
      const long d = k.effective_deadline();
      if (t >= d) {
        demand += ((t - d) / k.period + 1) * k.wcet;
      }
    }
    if (demand > t) {
      return false;
    }
  }
  return true;
}

RtTrace simulate_rt(const std::vector<Task>& tasks, RtPolicy policy) {
  RtTrace tr;
  tr.hyperperiod = hyperperiod_of(tasks);
  struct Job {
    std::size_t task;
    long release, deadline;
    int remaining;
  };
  std::vector<Job> active;
  // run one extra hyperperiod's worth of deadlines so that jobs released near the end are judged
  long horizon = tr.hyperperiod;
  for (const auto& t : tasks) {
    horizon = std::max(horizon, tr.hyperperiod - t.period + t.effective_deadline());
  }
  for (long now = 0; now < horizon; ++now) {
    if (now < tr.hyperperiod) {
      for (std::size_t i = 0; i < tasks.size(); ++i) {
        if (now % tasks[i].period == 0) {
          active.push_back(Job{i, now, now + tasks[i].effective_deadline(), tasks[i].wcet});
        }
      }
    }
    // deadline misses
    for (auto it = active.begin(); it != active.end();) {
      if (now >= it->deadline) {
        tr.missed.push_back(tasks[it->task].name + "@" + std::to_string(it->deadline));
        it = active.erase(it);
      } else {
        ++it;
      }
    }
    auto key = [&](const Job& j) -> std::pair<long, std::size_t> {
      switch (policy) {
        case RtPolicy::RateMonotonic:
          return {tasks[j.task].period, j.task};
        case RtPolicy::DeadlineMonotonic:
          return {tasks[j.task].effective_deadline(), j.task};
        case RtPolicy::EDF:
          return {j.deadline, j.task};
      }
      return {0, 0};
    };
    if (active.empty()) {
      if (now < tr.hyperperiod) {
        tr.timeline.emplace_back("");
      }
      continue;
    }
    auto run = std::min_element(active.begin(), active.end(), [&](const Job& a, const Job& b) { return key(a) < key(b); });
    if (now < tr.hyperperiod) {
      tr.timeline.push_back(tasks[run->task].name);
    }
    if (--run->remaining == 0) {
      active.erase(run);
    }
  }
  for (const auto& j : active) {  // unfinished at the end of the horizon
    tr.missed.push_back(tasks[j.task].name + "@" + std::to_string(j.deadline));
  }
  return tr;
}

InversionResult priority_inversion_demo(int medium_work) {
  // L (low) arrives at 0 and enters a 3-unit critical section at once; H (high) arrives at 1 and needs the lock for
  // 1 unit; M (medium) arrives at 2 and computes for `medium_work` units without the lock.
  InversionResult r{};
  for (int inherit = 0; inherit < 2; ++inherit) {
    int l_left = 3, h_left = 1, m_left = medium_work;
    bool lock_held_by_l = true;
    int h_finish = -1, h_blocked = 0;
    for (int t = 0; t < 1000 && h_finish < 0; ++t) {
      const bool h_ready = t >= 1 && h_left > 0;
      const bool m_ready = t >= 2 && m_left > 0;
      const bool l_ready = l_left > 0;
      // priorities: H 3, M 2, L 1 (L runs at 3 while it blocks H under priority inheritance)
      const bool h_can_run = h_ready && !lock_held_by_l;
      const int l_prio = (inherit != 0 && h_ready && lock_held_by_l) ? 3 : 1;
      std::string who;
      if (h_can_run) {
        who = "H";
      } else if (m_ready && l_prio < 2) {
        who = "M";
      } else if (l_ready) {
        who = "L";
      } else if (m_ready) {
        who = "M";
      }
      if (h_ready && !h_can_run) {
        ++h_blocked;
      }
      if (who == "H" && --h_left == 0) {
        h_finish = t + 1;
      } else if (who == "M") {
        --m_left;
      } else if (who == "L" && --l_left == 0) {
        lock_held_by_l = false;
      }
    }
    (inherit != 0 ? r.high_finish_inheritance : r.high_finish_no_protocol) = h_finish;
    (inherit != 0 ? r.high_blocking_inheritance : r.high_blocking_no_protocol) = h_blocked;
  }
  return r;
}

// ---------------------------------------------------------------- synchronous dataflow

std::optional<std::vector<long>> repetition_vector(const SdfGraph& g) {
  const std::size_t n = g.actors.size();
  // rational q_i = num / den, propagated over the (undirected) graph
  std::vector<long> num(n, 0), den(n, 1);
  std::vector<bool> seen(n, false);
  for (std::size_t root = 0; root < n; ++root) {
    if (seen[root]) {
      continue;
    }
    seen[root] = true;
    num[root] = 1;
    std::deque<std::size_t> queue{root};
    while (!queue.empty()) {
      const auto a = queue.front();
      queue.pop_front();
      for (const auto& e : g.edges) {
        // q[from] * produce = q[to] * consume
        std::size_t other;
        long on, od;
        if (e.from == a) {
          other = e.to;
          on = num[a] * e.produce;
          od = den[a] * e.consume;
        } else if (e.to == a) {
          other = e.from;
          on = num[a] * e.consume;
          od = den[a] * e.produce;
        } else {
          continue;
        }
        const long gcd = std::gcd(on, od);
        on /= gcd;
        od /= gcd;
        if (!seen[other]) {
          seen[other] = true;
          num[other] = on;
          den[other] = od;
          queue.push_back(other);
        } else if (num[other] * od != on * den[other]) {
          return std::nullopt;  // inconsistent sample rates
        }
      }
    }
  }
  long l = 1;
  for (const auto d : den) {
    l = std::lcm(l, d);
  }
  std::vector<long> q(n);
  long g_all = 0;
  for (std::size_t i = 0; i < n; ++i) {
    q[i] = num[i] * (l / den[i]);
    g_all = std::gcd(g_all, q[i]);
  }
  for (auto& x : q) {
    x /= g_all;
  }
  return q;
}

std::optional<std::vector<std::size_t>> sdf_schedule(const SdfGraph& g, std::vector<long>* buffer_max) {
  const auto q = repetition_vector(g);
  if (!q) {
    return std::nullopt;
  }
  std::vector<long> tokens, peak;
  for (const auto& e : g.edges) {
    tokens.push_back(e.initial);
    peak.push_back(e.initial);
  }
  std::vector<long> left = *q;
  std::vector<std::size_t> order;
  long total = std::accumulate(left.begin(), left.end(), 0L);
  while (total > 0) {
    bool fired = false;
    for (std::size_t a = 0; a < g.actors.size() && !fired; ++a) {
      if (left[a] == 0) {
        continue;
      }
      bool ready = true;
      for (std::size_t k = 0; k < g.edges.size(); ++k) {
        if (g.edges[k].to == a && tokens[k] < g.edges[k].consume) {
          ready = false;
        }
      }
      if (!ready) {
        continue;
      }
      for (std::size_t k = 0; k < g.edges.size(); ++k) {
        if (g.edges[k].to == a) tokens[k] -= g.edges[k].consume;
      }
      for (std::size_t k = 0; k < g.edges.size(); ++k) {
        if (g.edges[k].from == a) {
          tokens[k] += g.edges[k].produce;
          peak[k] = std::max(peak[k], tokens[k]);
        }
      }
      --left[a];
      --total;
      order.push_back(a);
      fired = true;
    }
    if (!fired) {
      return std::nullopt;  // deadlock: a cycle lacks initial tokens
    }
  }
  if (buffer_max != nullptr) {
    *buffer_max = peak;
  }
  return order;
}

// ---------------------------------------------------------------- Petri nets

Reachability explore(const PetriNet& net, std::size_t max_markings) {
  Reachability r;
  const std::size_t nt = net.transitions.size();
  std::set<std::vector<int>> seen;
  std::vector<std::vector<int>> path;
  std::function<void(const std::vector<int>&)> dfs = [&](const std::vector<int>& m) {
    if (!seen.insert(m).second) {
      return;
    }
    if (seen.size() > max_markings) {
      throw std::runtime_error("petri: too many markings");
    }
    r.max_tokens = std::max(r.max_tokens, *std::max_element(m.begin(), m.end()));
    // unboundedness: m strictly covers a marking on the path from the initial marking
    for (const auto& anc : path) {
      bool covers = true, strictly = false;
      for (std::size_t p = 0; p < m.size(); ++p) {
        covers = covers && m[p] >= anc[p];
        strictly = strictly || m[p] > anc[p];
      }
      if (covers && strictly) {
        r.bounded = false;
        return;  // do not expand an unbounded branch further
      }
    }
    bool any = false;
    path.push_back(m);
    for (std::size_t t = 0; t < nt; ++t) {
      bool enabled = true;
      for (std::size_t p = 0; p < m.size(); ++p) {
        enabled = enabled && m[p] >= net.pre[t][p];
      }
      if (!enabled) {
        continue;
      }
      any = true;
      std::vector<int> next = m;
      for (std::size_t p = 0; p < m.size(); ++p) {
        next[p] += net.post[t][p] - net.pre[t][p];
      }
      dfs(next);
    }
    path.pop_back();
    if (!any) {
      ++r.deadlocks;
      r.deadlock_markings.push_back(m);
    }
  };
  dfs(net.initial);
  r.markings = seen.size();
  return r;
}

}  // namespace nexus::embedded
