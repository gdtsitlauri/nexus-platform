// mips-sim commands for trace-driven studies: vector processor and multithreading (NEY704), virtual memory
// (EY321), non-blocking caches and prefetching (NEY606).
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/advanced/multithreading.hpp"
#include "nexus/sim/functional/interpreter.hpp"
#include "nexus/sim/memory/nonblocking_cache.hpp"
#include "nexus/sim/memory/virtual_memory.hpp"
#include "nexus/sim/parallel/message_passing.hpp"
#include "nexus/sim/parallel/network.hpp"
#include "nexus/sim/vector/model.hpp"

int run_vector(int argc, char** argv) {
  namespace vec = nexus::sim::vector;
  const std::string usage =
      "usage: mips-sim vector <file.vasm> [--n N] [--mvl M] [--lanes L] [--no-chaining] [--trace]\n";
  if (argc < 3) {
    std::cerr << usage;
    return 1;
  }
  std::ifstream in(argv[2]);
  if (!in) {
    std::cerr << "error: cannot open " << argv[2] << '\n';
    return 1;
  }
  std::stringstream text;
  text << in.rdbuf();
  vec::Options options;
  for (int i = 3; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--n" && i + 1 < argc) {
      options.params["n"] = std::stoll(argv[++i]);
    } else if (arg == "--mvl" && i + 1 < argc) {
      options.mvl = std::stoul(argv[++i]);
    } else if (arg == "--lanes" && i + 1 < argc) {
      options.lanes = std::stoul(argv[++i]);
    } else if (arg == "--no-chaining") {
      options.chaining = false;
    } else if (arg == "--trace") {
      options.trace = true;
    } else {
      std::cerr << usage;
      return 1;
    }
  }
  const auto result = vec::run(text.str(), options);
  if (!result.success) {
    std::cerr << "error: " << result.error << '\n';
    return 1;
  }
  for (const auto& line : result.trace_lines) {
    std::cout << line << '\n';
  }
  std::cout << "vector processor: MVL " << options.mvl << ", " << options.lanes << " lane(s), chaining "
            << (options.chaining ? "on" : "off") << '\n';
  std::cout << "cycles " << result.cycles << ", vector instructions " << result.vector_instructions
            << ", scalar instructions " << result.scalar_instructions << ", element operations " << result.elements
            << '\n';
  std::cout << "strips " << result.strips << ", convoys " << result.convoys << ", chime estimate "
            << result.chimes_estimate << " cycles\n";
  for (const auto& [name, values] : result.arrays) {
    std::cout << name << " =";
    const std::size_t shown = values.size() < 8 ? values.size() : 8;
    for (std::size_t i = 0; i < shown; ++i) {
      std::cout << ' ' << values[i];
    }
    if (values.size() > shown) {
      std::cout << " ... (" << values.size() << " elements)";
    }
    std::cout << '\n';
  }
  return 0;
}

// Multithreaded cores (NEY704): mips-sim mt <single|coarse|fine|smt> <thread.s>... [--width W] [--miss M]
int run_multithreading(int argc, char** argv) {
  namespace adv = nexus::sim::advanced;
  const std::string usage =
      "usage: mips-sim mt <single|coarse|fine|smt> <thread.s>... [--width W] [--miss M] [--switch S]\n";
  if (argc < 4) {
    std::cerr << usage;
    return 1;
  }
  adv::MtOptions options;
  const std::string policy = argv[2];
  if (policy == "single") options.policy = adv::MtPolicy::Single;
  else if (policy == "coarse") options.policy = adv::MtPolicy::Coarse;
  else if (policy == "fine") options.policy = adv::MtPolicy::Fine;
  else if (policy == "smt") options.policy = adv::MtPolicy::Smt;
  else {
    std::cerr << usage;
    return 1;
  }
  std::vector<nexus::mips::loader::LoadedProgram> loaded;
  for (int i = 3; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--width" && i + 1 < argc) {
      options.issue_width = std::stoul(argv[++i]);
    } else if (arg == "--miss" && i + 1 < argc) {
      options.miss_penalty = std::stoul(argv[++i]);
    } else if (arg == "--switch" && i + 1 < argc) {
      options.switch_penalty = std::stoul(argv[++i]);
    } else {
      auto program = nexus::mips::loader::load_program_from_file(arg);
      if (!program.program) {
        std::cerr << "error: cannot load " << arg << '\n';
        return 1;
      }
      loaded.push_back(std::move(*program.program));
    }
  }
  std::vector<const nexus::mips::loader::LoadedProgram*> programs;
  for (const auto& program : loaded) {
    programs.push_back(&program);
  }
  const auto result = adv::run_multithreaded(programs, options);
  if (!result.success) {
    std::cerr << "error: " << result.error << '\n';
    return 1;
  }
  const std::size_t width = options.policy == adv::MtPolicy::Smt ? options.issue_width : 1;
  std::cout << adv::policy_name(options.policy) << " multithreading, " << programs.size() << " thread(s): " << result.cycles
            << " cycles, " << result.issued << " instructions, utilisation " << std::fixed << std::setprecision(2)
            << 100.0 * result.utilization(width) << "%, " << result.switches << " switches, cache " << result.cache_hits
            << " hits / " << result.cache_misses << " misses\n";
  for (std::size_t t = 0; t < result.threads.size(); ++t) {
    std::cout << "  thread " << t << ": exit " << result.threads[t].exit_code << ", " << result.threads[t].instructions
              << " instructions, " << result.threads[t].misses << " misses, finished at cycle "
              << result.threads[t].finish_cycle << '\n';
  }
  return 0;
}

