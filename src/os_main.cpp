// nexus-os: operating-system algorithms (EY517) from the command line.
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "nexus/os/concurrency.hpp"
#include "nexus/os/resources.hpp"
#include "nexus/os/scheduling.hpp"
#include "nexus/os/storage.hpp"

namespace os = nexus::os;

namespace {

void usage() {
  std::cout << "nexus-os: operating-system algorithms (EY517)\n"
               "  nexus-os schedule <file.jobs> [--policy fcfs|rr|spn|srt|hrrn|priority|feedback|all] [--quantum q]\n"
               "                    job file lines: <name> <arrival> <service> [priority]\n"
               "  nexus-os check <file.conc>              model-check a concurrent program (all interleavings)\n"
               "  nexus-os banker                          Banker's algorithm on the textbook example\n"
               "  nexus-os pages <frames> <ref> <ref> ...  FIFO, LRU, OPT and Clock page replacement\n"
               "  nexus-os disk <start> <track> ...        FIFO, SSTF, SCAN, C-SCAN, LOOK, C-LOOK\n"
               "  nexus-os alloc <memory> <size> <size> ... first/next/best/worst fit and the buddy system\n"
               "  nexus-os fs                              file-system demonstration with fsck\n";
}

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot open " + path);
  }
  std::ostringstream s;
  s << in.rdbuf();
  return s.str();
}

int cmd_schedule(const std::string& path, const std::string& which, int quantum) {
  std::vector<os::Job> jobs;
  std::istringstream in(read_file(path));
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream ls(line);
    os::Job j;
    ls >> j.name >> j.arrival >> j.service;
    if (!(ls >> j.priority)) {
      j.priority = 0;
    }
    jobs.push_back(j);
  }
  const std::map<std::string, os::Policy> pol = {
      {"fcfs", os::Policy::FCFS}, {"rr", os::Policy::RR},     {"spn", os::Policy::SPN},
      {"srt", os::Policy::SRT},   {"hrrn", os::Policy::HRRN}, {"priority", os::Policy::Priority},
      {"feedback", os::Policy::Feedback}};
  for (const auto& [name, p] : pol) {
    if (which == "all" || which == name) {
      std::cout << os::format_schedule(os::schedule(jobs, p, quantum, name == "feedback")) << '\n';
    }
  }
  return 0;
}

int cmd_check(const std::string& path) {
  const auto r = os::model_check(os::parse_concurrent(read_file(path)));
  std::cout << "states explored: " << r.states << ", transitions: " << r.transitions << '\n'
            << "mutual exclusion: " << (r.mutual_exclusion ? "holds" : "VIOLATED") << '\n'
            << "deadlock freedom: " << (r.deadlock_free ? "holds" : "VIOLATED") << '\n'
            << "final assertions: " << (r.assertions_hold ? "hold" : "FAIL") << '\n';
  if (!r.final_values_seen.empty()) {
    std::cout << "final values of the asserted variable:";
    for (const auto v : r.final_values_seen) {
      std::cout << ' ' << v;
    }
    std::cout << '\n';
  }
  if (!r.violation.empty()) {
    std::cout << "first violation: " << r.violation << "\ncounterexample schedule:\n";
    for (const auto& s : r.counterexample) {
      std::cout << "  " << s << '\n';
    }
  }
  return r.mutual_exclusion && r.deadlock_free && r.assertions_hold ? 0 : 2;
}

int cmd_banker() {
  os::BankerState s;
  s.available = {3, 3, 2};
  s.max = {{7, 5, 3}, {3, 2, 2}, {9, 0, 2}, {2, 2, 2}, {4, 3, 3}};
  s.allocation = {{0, 1, 0}, {2, 0, 0}, {3, 0, 2}, {2, 1, 1}, {0, 0, 2}};
  auto show = [](const os::BankerState& st) {
    const auto seq = os::safe_sequence(st);
    if (seq) {
      std::cout << "safe, sequence:";
      for (const auto p : *seq) {
        std::cout << " P" << p;
      }
      std::cout << '\n';
    } else {
      std::cout << "unsafe\n";
    }
  };
  std::cout << "Available (A B C) = 3 3 2\n";
  show(s);
  for (const auto& [p, req] : std::vector<std::pair<std::size_t, std::vector<int>>>{
           {1, {1, 0, 2}}, {4, {3, 3, 0}}, {0, {0, 2, 0}}}) {
    std::string why;
    os::request_resources(s, p, req, &why);
    std::cout << "P" << p << " requests (" << req[0] << ' ' << req[1] << ' ' << req[2] << "): " << why << '\n';
  }
  return 0;
}

