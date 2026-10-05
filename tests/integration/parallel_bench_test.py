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
    bin_dir = Path(sys.argv[1])
    repo = Path(sys.argv[2])
    wrapper = repo / "parallel-bench"

    with tempfile.TemporaryDirectory() as temp_dir:
        temp = Path(temp_dir)
        csv_path = temp / "parallel_bench.csv"
        md_path = temp / "parallel_bench.md"
        result = run(
            [
                sys.executable,
                str(repo / "scripts" / "parallel_bench.py"),
                "--all",
                "--build-dir",
                str(bin_dir.parent),
                "--repo-root",
                str(repo),
                "--csv",
                str(csv_path),
                "--markdown",
                str(md_path),
            ]
        )
        require(result.returncode == 0, f"parallel-bench wrapper failed: {result.stderr}")
        require(csv_path.exists(), "parallel-bench CSV was not created")
        require(md_path.exists(), "parallel-bench markdown summary was not created")
        csv_text = csv_path.read_text()
        require("openmp" in csv_text, "parallel-bench CSV missing openmp rows")
        require("mpi" in csv_text, "parallel-bench CSV missing mpi rows")
        require("simd" in csv_text, "parallel-bench CSV missing simd rows")
        require("parallel" in csv_text, "parallel-bench CSV missing parallel rows")
        md_text = md_path.read_text()
        require("# Phase 11 Parallel Benchmark Summary" in md_text, "parallel-bench markdown missing title")
        require("| openmp |" in md_text, "parallel-bench markdown missing openmp summary")
        require("| parallel |" in md_text, "parallel-bench markdown missing parallel summary")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
