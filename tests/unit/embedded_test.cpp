// NEY604 embedded-system models: schedulability tests against simulation, SDF and Petri-net analysis.
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <string>

#include "nexus/embedded/models.hpp"

namespace em = nexus::embedded;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void real_time() {
  expect(std::abs(em::liu_layland_bound(1) - 1.0) < 1e-12 && std::abs(em::liu_layland_bound(3) - 0.7798) < 1e-4,
         "Liu-Layland bound");
  // U = 0.833 > 0.78 (Liu-Layland fails) but the task set is RM-schedulable (exact analysis)
  const std::vector<em::Task> a = {{"t1", 1, 4, 0}, {"t2", 2, 6, 0}, {"t3", 3, 12, 0}};
  expect(!em::rm_liu_layland(a), "Liu-Layland test is only sufficient");
  const auto ra = em::response_times(a);
  expect(ra[0] == 1 && ra[1] == 3 && ra[2] == 10, "response times 1, 3, 10");
  expect(em::simulate_rt(a, em::RtPolicy::RateMonotonic).missed.empty(), "RM simulation meets every deadline");
  // U = 0.971: RM misses, EDF does not
  const std::vector<em::Task> b = {{"t1", 2, 5, 0}, {"t2", 4, 7, 0}};
  expect(!em::response_times(b)[1].has_value(), "RTA: the second task misses under RM");
  expect(!em::simulate_rt(b, em::RtPolicy::RateMonotonic).missed.empty(), "RM simulation misses a deadline");
  expect(em::edf_schedulable(b) && em::simulate_rt(b, em::RtPolicy::EDF).missed.empty(), "EDF schedules it");
  // random task sets: the exact tests agree with simulation over the hyperperiod
  std::mt19937 rng(604);
  std::uniform_int_distribution<int> period_pick(0, 5);
  const int periods[] = {4, 5, 6, 8, 10, 12};
  int both = 0;
  for (int trial = 0; trial < 400; ++trial) {
    std::vector<em::Task> ts;
    for (int i = 0; i < 3; ++i) {
      const int p = periods[period_pick(rng)];
      std::uniform_int_distribution<int> c(1, std::max(1, p / 2));
      ts.push_back({"t" + std::to_string(i), c(rng), p, 0});
    }
    std::stable_sort(ts.begin(), ts.end(), [](const em::Task& x, const em::Task& y) { return x.period < y.period; });
    bool rta_ok = true;
    for (const auto& r : em::response_times(ts)) rta_ok = rta_ok && r.has_value();
    const bool rm_sim = em::simulate_rt(ts, em::RtPolicy::RateMonotonic).missed.empty();
    expect(rta_ok == rm_sim, "response-time analysis is exact for synchronous periodic tasks");
    expect(em::edf_schedulable(ts) == em::simulate_rt(ts, em::RtPolicy::EDF).missed.empty(), "EDF test exact (U <= 1)");
    if (em::rm_liu_layland(ts)) expect(rm_sim, "Liu-Layland is sufficient");
    if (em::rm_hyperbolic(ts)) expect(rm_sim, "the hyperbolic bound is sufficient");
    both += rm_sim ? 1 : 0;
  }
  expect(both > 50 && both < 380, "random sets mix schedulable and unschedulable cases");
  // constrained deadlines: deadline-monotonic
  const std::vector<em::Task> c = {{"t1", 1, 10, 3}, {"t2", 2, 5, 5}};
  expect(em::simulate_rt(c, em::RtPolicy::DeadlineMonotonic).missed.empty(), "deadline-monotonic meets D < T");
  const auto inv = em::priority_inversion_demo(5);
  expect(inv.high_finish_no_protocol == 9 && inv.high_finish_inheritance == 4,
         "priority inheritance bounds the blocking of the high-priority task (9 -> 4)");
}

