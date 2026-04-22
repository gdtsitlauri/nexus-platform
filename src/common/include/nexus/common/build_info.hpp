#pragma once

#include <string>

namespace nexus::common {

struct BuildInfo {
  std::string project_name;
  std::string version_label;
  std::string phase_label;
  std::string status_label;
};

BuildInfo build_info();

}  // namespace nexus::common
