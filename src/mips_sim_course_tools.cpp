// mips-sim commands for the vector processor and the multithreaded cores (NEY704).
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "nexus/mips/loader/parser.hpp"
#include "nexus/sim/advanced/multithreading.hpp"
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
