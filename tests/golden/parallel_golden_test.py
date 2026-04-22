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

    coherence_demo = repo / "tests" / "golden" / "parallel_coherence_demo.s"
    consistency_demo = repo / "tests" / "golden" / "parallel_consistency_demo.s"
    atomic_demo = repo / "tests" / "golden" / "parallel_atomic_demo.s"

    coherence_run = run(
        [
            str(mips_sim),
            "run",
            str(coherence_demo),
            "--mode",
            "parallel",
            "--cores",
            "2",
            "--coherence",
            "snoop",
            "--consistency",
            "sc",
            "--interconnect",
            "bus",
            "--stats",
        ]
    )
    require(coherence_run.returncode == 0, "parallel coherence golden run failed")
    require("Mode: parallel" in coherence_run.stdout, "parallel coherence summary missing mode")
    require("Cores: 2" in coherence_run.stdout, "parallel coherence summary missing core count")
    require("Coherence: snoop" in coherence_run.stdout, "parallel coherence summary missing coherence")
    require("Program exited with code 7" in coherence_run.stdout, "parallel coherence summary missing exit code")

    consistency_run = run(
        [
            str(mips_sim),
            "run",
            str(consistency_demo),
            "--mode",
            "parallel",
            "--cores",
            "2",
            "--consistency",
            "weak-lite",
            "--stats",
        ]
    )
    require(consistency_run.returncode == 0, "parallel consistency golden run failed")
    require("Consistency: weak-lite" in consistency_run.stdout, "parallel consistency summary missing model")
    require("Store-buffer flushes: 2" in consistency_run.stdout, "parallel consistency summary missing flushes")
    require("Program exited with code 1" in consistency_run.stdout, "parallel consistency summary missing exit code")

    atomic_run = run(
        [
            str(mips_sim),
            "run",
            str(atomic_demo),
            "--mode",
            "parallel",
            "--cores",
            "2",
            "--trace",
        ]
    )
    require(atomic_run.returncode == 0, "parallel atomic trace run failed")
    require(
        "sync[atomic-fetch-inc]: core0 value=0" in atomic_run.stdout,
        "parallel atomic trace missing core0 fetch-inc event",
    )
    require(
        "sync[atomic-fetch-inc]: core1 value=1" in atomic_run.stdout,
        "parallel atomic trace missing core1 fetch-inc event",
    )
    require("Program exited with code 1" in atomic_run.stdout, "parallel atomic trace missing final result")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
