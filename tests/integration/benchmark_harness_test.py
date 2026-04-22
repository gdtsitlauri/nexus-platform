#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


def run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, capture_output=True, check=False)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> int:
    nexusc = Path(sys.argv[1])
    mips_sim = Path(sys.argv[2])
    repo = Path(sys.argv[3])

    with tempfile.TemporaryDirectory() as temp_dir:
        temp = Path(temp_dir)
        raw_csv = temp / "benchmarks.csv"
        summary_csv = temp / "benchmarks_summary.csv"
        summary_md = temp / "benchmarks_summary.md"
        report_md = temp / "benchmark_report.md"

        run_result = run(
            [
                sys.executable,
                str(repo / "scripts" / "run_benchmarks.py"),
                str(nexusc),
                str(mips_sim),
                str(repo),
                str(raw_csv),
            ]
        )
        require(run_result.returncode == 0, f"run_benchmarks.py failed: {run_result.stderr}")
        require(raw_csv.exists(), "raw benchmark CSV was not created")
        raw_text = raw_csv.read_text()
        require("benchmark,run,mode" in raw_text, "raw benchmark CSV missing header")
        require("factorial" in raw_text and "arrays_and_loops" in raw_text, "raw benchmark CSV missing benchmarks")

        aggregate_result = run(
            [
                sys.executable,
                str(repo / "scripts" / "aggregate_benchmarks.py"),
                str(raw_csv),
                str(summary_csv),
                str(summary_md),
            ]
        )
        require(aggregate_result.returncode == 0, f"aggregate_benchmarks.py failed: {aggregate_result.stderr}")
        require(summary_csv.exists(), "summary CSV was not created")
        require(summary_md.exists(), "summary markdown was not created")
        summary_text = summary_md.read_text()
        require("| factorial |" in summary_text, "summary markdown missing factorial row")
        require("pipeline-direct" in summary_text or "pipeline-assoc" in summary_text, "summary markdown missing cache-backed run")

        report_result = run(
            [
                sys.executable,
                str(repo / "scripts" / "generate_benchmark_report.py"),
                str(raw_csv),
                str(summary_md),
                str(report_md),
            ]
        )
        require(report_result.returncode == 0, f"generate_benchmark_report.py failed: {report_result.stderr}")
        require(report_md.exists(), "benchmark report was not created")
        report_text = report_md.read_text()
        require("# Phase 7 Benchmark Report" in report_text, "benchmark report missing title")
        require("## Aggregated Summary" in report_text, "benchmark report missing summary section")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
