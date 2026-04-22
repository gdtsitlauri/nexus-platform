#include "nexus/common/build_info.hpp"

namespace nexus::common {

BuildInfo build_info() {
  return BuildInfo{
      .project_name = "Nexus / NexusLang Platform",
      .version_label = NEXUS_VERSION_LABEL,
      .phase_label = NEXUS_PHASE_LABEL,
      .status_label = "buildable compiler, simulator, HDL, and final-report stack with optional GPU demos",
  };
}

}  // namespace nexus::common
