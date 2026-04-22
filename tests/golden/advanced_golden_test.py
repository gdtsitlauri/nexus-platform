#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, capture_output=True, check=False)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> int:
    mips_sim = Path(sys.argv[1])
    repo = Path(sys.argv[2])

    trace_program = repo / "tests" / "golden" / "advanced_vliw_demo.s"
    predictor_program = repo / "tests" / "golden" / "advanced_branch_demo.s"
    trace_expected = (repo / "tests" / "golden" / "advanced_trace.stdout.txt").read_text()

    trace_run = run(
        [
            str(mips_sim),
            "run",
            str(trace_program),
            "--mode",
            "advanced",
            "--scheduler",
            "vliw-lite",
            "--issue-width",
            "2",
            "--trace",
        ]
    )
    require(trace_run.returncode == 0, "advanced golden trace run failed")
    require(trace_run.stdout == trace_expected, "advanced golden trace output mismatch")

    static_run = run(
        [str(mips_sim), "run", str(predictor_program), "--mode", "advanced", "--predictor", "static-not-taken", "--stats"]
    )
    two_bit_run = run(
        [str(mips_sim), "run", str(predictor_program), "--mode", "advanced", "--predictor", "2bit", "--stats"]
    )
    require(static_run.returncode == 0 and two_bit_run.returncode == 0, "advanced golden predictor run failed")
    require("Branch mispredictions: 3" in static_run.stdout, "static predictor golden stats mismatch")
    require("Branch mispredictions: 2" in two_bit_run.stdout, "2-bit predictor golden stats mismatch")
    require("Cycles: 18" in static_run.stdout, "static predictor cycle count mismatch")
    require("Cycles: 16" in two_bit_run.stdout, "2-bit predictor cycle count mismatch")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
