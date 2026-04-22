#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: gpu_optional_test.py <bin-dir> <repo-root>")

    bin_dir = Path(sys.argv[1]).resolve()
    repo_root = Path(sys.argv[2]).resolve()
    parallel_bench = repo_root / "parallel-bench"

    result = subprocess.run(
        [
            str(parallel_bench),
            "--gpu",
            "--build-dir",
            str(bin_dir.parent),
            "--repo-root",
            str(repo_root),
        ],
        text=True,
        capture_output=True,
        check=False,
    )
    require(result.returncode == 0, result.stderr)
    require("suite=gpu" in result.stdout, "missing gpu suite line")
    require("status=ok" in result.stdout or "status=skipped" in result.stdout, result.stdout)

    gpu_bench = bin_dir / "gpu-bench"
    if not gpu_bench.exists():
        require("reason=cuda-disabled" in result.stdout, result.stdout)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