int cmd_pages(const std::vector<std::string>& args) {
  const auto frames = static_cast<std::size_t>(std::stoul(args.at(0)));
  std::vector<int> refs;
  for (std::size_t i = 1; i < args.size(); ++i) {
    refs.push_back(std::stoi(args[i]));
  }
  for (const auto r : {os::Replacement::FIFO, os::Replacement::LRU, os::Replacement::OPT, os::Replacement::Clock}) {
    const auto t = os::simulate_replacement(refs, frames, r);
    std::cout << std::setw(6) << os::replacement_name(r) << ": " << t.faults << " faults  ";
    for (std::size_t i = 0; i < refs.size(); ++i) {
      std::cout << (t.fault[i] ? '*' : '.');
    }
    std::cout << '\n';
  }
  return 0;
}

int cmd_disk(const std::vector<std::string>& args) {
  const int start = std::stoi(args.at(0));
  std::vector<int> req;
  for (std::size_t i = 1; i < args.size(); ++i) {
    req.push_back(std::stoi(args[i]));
  }
  for (const auto p : {os::DiskPolicy::FIFO, os::DiskPolicy::SSTF, os::DiskPolicy::SCAN, os::DiskPolicy::CSCAN,
                       os::DiskPolicy::LOOK, os::DiskPolicy::CLOOK}) {
    const auto s = os::schedule_disk(req, start, p);
    std::cout << std::setw(7) << os::disk_policy_name(p) << ": movement " << s.total_movement << ", average "
              << s.average_seek << "  order:";
    for (const int t : s.order) {
      std::cout << ' ' << t;
    }
    std::cout << '\n';
  }
  if (req.size() <= 9) {
    std::cout << "optimal movement (exhaustive): " << os::optimal_disk_movement(req, start) << '\n';
  }
  return 0;
}

int cmd_alloc(const std::vector<std::string>& args) {
  const auto memory = static_cast<std::size_t>(std::stoul(args.at(0)));
  const char* names[] = {"first", "next", "best", "worst"};
  for (int f = 0; f < 4; ++f) {
    os::PartitionAllocator a(memory, static_cast<os::Fit>(f));
    std::size_t failed = 0;
    for (std::size_t i = 1; i < args.size(); ++i) {
      const int owner = static_cast<int>(i);
      if (args[i][0] == '-') {
        a.release(std::stoi(args[i].substr(1)));  // "-k" frees request k
      } else if (!a.allocate(std::stoul(args[i]), owner)) {
        ++failed;
      }
    }
    std::cout << std::setw(5) << names[f] << " fit: holes " << a.holes() << ", free " << a.free_total()
              << ", largest hole " << a.largest_hole() << ", external fragmentation " << a.external_fragmentation()
              << ", failed requests " << failed << '\n';
  }
  return 0;
}

int cmd_fs() {
  os::FileSystem fs(256, 64, 32);
  fs.mkdir("/home");
  fs.create("/home/notes.txt");
  fs.write("/home/notes.txt", 0, "operating systems: processes, memory, files\n");
  fs.create("/big");
  fs.write("/big", 0, std::string(3000, 'x'));
  fs.link("/home/notes.txt", "/notes-link");
  std::cout << "/ : ";
  for (const auto& n : fs.list("/")) std::cout << n << ' ';
  std::cout << "\n/home : ";
  for (const auto& n : fs.list("/home")) std::cout << n << ' ';
  std::cout << "\n/notes-link: " << *fs.read("/notes-link");
  std::cout << "/big uses " << fs.blocks_of("/big") << " blocks (data + indirect), max file size "
            << fs.max_file_size() << " bytes, free blocks " << fs.free_blocks() << '\n';
  std::string report;
  std::cout << "fsck: " << (fs.fsck(&report) ? "clean" : report) << '\n';
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 0;
  }
  const std::string cmd = argv[1];
  std::vector<std::string> args(argv + 2, argv + argc);
  try {
    if (cmd == "schedule") {
      std::string policy = "all";
      int quantum = 1;
      std::vector<std::string> pos;
      for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--policy") policy = args.at(++i);
        else if (args[i] == "--quantum") quantum = std::stoi(args.at(++i));
        else pos.push_back(args[i]);
      }
      return cmd_schedule(pos.at(0), policy, quantum);
    }
    if (cmd == "check") return cmd_check(args.at(0));
    if (cmd == "banker") return cmd_banker();
    if (cmd == "pages") return cmd_pages(args);
    if (cmd == "disk") return cmd_disk(args);
    if (cmd == "alloc") return cmd_alloc(args);
    if (cmd == "fs") return cmd_fs();
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
    return 1;
  }
  usage();
  return 1;
}
