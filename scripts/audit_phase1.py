#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path


REQUIRED_PATHS = [
    "README.md",
    "ROADMAP.md",
    "ARCHITECTURE.md",
    "STATUS.md",
    "CHANGELOG.md",
    "LICENSE",
    "CMakeLists.txt",
    "Makefile",
    "cmake/ProjectOptions.cmake",
    "cmake/Warnings.cmake",
    "cmake/Sanitizers.cmake",
    "config/Doxyfile.in",
    "docs/architecture/mips_isa.md",
    "docs/architecture/isa_comparison.md",
    "docs/architecture/data_representation.md",
    "docs/compiler/overview.md",
    "docs/compiler/language_spec.md",
    "docs/compiler/formal_foundations.md",
    "docs/compiler/semantic_models.md",
    "docs/compiler/ir.md",
    "docs/compiler/optimizations.md",
    "docs/compiler/generalized_and_parallel_parsing.md",
    "docs/compiler/region_based_dataflow.md",
    "docs/compiler/loop_optimizations.md",
    "docs/microarchitecture/pipeline.md",
    "docs/microarchitecture/hazards.md",
    "docs/microarchitecture/branch_prediction.md",
    "docs/microarchitecture/cache.md",
    "docs/parallel/overview.md",
    "docs/parallel/coherence.md",
    "docs/parallel/consistency.md",
    "docs/parallel/openmp_mpi_gpu.md",
    "docs/literature/architecture_readings.md",
    "docs/literature/parallel_readings.md",
    "docs/literature/compiler_readings.md",
    "docs/reports/history_of_computing_evolution.md",
    "docs/reports/course_mapping.md",
    "docs/reports/toolchain_comparison.md",
    "docs/reports/benchmark_report_template.md",
    "docs/reports/validation_report.md",
    "docs/reports/full_syllabus_checklist.md",
    "docs/diagrams/system_overview.mmd",
    "examples/formal/README.md",
    "tools/toolchain_compare/README.md",
    "third_party/README.md",
    "src/CMakeLists.txt",
    "src/nexusc_main.cpp",
    "src/mips_sim_main.cpp",
    "src/common/CMakeLists.txt",
    "src/common/include/nexus/common/build_info.hpp",
    "src/common/include/nexus/common/banner.hpp",
    "src/common/src/build_info.cpp",
    "src/common/src/banner.cpp",
    "tests/CMakeLists.txt",
    "tests/unit/CMakeLists.txt",
    "tests/unit/common_smoke_test.cpp",
    "tests/integration/CMakeLists.txt",
    "tests/golden/CMakeLists.txt",
    "tests/golden/README.md",
]

REQUIRED_LEGEND = [
    "- implemented",
    "- experimentally implemented",
    "- documented with worked examples",
    "- pending",
]


def main() -> int:
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path.cwd().resolve()

    missing = [path for path in REQUIRED_PATHS if not (root / path).exists()]

    checklist = (root / "docs/reports/full_syllabus_checklist.md").read_text(encoding="utf-8")
    missing_legend = [entry for entry in REQUIRED_LEGEND if entry not in checklist]

    if missing:
        print("Missing required Phase 1 paths:")
        for path in missing:
            print(f"  - {path}")
        return 1

    if missing_legend:
        print("Missing checklist legend entries:")
        for entry in missing_legend:
            print(f"  - {entry}")
        return 1

    print("Phase 1 scaffold audit passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
