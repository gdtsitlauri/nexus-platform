#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path

ALLOWED_STATUSES = {"implemented and tested", "implemented, toolchain-dependent", "notes"}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def read(path: Path) -> str:
    require(path.exists(), f"missing required file: {path}")
    return path.read_text(encoding="utf-8")


def registered_tests(root: Path) -> set[str]:
    names: set[str] = set()
    for cmake in (root / "tests").rglob("CMakeLists.txt"):
        names.update(re.findall(r"add_test\(\s*NAME\s+(\w+)", cmake.read_text(encoding="utf-8")))
    return names


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
    tests = registered_tests(root)
    rows = 0
    for line in checklist.splitlines():
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) != 4 or cells[0] in ("Topic", "---"):
            continue
        rows += 1
        require(cells[1] in ALLOWED_STATUSES, f"unexpected checklist status '{cells[1]}' for '{cells[0]}'")
        for name in re.findall(r"`(nexus_[\w*]+)`", cells[3]):
            if name.endswith("*"):
                require(any(test.startswith(name[:-1]) for test in tests), f"no test matches {name}")
            else:
                require(name in tests, f"checklist cites unknown test {name}")
    require(rows >= 70, f"checklist has too few rows ({rows})")
    require("still outside" not in checklist.lower(), "a topic is still marked as outside the project")

    for relative in (
        "docs/reports/validation_report.md",
        "docs/reports/course_mapping.md",
        "docs/reports/toolchain_comparison.md",
        "docs/reports/benchmark_report_template.md",
    ):
        text = read(root / relative)
        require("placeholder" not in text.lower(), f"placeholder text still present in {relative}")
        require(len(text.splitlines()) >= 10, f"report too small: {relative}")


def check_final(root: Path) -> None:
    status = read(root / "STATUS.md")
    readme = read(root / "README.md")
    architecture = read(root / "ARCHITECTURE.md")
    changelog = read(root / "CHANGELOG.md")

    require("1.0.0" in status, "STATUS.md does not reflect version 1.0.0")
    require("1.0.0" in readme, "README.md missing version 1.0.0")
    require("HDL" in architecture, "ARCHITECTURE.md missing HDL coverage")
    require("[1.0.0]" in changelog, "CHANGELOG.md missing the 1.0.0 entry")

    for relative in (
        "README_GR.md",
        "CITATION.cff",
        "scripts/test_hdl.py",
        "hdl-test",
        "src/hdl/alu/nexus_alu.v",
        "src/hdl/register_file/nexus_register_file.v",
        "src/hdl/control/nexus_control_unit.v",
        "src/hdl/pipeline_regs/nexus_pipeline_reg.v",
        "src/hdl/alu/nexus_iterative_multiplier.v",
        "src/hdl/alu/nexus_iterative_divider.v",
        "src/hdl/cpu_pipeline/nexus_mips_pipeline.v",
        "benchmarks/gpu_optional/gpu_optional_bench.cu",
        "docs/course_outlines/NEY221_principles_of_computer_operation.pdf",
        "docs/course_outlines/EY321_computer_organization.pdf",
        "docs/course_outlines/NEY606_computer_architecture.pdf",
        "docs/course_outlines/NEY613_compilers.pdf",
        "docs/course_outlines/NEY709_advanced_compiler_topics.pdf",
        "docs/course_outlines/NEY704_parallel_systems_and_programming.pdf",
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
