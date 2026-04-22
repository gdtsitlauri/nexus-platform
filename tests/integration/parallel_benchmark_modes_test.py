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
    mode = sys.argv[1]
    bin_dir = Path(sys.argv[2])
    if mode == "openmp":
      result = run([str(bin_dir / "openmp-bench"), "--kernel", "all", "--size", "16", "--threads", "2"])
      require(result.returncode == 0, f"openmp benchmark failed: {result.stderr}")
      require("benchmark=openmp kernel=vector-add" in result.stdout, "openmp output missing vector-add")
      require("benchmark=openmp kernel=reduction" in result.stdout, "openmp output missing reduction")
      require("benchmark=openmp kernel=matmul" in result.stdout, "openmp output missing matmul")
      require("benchmark=openmp kernel=branch-mix" in result.stdout, "openmp output missing branch-mix")
      return 0

    if mode == "mpi":
      result = run(
          [
              "mpiexec",
              "--oversubscribe",
              "-n",
              "2",
              str(bin_dir / "mpi-bench"),
              "--kernel",
              "all",
              "--size",
              "8",
          ]
      )
      require(result.returncode == 0, f"mpi benchmark failed: {result.stderr}")
      require("benchmark=mpi kernel=reduce" in result.stdout, "mpi output missing reduce summary")
      require("benchmark=mpi kernel=ring" in result.stdout, "mpi output missing ring summary")
      require("status=ok" in result.stdout, "mpi output missing status")
      return 0

    if mode == "simd":
      result = run([str(bin_dir / "simd-bench"), "--kernel", "all", "--size", "32"])
      require(result.returncode == 0, f"simd benchmark failed: {result.stderr}")
      require("benchmark=simd kernel=vector-add" in result.stdout, "simd output missing vector-add")
      require("benchmark=simd kernel=dot" in result.stdout, "simd output missing dot")
      require("status=ok" in result.stdout, "simd output missing ok status")
      return 0

    raise SystemExit(f"unknown benchmark mode: {mode}")


if __name__ == "__main__":
    raise SystemExit(main())
