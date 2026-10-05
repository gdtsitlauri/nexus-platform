#include <iostream>
#include <string>

#include "nexus/common/banner.hpp"
#include "nexus/common/build_info.hpp"

int main() {
  const nexus::common::BuildInfo info = nexus::common::build_info();

  if (info.project_name != "Nexus / NexusLang Platform") {
    std::cerr << "Unexpected project name: " << info.project_name << '\n';
    return 1;
  }

  if (info.version_label != "1.0.0") {
    std::cerr << "Unexpected version label: " << info.version_label << '\n';
    return 1;
  }

  if (info.phase_label != "1.0 - complete course coverage") {
    std::cerr << "Unexpected phase label: " << info.phase_label << '\n';
    return 1;
  }

  const std::string compiler_banner = nexus::common::banner_text("nexusc");
  if (compiler_banner.find(
          "Compiler path available: lex/parse/ast/check/ir/cfg/dom/analysis/opt/experimental-parse/compile") ==
      std::string::npos) {
    std::cerr << "Compiler banner missing Phase 9 command text.\n";
    return 1;
  }

  const std::string simulator_banner = nexus::common::banner_text("mips-sim");
  if (simulator_banner.find(
          "MIPS simulator available: fp-demo <lhs> <rhs>; run <file> --mode functional|single-cycle|multi-cycle|pipeline|advanced|parallel [--trace] [--stats]; HDL via hdl-test all") ==
      std::string::npos) {
    std::cerr << "Simulator banner missing expected mode text.\n";
    return 1;
  }

  return 0;
}
