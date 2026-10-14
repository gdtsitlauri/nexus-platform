#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Processes and uniprocessor scheduling (EY517).
namespace nexus::os {

// ---------------------------------------------------------------- process states

enum class ProcState { New, Ready, Running, Blocked, ReadySuspend, BlockedSuspend, Exit };
const char* state_name(ProcState s);

// The seven-state process model: returns true when `from -> to` is a legal transition (admit, dispatch, timeout,
// event wait, event occurs, suspend, activate, release).
bool legal_transition(ProcState from, ProcState to);

struct ProcessControlBlock {
  int pid = 0;
  ProcState state = ProcState::New;
  std::vector<std::pair<ProcState, ProcState>> history;
  bool transition(ProcState to);  // false (and no change) when illegal
};

// ---------------------------------------------------------------- scheduling

struct Job {
  std::string name;
  int arrival = 0;
  int service = 1;
  int priority = 0;  // lower number = higher priority
};

struct Slice {
  std::string name;  // "" = idle
  int start = 0;
  int end = 0;
};

struct JobResult {
  std::string name;
  int finish = 0;
  int turnaround = 0;     // finish - arrival
  double normalized = 0;  // turnaround / service
  int waiting = 0;        // turnaround - service
  int response = 0;       // first run - arrival
};

struct Schedule {
  std::string policy;
  std::vector<Slice> gantt;
  std::vector<JobResult> jobs;  // in input order
  double mean_turnaround = 0.0;
  double mean_normalized = 0.0;
  double mean_waiting = 0.0;
  std::size_t context_switches = 0;
};

enum class Policy { FCFS, RR, SPN, SRT, HRRN, Priority, PriorityPreemptive, Feedback };

// `quantum` for RR; Feedback uses queues RQ0..RQn with quantum q * 2^i when `exponential` is set (Stallings FB).
// Ties are broken by arrival, then input order. In RR a job arriving at the moment of a time-out enters the ready
// queue before the preempted job (Stallings' convention).
Schedule schedule(const std::vector<Job>& jobs, Policy policy, int quantum = 1, bool exponential = false);
std::string format_schedule(const Schedule& s);

// Minimum mean turnaround over every non-preemptive order when all jobs arrive at 0 (exhaustive; jobs <= 8).
double best_nonpreemptive_mean_turnaround(const std::vector<Job>& jobs);

}  // namespace nexus::os
