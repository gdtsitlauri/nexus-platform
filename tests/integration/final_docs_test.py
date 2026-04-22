#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def read(path: Path) -> str:
    require(path.exists(), f"missing required file: {path}")
    return path.read_text(encoding="utf-8")


def check_literature(root: Path) -> None:
    for relative in (
        "docs/literature/architecture_readings.md",
        "docs/literature/parallel_readings.md",
        "docs/literature/compiler_readings.md",
    ):
        text = read(root / relative)
        require("placeholder" not in text.lower(), f"placeholder text still present in {relative}")
        require(len(text.splitlines()) >= 20, f"literature note too small: {relative}")


def check_reports(root: Path) -> None:
    checklist = read(root / "docs/reports/full_syllabus_checklist.md")
    require("## Status Legend" in checklist, "missing checklist legend")
    require(
        all(
            status in checklist
            for status in (
                "implemented",
                "experimentally implemented",
                "documented with worked examples",
                "pending",
            )
        ),
        "missing one or more checklist statuses",
    )

    for relative in (
        "docs/reports/validation_report.md",
        "docs/reports/course_mapping.md",
        "docs/reports/toolchain_comparison.md",
        "docs/reports/benchmark_report_template.md",
        "docs/reports/final_project_summary.md",
        "docs/reports/final_nexus_system_paper.md",
    ):
        text = read(root / relative)
        require("placeholder" not in text.lower(), f"placeholder text still present in {relative}")
        require(len(text.splitlines()) >= 10, f"report too small: {relative}")


def check_final(root: Path) -> None:
    status = read(root / "STATUS.md")
    readme = read(root / "README.md")
    architecture = read(root / "ARCHITECTURE.md")
    changelog = read(root / "CHANGELOG.md")

    require("Phase 11" in status, "STATUS.md does not reflect Phase 11")
    require("0.11.0-phase11" in readme, "README.md missing Phase 11 version")
    require("HDL" in architecture, "ARCHITECTURE.md missing HDL coverage")
    require("[0.11.0-phase11]" in changelog, "CHANGELOG.md missing Phase 11 entry")

    for relative in (
        "scripts/test_hdl.sh",
        "hdl-test",
        "src/hdl/alu/nexus_alu.v",
        "src/hdl/register_file/nexus_register_file.v",
        "src/hdl/control/nexus_control_unit.v",
        "src/hdl/pipeline_regs/nexus_pipeline_reg.v",
        "src/hdl/alu/nexus_iterative_multiplier.v",
        "src/hdl/alu/nexus_iterative_divider.v",
        "benchmarks/gpu_optional/gpu_optional_bench.cu",
        "docs/reports/final_nexus_system_paper.md",
    ):
        require((root / relative).exists(), f"missing final artifact: {relative}")


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: final_docs_test.py <mode> <repo-root>")

    mode = sys.argv[1]
    root = Path(sys.argv[2]).resolve()

    if mode == "literature":
        check_literature(root)
    elif mode == "report":
        check_reports(root)
    elif mode == "final":
        check_final(root)
    else:
        raise SystemExit(f"unknown mode: {mode}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
