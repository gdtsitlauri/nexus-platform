// nexus-embedded: real-time scheduling and embedded-system models (NEY604).
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "nexus/embedded/models.hpp"

namespace em = nexus::embedded;

namespace {

void usage() {
  std::cout << "nexus-embedded: real-time scheduling and models (NEY604)\n"
               "  nexus-embedded rt <file.tasks>   utilisation tests, response-time analysis, RM/DM/EDF timelines\n"
               "                                   task file lines: <name> <wcet> <period> [deadline]\n"
               "  nexus-embedded inversion [work]  priority inversion with and without priority inheritance\n"
               "  nexus-embedded sdf               synchronous dataflow example: repetition vector and schedule\n";
}

int cmd_rt(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot open " + path);
  }
  std::vector<em::Task> tasks;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ls(line);
    em::Task t;
    ls >> t.name >> t.wcet >> t.period;
    if (!(ls >> t.deadline)) t.deadline = 0;
    tasks.push_back(t);
  }
  std::stable_sort(tasks.begin(), tasks.end(), [](const em::Task& a, const em::Task& b) { return a.period < b.period; });
  std::cout << "utilisation U = " << em::utilization(tasks) << ", Liu-Layland bound " << em::liu_layland_bound(tasks.size())
            << " -> " << (em::rm_liu_layland(tasks) ? "RM schedulable" : "inconclusive") << "; hyperbolic bound -> "
            << (em::rm_hyperbolic(tasks) ? "RM schedulable" : "inconclusive") << '\n';
  const auto r = em::response_times(tasks);
  std::cout << "response-time analysis (rate-monotonic priorities):\n";
  for (std::size_t i = 0; i < tasks.size(); ++i) {
    std::cout << "  " << tasks[i].name << ": R = " << (r[i] ? std::to_string(*r[i]) : "> D (misses)") << ", D = "
              << tasks[i].effective_deadline() << '\n';
  }
  std::cout << "EDF: " << (em::edf_schedulable(tasks) ? "schedulable" : "not schedulable") << '\n';
  for (const auto [name, pol] : {std::pair{"RM", em::RtPolicy::RateMonotonic}, std::pair{"DM", em::RtPolicy::DeadlineMonotonic},
                                 std::pair{"EDF", em::RtPolicy::EDF}}) {
    const auto tr = em::simulate_rt(tasks, pol);
    std::cout << name << " timeline (hyperperiod " << tr.hyperperiod << "): ";
    for (std::size_t t = 0; t < tr.timeline.size() && t < 60; ++t) {
      std::cout << (tr.timeline[t].empty() ? std::string(".") : tr.timeline[t].substr(tr.timeline[t].size() - 1));
    }
    std::cout << (tr.missed.empty() ? "  all deadlines met" : "  missed: " + tr.missed.front()) << '\n';
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 0;
  }
  const std::string cmd = argv[1];
  try {
    if (cmd == "rt" && argc >= 3) return cmd_rt(argv[2]);
    if (cmd == "inversion") {
      const auto r = em::priority_inversion_demo(argc >= 3 ? std::stoi(argv[2]) : 5);
      std::cout << "high-priority task finishes at " << r.high_finish_no_protocol << " (blocked "
                << r.high_blocking_no_protocol << ") without a protocol, at " << r.high_finish_inheritance << " (blocked "
                << r.high_blocking_inheritance << ") with priority inheritance\n";
      return 0;
    }
    if (cmd == "sdf") {
      em::SdfGraph g;
      g.actors = {"src", "filter", "sink"};
      g.edges = {{0, 1, 2, 3, 0}, {1, 2, 1, 2, 0}};
      const auto q = em::repetition_vector(g);
      std::vector<long> buf;
      const auto s = em::sdf_schedule(g, &buf);
      std::cout << "repetition vector:";
      for (std::size_t i = 0; i < q->size(); ++i) std::cout << ' ' << g.actors[i] << "x" << (*q)[i];
      std::cout << "\nschedule:";
      for (const auto a : *s) std::cout << ' ' << g.actors[a];
      std::cout << "\nbuffer sizes: " << buf[0] << ", " << buf[1] << '\n';
      return 0;
    }
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
    return 1;
  }
  usage();
  return 1;
}
