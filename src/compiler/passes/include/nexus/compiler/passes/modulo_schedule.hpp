#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nexus::compiler::passes {

// Software pipelining by iterative modulo scheduling (Rau 1994) for a VLIW machine (NEY606 (e): static
// instruction scheduling, VLIW processors and the programming techniques for static scheduling).
//
// Input: the data-dependence graph of a loop body,
//   resources alu 2, mem 1, mul 1
//   op load  mem latency 2
//   op mul   mul latency 3
//   edge load -> mul                 (distance 0: same iteration)
//   edge add -> load distance 1      (loop-carried: the next iteration)
// A new iteration starts every II cycles.  ResMII = max over resources of ceil(uses / units); RecMII = the
// smallest II for which no dependence cycle has positive weight sum(latency) - II * sum(distance).  The scheduler
// places operations by height priority into a modulo reservation table, evicting conflicting operations when no
// slot is free, and raises II when its budget runs out.  The result is checked against every dependence and
// every resource limit.

struct DdgOperation {
  std::string name;
  std::string resource;
  std::int64_t latency = 1;
};

struct DdgEdge {
  std::size_t from = 0;
  std::size_t to = 0;
  std::int64_t distance = 0;
};

struct Ddg {
  std::map<std::string, std::int64_t> resources;  // resource -> units per cycle
  std::vector<DdgOperation> operations;
  std::vector<DdgEdge> edges;
};

struct DdgParse {
  std::optional<Ddg> ddg;
  std::string error;
};

DdgParse parse_ddg(const std::string& text);

struct ModuloSchedule {
  bool success = false;
  std::string error;
  std::int64_t res_mii = 0;
  std::int64_t rec_mii = 0;
  std::int64_t mii = 0;
  std::int64_t ii = 0;
  std::vector<std::int64_t> time;      // issue cycle of each operation within one iteration
  std::int64_t stages = 0;             // ceil(schedule length / II)
  std::int64_t iteration_length = 0;   // cycles from the first issue to the last result of one iteration
  std::size_t attempts = 0;            // placements, including re-placements after evictions
  std::size_t evictions = 0;
};

std::int64_t res_mii(const Ddg& ddg);
std::int64_t rec_mii(const Ddg& ddg);
ModuloSchedule modulo_schedule(const Ddg& ddg, std::int64_t max_ii_extra = 32);
// Empty when the schedule meets every dependence and resource constraint; otherwise the first violation.
std::string verify_schedule(const Ddg& ddg, const ModuloSchedule& schedule);
std::string print_schedule(const Ddg& ddg, const ModuloSchedule& schedule);

}  // namespace nexus::compiler::passes
