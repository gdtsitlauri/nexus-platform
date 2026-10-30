// mips-sim commands for machine models that do not execute MIPS programs: the vector processor (NEY704).
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

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