namespace {

// Runs a MIPS program functionally and returns its executed instructions with load/store addresses.
bool record(const char* path, std::vector<nexus::sim::functional::Event>& events) {
  const auto loaded = nexus::mips::loader::load_program_from_file(path);
  if (!loaded.program) {
    std::cerr << "error: cannot load " << path << '\n';
    return false;
  }
  nexus::sim::functional::RunOptions options;
  options.record_events = true;
  options.max_instructions = 5'000'000;
  auto run = nexus::sim::functional::run_program(*loaded.program, options);
  if (!run.success) {
    std::cerr << "error: " << run.error << '\n';
    return false;
  }
  events = std::move(run.events);
  return true;
}

}  // namespace

// Virtual memory (EY321): mips-sim vm <prog.s> [--page B] [--frames F] [--tlb T] [--policy fifo|lru|clock|opt]
int run_virtual_memory(int argc, char** argv) {
  namespace mem = nexus::sim::memory;
  if (argc < 3) {
    std::cerr << "usage: mips-sim vm <prog.s> [--page B] [--frames F] [--tlb T] [--policy fifo|lru|clock|opt]\n";
    return 1;
  }
  mem::VmConfig config;
  for (int i = 3; i + 1 < argc; i += 2) {
    const std::string arg = argv[i];
    const std::string value = argv[i + 1];
    if (arg == "--page") config.page_bytes = std::stoul(value);
    else if (arg == "--frames") config.frames = std::stoul(value);
    else if (arg == "--tlb") config.tlb_entries = std::stoul(value);
    else if (arg == "--policy") {
      config.policy = value == "fifo" ? mem::PagePolicy::Fifo : value == "clock" ? mem::PagePolicy::Clock
                    : value == "opt" ? mem::PagePolicy::Opt : mem::PagePolicy::Lru;
    }
  }
  std::vector<nexus::sim::functional::Event> events;
  if (!record(argv[2], events)) {
    return 1;
  }
  std::vector<mem::VmAccess> trace;
  for (const auto& e : events) {
    if (e.address >= 0) {
      trace.push_back({static_cast<std::uint64_t>(e.address), e.opcode == nexus::mips::isa::Opcode::Sw});
    }
  }
  const auto s = mem::simulate_virtual_memory(trace, config);
  std::cout << "virtual memory: pages of " << config.page_bytes << " bytes, " << config.frames << " frames, "
            << config.tlb_entries << "-entry TLB, " << mem::page_policy_name(config.policy) << " replacement\n";
  std::cout << s.accesses << " accesses to " << s.distinct_pages << " pages: TLB " << s.tlb_hits << " hits / "
            << s.tlb_misses << " misses, " << s.page_faults << " page faults, " << s.write_backs
            << " dirty pages written back; average access " << std::fixed << std::setprecision(2)
            << s.average_access_cycles() << " cycles\n";
  return 0;
}

