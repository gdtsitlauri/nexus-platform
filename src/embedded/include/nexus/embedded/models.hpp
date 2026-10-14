#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Embedded-system modelling and real-time scheduling (NEY604).
namespace nexus::embedded {

// ---------------------------------------------------------------- periodic real-time tasks

struct Task {
  std::string name;
  int wcet = 1;      // C
  int period = 1;    // T
  int deadline = 0;  // D (0 = implicit, D = T)
  int effective_deadline() const { return deadline > 0 ? deadline : period; }
};

double utilization(const std::vector<Task>& tasks);
double liu_layland_bound(std::size_t n);  // n (2^(1/n) - 1)
// Sufficient tests for rate-monotonic scheduling: Liu-Layland and the hyperbolic bound prod(U_i + 1) <= 2.
bool rm_liu_layland(const std::vector<Task>& tasks);
bool rm_hyperbolic(const std::vector<Task>& tasks);
// Exact response-time analysis for fixed priorities (tasks in priority order, highest first).
std::vector<std::optional<int>> response_times(const std::vector<Task>& tasks_by_priority);
// EDF with D = T: schedulable exactly when U <= 1; with D < T the processor-demand test up to the hyperperiod.
bool edf_schedulable(const std::vector<Task>& tasks);

enum class RtPolicy { RateMonotonic, DeadlineMonotonic, EDF };
struct RtTrace {
  std::vector<std::string> timeline;  // task running in each time unit ("" = idle)
  std::vector<std::string> missed;    // "<task>@<time>" for every missed deadline
  long hyperperiod = 0;
};
// Simulates the job releases of one hyperperiod (all tasks released at 0) under a preemptive policy.
RtTrace simulate_rt(const std::vector<Task>& tasks, RtPolicy policy);

// Priority inversion: a low-priority task holds a lock that a high-priority task needs while a medium task runs.
struct InversionResult {
  int high_finish_no_protocol = 0;
  int high_finish_inheritance = 0;
  int high_blocking_no_protocol = 0;
  int high_blocking_inheritance = 0;
};
InversionResult priority_inversion_demo(int medium_work);

// ---------------------------------------------------------------- synchronous dataflow

struct SdfEdge {
  std::size_t from, to;
  int produce, consume;  // tokens per firing
  int initial = 0;       // initial tokens (delays)
};
struct SdfGraph {
  std::vector<std::string> actors;
  std::vector<SdfEdge> edges;
};
// Repetition vector from the balance equations; nothing if the graph is inconsistent (sample-rate mismatch).
std::optional<std::vector<long>> repetition_vector(const SdfGraph& g);
// A periodic admissible sequential schedule (list scheduling of one iteration); nothing on deadlock.
std::optional<std::vector<std::size_t>> sdf_schedule(const SdfGraph& g, std::vector<long>* buffer_max = nullptr);

// ---------------------------------------------------------------- Petri nets

struct PetriNet {
  std::vector<std::string> places;
  std::vector<std::string> transitions;
  std::vector<std::vector<int>> pre;   // [transition][place] tokens consumed
  std::vector<std::vector<int>> post;  // [transition][place] tokens produced
  std::vector<int> initial;
};
struct Reachability {
  std::size_t markings = 0;
  bool bounded = true;            // no marking strictly covers an ancestor (Karp-Miller style check)
  std::size_t deadlocks = 0;      // reachable markings with no enabled transition
  int max_tokens = 0;             // largest number of tokens in any place
  std::vector<std::vector<int>> deadlock_markings;
};
Reachability explore(const PetriNet& net, std::size_t max_markings = 100000);

}  // namespace nexus::embedded
