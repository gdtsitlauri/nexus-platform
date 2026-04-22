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
        raw_csv = temp / "advanced.csv"
        summary_md = temp / "advanced_summary.md"
        report_md = temp / "amdahl_report.md"

        run_result = run(
            [
                sys.executable,
                str(repo / "scripts" / "run_advanced_experiments.py"),
                str(nexusc),
                str(mips_sim),
                str(repo),
                str(raw_csv),
            ]
        )
        require(run_result.returncode == 0, f"run_advanced_experiments.py failed: {run_result.stderr}")
        require(raw_csv.exists(), "advanced experiments CSV was not created")
        raw_text = raw_csv.read_text()
        require("benchmark,configuration,mode" in raw_text, "advanced CSV missing header")
        require("predictor_loop,advanced-2bit" in raw_text, "advanced CSV missing predictor experiment")
        require("vliw_bundle,advanced-vliw-lite" in raw_text, "advanced CSV missing VLIW experiment")

        report_result = run(
            [
                sys.executable,
                str(repo / "scripts" / "generate_amdahl_report.py"),
                str(raw_csv),
                str(summary_md),
                str(report_md),
            ]
        )
        require(report_result.returncode == 0, f"generate_amdahl_report.py failed: {report_result.stderr}")
        require(summary_md.exists(), "advanced summary markdown was not created")
        require(report_md.exists(), "Amdahl report was not created")

        summary_text = summary_md.read_text()
        require("# Phase 8 Advanced Summary" in summary_text, "summary markdown missing Phase 8 title")
        require("| predictor_loop |" in summary_text, "summary markdown missing predictor row")
        require("| vliw_bundle |" in summary_text, "summary markdown missing VLIW row")

        report_text = report_md.read_text()
        require("# Phase 8 Amdahl Evaluation" in report_text, "Amdahl report missing title")
        require("## Predictor Comparison" in report_text, "Amdahl report missing predictor section")
        require("## Amdahl Projection" in report_text, "Amdahl report missing Amdahl section")
        require("advanced-2bit" in report_text, "Amdahl report missing improved predictor evidence")
        require("advanced-vliw-lite" in report_text, "Amdahl report missing VLIW evidence")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