// Non-blocking caches and prefetching (NEY606): mips-sim cache <prog.s> [--mshr K] [--prefetch none|next|stride]
int run_cache_study(int argc, char** argv) {
  namespace mem = nexus::sim::memory;
  if (argc < 3) {
    std::cerr << "usage: mips-sim cache <prog.s> [--mshr K] [--prefetch none|next|stride] [--penalty P] [--line B]\n";
    return 1;
  }
  mem::NbConfig config;
  for (int i = 3; i + 1 < argc; i += 2) {
    const std::string arg = argv[i];
    const std::string value = argv[i + 1];
    if (arg == "--mshr") config.mshrs = std::stoul(value);
    else if (arg == "--penalty") config.miss_penalty = std::stoul(value);
    else if (arg == "--line") config.line_bytes = std::stoul(value);
    else if (arg == "--prefetch") {
      config.prefetcher = value == "next" ? mem::Prefetcher::NextLine : value == "stride" ? mem::Prefetcher::Stride
                                                                                          : mem::Prefetcher::None;
    }
  }
  std::vector<nexus::sim::functional::Event> events;
  if (!record(argv[2], events)) {
    return 1;
  }
  std::vector<mem::CacheTraceEntry> trace;
  for (const auto& e : events) {
    trace.push_back({e.pc, e.address});
  }
  const auto s = mem::simulate_nonblocking_cache(trace, config);
  std::cout << (config.mshrs == 0 ? std::string("blocking cache") : std::to_string(config.mshrs) + " MSHRs")
            << ", prefetcher " << mem::prefetcher_name(config.prefetcher) << ", miss penalty " << config.miss_penalty << '\n';
  std::cout << s.instructions << " instructions, " << s.accesses << " accesses: " << s.hits << " hits, " << s.misses
            << " misses, " << s.merged << " merged into lines in flight; " << s.prefetches << " prefetches ("
            << s.useful_prefetches << " useful); " << s.cycles << " cycles, " << s.stall_cycles << " stall cycles\n";
  return 0;
}

// Distributed memory (NEY704): mips-sim msg <collective> [--nodes P] [--topology T] [--words M]
int run_message_passing_study(int argc, char** argv) {
  namespace mp = nexus::sim::parallel;
  const std::string usage =
      "usage: mips-sim msg <bcast-linear|bcast-tree|reduce|allreduce-rb|allreduce-rd> [--nodes P] "
      "[--topology full|ring|mesh|hypercube] [--words M]\n";
  if (argc < 3) {
    std::cerr << usage;
    return 1;
  }
  const std::string name = argv[2];
  mp::Collective collective;
  if (name == "bcast-linear") collective = mp::Collective::BroadcastLinear;
  else if (name == "bcast-tree") collective = mp::Collective::BroadcastTree;
  else if (name == "reduce") collective = mp::Collective::ReduceTree;
  else if (name == "allreduce-rb") collective = mp::Collective::AllreduceReduceBroadcast;
  else if (name == "allreduce-rd") collective = mp::Collective::AllreduceRecursiveDoubling;
  else {
    std::cerr << usage;
    return 1;
  }
  mp::NetworkConfig net;
  std::size_t words = 1;
  for (int i = 3; i + 1 < argc; i += 2) {
    const std::string arg = argv[i];
    const std::string value = argv[i + 1];
    if (arg == "--nodes") net.nodes = std::stoul(value);
    else if (arg == "--words") words = std::stoul(value);
    else if (arg == "--topology") {
      net.topology = value == "ring" ? mp::Topology::Ring : value == "mesh" ? mp::Topology::Mesh
                   : value == "hypercube" ? mp::Topology::Hypercube : mp::Topology::Full;
    }
  }
  const auto r = mp::run_collective(net, collective, words);
  if (!r.success) {
    std::cerr << "error: " << r.error << '\n';
    return 1;
  }
  std::cout << mp::collective_name(collective) << " on " << net.nodes << " nodes, " << mp::topology_name(net.topology)
            << ", " << words << "-word messages\n";
  std::cout << r.cycles << " cycles, " << r.messages << " messages, " << r.words << " words sent\n";
  std::cout << "node 0 holds:";
  for (std::size_t k = 0; k < r.data[0].size() && k < 8; ++k) {
    std::cout << ' ' << r.data[0][k];
  }
  std::cout << (r.data[0].size() > 8 ? " ..." : "") << '\n';
  return 0;
}

