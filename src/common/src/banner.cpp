#include "nexus/common/banner.hpp"

#include <sstream>

#include "nexus/common/build_info.hpp"

namespace nexus::common {

namespace {

std::string feature_label(std::string_view tool_name) {
  if (tool_name == "nexusc") {
    return "Compiler path available: lex/parse/ast/check/ir/cfg/dom/analysis/opt/experimental-parse/compile";
  }

  if (tool_name == "mips-sim") {
    return "MIPS simulator available: fp-demo <lhs> <rhs>; run <file> --mode functional|single-cycle|multi-cycle|pipeline|advanced|parallel [--trace] [--stats]; HDL via hdl-test all";
  }

  return "Functionality: not yet implemented";
}

}  // namespace

std::string banner_text(std::string_view tool_name) {
  const BuildInfo info = build_info();

  std::ostringstream banner;
  banner << info.project_name << '\n'
         << "Tool: " << tool_name << '\n'
         << "Version: " << info.version_label << '\n'
         << "Phase: " << info.phase_label << '\n'
         << "Status: " << info.status_label << '\n'
         << feature_label(tool_name);
  return banner.str();
}

}  // namespace nexus::common