void dataflow() {
  em::SdfGraph g;
  g.actors = {"A", "B", "C"};
  g.edges = {{0, 1, 2, 3, 0}, {1, 2, 1, 2, 0}};
  const auto q = em::repetition_vector(g);
  expect(q && *q == std::vector<long>({3, 2, 1}), "repetition vector (3, 2, 1)");
  std::vector<long> buffers;
  const auto s = em::sdf_schedule(g, &buffers);
  expect(s && s->size() == 6, "one iteration fires 6 actors");
  em::SdfGraph bad = g;
  bad.edges.push_back({0, 2, 1, 1, 0});
  expect(!em::repetition_vector(bad).has_value(), "inconsistent sample rates detected");
  em::SdfGraph loop;
  loop.actors = {"X", "Y"};
  loop.edges = {{0, 1, 1, 1, 0}, {1, 0, 1, 1, 0}};
  expect(!em::sdf_schedule(loop).has_value(), "a cycle without initial tokens deadlocks");
  loop.edges[1].initial = 1;
  expect(em::sdf_schedule(loop).has_value(), "one delay token makes the cycle live");
}

void petri() {
  // producer/consumer with a two-slot buffer: places free, full, producer ready, consumer ready
  em::PetriNet pc;
  pc.places = {"free", "full"};
  pc.transitions = {"produce", "consume"};
  pc.pre = {{1, 0}, {0, 1}};
  pc.post = {{0, 1}, {1, 0}};
  pc.initial = {2, 0};
  const auto r = em::explore(pc);
  expect(r.bounded && r.deadlocks == 0 && r.markings == 3 && r.max_tokens == 2, "bounded buffer net: 3 markings, live");
  // without the capacity place the buffer is unbounded
  em::PetriNet unb;
  unb.places = {"full"};
  unb.transitions = {"produce"};
  unb.pre = {{0}};
  unb.post = {{1}};
  unb.initial = {0};
  expect(!em::explore(unb).bounded, "unbounded net detected");
  // mutual exclusion: idle1, cs1, idle2, cs2, lock
  em::PetriNet mx;
  mx.places = {"idle1", "cs1", "idle2", "cs2", "lock"};
  mx.transitions = {"enter1", "exit1", "enter2", "exit2"};
  mx.pre = {{1, 0, 0, 0, 1}, {0, 1, 0, 0, 0}, {0, 0, 1, 0, 1}, {0, 0, 0, 1, 0}};
  mx.post = {{0, 1, 0, 0, 0}, {1, 0, 0, 0, 1}, {0, 0, 0, 1, 0}, {0, 0, 1, 0, 1}};
  mx.initial = {1, 0, 1, 0, 1};
  const auto rm = em::explore(mx);
  expect(rm.bounded && rm.deadlocks == 0 && rm.markings == 3, "mutual-exclusion net: 3 markings, never both in cs");
  // two processes taking two locks in opposite order can deadlock
  em::PetriNet dl;
  dl.places = {"a1", "a2", "b1", "b2", "lockA", "lockB", "done1", "done2"};
  dl.transitions = {"p1A", "p1B", "p2B", "p2A"};
  dl.pre = {{1, 0, 0, 0, 1, 0, 0, 0}, {0, 1, 0, 0, 0, 1, 0, 0}, {0, 0, 1, 0, 0, 1, 0, 0}, {0, 0, 0, 1, 1, 0, 0, 0}};
  dl.post = {{0, 1, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 1, 1, 0}, {0, 0, 0, 1, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 1, 0, 1}};
  dl.initial = {1, 0, 1, 0, 1, 1, 0, 0};
  const auto rd = em::explore(dl);
  bool real_deadlock = false;
  for (const auto& m : rd.deadlock_markings) real_deadlock |= m[1] == 1 && m[3] == 1;
  expect(real_deadlock, "opposite lock order: deadlock marking reachable");
}

}  // namespace

int main() {
  real_time();
  dataflow();
  petri();
  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "embedded tests passed\n";
  return 0;
}