// Interconnection networks with link contention (NEY704): mips-sim net <topology> [options]
int run_network_study(int argc, char** argv) {
  namespace mp = nexus::sim::parallel;
  const std::string usage =
      "usage: mips-sim net <bus|crossbar|ring|mesh|torus|hypercube> [--nodes P] "
      "[--traffic uniform|transpose|complement|hotspot|neighbor] [--rate R] [--cycles C] [--flits F] "
      "[--router-delay D] [--seed S] [--sweep]\n";
  if (argc < 3) {
    std::cerr << usage;
    return 1;
  }
  const std::string name = argv[2];
  mp::ContentionConfig config;
  if (name == "bus") config.topology = mp::NetTopology::Bus;
  else if (name == "crossbar") config.topology = mp::NetTopology::Crossbar;
  else if (name == "ring") config.topology = mp::NetTopology::Ring;
  else if (name == "mesh") config.topology = mp::NetTopology::Mesh;
  else if (name == "torus") config.topology = mp::NetTopology::Torus;
  else if (name == "hypercube") config.topology = mp::NetTopology::Hypercube;
  else {
    std::cerr << usage;
    return 1;
  }
  mp::Traffic traffic = mp::Traffic::Uniform;
  double rate = 0.1;
  std::size_t cycles = 2000;
  std::uint32_t seed = 1;
  bool sweep = false;
  for (int i = 3; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--sweep") {
      sweep = true;
      continue;
    }
    if (i + 1 >= argc) {
      std::cerr << usage;
      return 1;
    }
    const std::string value = argv[++i];
    if (arg == "--nodes") config.nodes = std::stoul(value);
    else if (arg == "--rate") rate = std::stod(value);
    else if (arg == "--cycles") cycles = std::stoul(value);
    else if (arg == "--flits") config.flits = std::stoul(value);
    else if (arg == "--router-delay") config.router_delay = std::stoul(value);
    else if (arg == "--seed") seed = static_cast<std::uint32_t>(std::stoul(value));
    else if (arg == "--traffic") {
      if (value == "uniform") traffic = mp::Traffic::Uniform;
      else if (value == "transpose") traffic = mp::Traffic::Transpose;
      else if (value == "complement") traffic = mp::Traffic::BitComplement;
      else if (value == "hotspot") traffic = mp::Traffic::Hotspot;
      else if (value == "neighbor") traffic = mp::Traffic::Neighbor;
      else {
        std::cerr << usage;
        return 1;
      }
    } else {
      std::cerr << usage;
      return 1;
    }
  }
  if (const auto error = mp::validate(config); !error.empty()) {
    std::cerr << "error: " << error << '\n';
    return 1;
  }
  std::cout << mp::topology_name(config.topology) << ", " << config.nodes << " nodes, " << mp::traffic_name(traffic)
            << " traffic, " << config.flits << "-flit packets, router delay " << config.router_delay << '\n';
  if (traffic == mp::Traffic::Uniform) {
    std::cout << "bisection bound on uniform throughput: " << std::fixed << std::setprecision(3)
              << mp::uniform_throughput_bound(config) << " flits/node/cycle\n";
  }
  if (sweep) {
    std::cout << "offered  accepted  avg latency  zero-load  max latency  busiest channel (utilisation)\n";
    for (const double offered : {0.02, 0.05, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.8, 1.0}) {
      const auto r = mp::simulate_network(config, mp::make_traffic(traffic, config.nodes, offered, config.flits, cycles, seed));
      if (r.packets == 0) {
        continue;
      }
      const auto& busiest = r.channels[r.busiest];
      std::cout << std::fixed << std::setprecision(2) << std::setw(7) << offered << std::setw(10) << std::setprecision(3)
                << r.accepted_throughput << std::setw(13) << std::setprecision(1) << r.average_latency << std::setw(11)
                << r.average_zero_load << std::setw(13) << r.max_latency << "  " << busiest.name << " ("
                << std::setprecision(2) << 100.0 * static_cast<double>(busiest.busy) / static_cast<double>(r.makespan)
                << "%)\n";
    }
    return 0;
  }
  const auto r = mp::simulate_network(config, mp::make_traffic(traffic, config.nodes, rate, config.flits, cycles, seed));
  if (!r.success) {
    std::cerr << "error: " << r.error << '\n';
    return 1;
  }
  std::cout << r.packets << " packets (" << r.flits << " flits) offered at " << rate << " flits/node/cycle over "
            << cycles << " cycles\n";
  std::cout << std::fixed << std::setprecision(1) << "average latency " << r.average_latency << " cycles (zero-load "
            << r.average_zero_load << ", contention " << r.average_latency - r.average_zero_load << "), max "
            << r.max_latency << '\n';
  std::cout << "waiting for busy channels: " << r.contention_cycles << " cycles in total; last delivery at cycle "
            << r.makespan << ", accepted " << std::setprecision(3) << r.accepted_throughput << " flits/node/cycle\n";
  const auto& busiest = r.channels[r.busiest];
  std::cout << "busiest channel " << busiest.name << ": " << busiest.packets << " packets, busy " << busiest.busy
            << " of " << r.makespan << " cycles (" << std::setprecision(1)
            << 100.0 * static_cast<double>(busiest.busy) / static_cast<double>(std::max<std::size_t>(r.makespan, 1))
            << "%)\n";
  return 0;
}
