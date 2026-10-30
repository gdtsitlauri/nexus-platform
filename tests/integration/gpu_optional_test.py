#!/usr/bin/env python3
"""The CUDA example (NEY704): when it was built (-DNEXUS_ENABLE_CUDA=ON), its GPU results must equal the CPU ones."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: gpu_optional_test.py <bin-dir> <repo-root>")
    gpu_bench = Path(sys.argv[1]).resolve() / "gpu-bench"
    if not gpu_bench.exists() and not gpu_bench.with_suffix(".exe").exists():
        print("gpu-bench not built (configure with -DNEXUS_ENABLE_CUDA=ON on a machine with nvcc): skipped")
        return 77
    result = subprocess.run([str(gpu_bench)], text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise SystemExit(result.stdout + result.stderr)
    print(result.stdout)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
