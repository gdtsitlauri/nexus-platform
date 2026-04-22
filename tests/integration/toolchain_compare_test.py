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
    repo = Path(sys.argv[2])
    script = repo / "tools" / "toolchain_compare" / "run_toolchain_compare.py"

    with tempfile.TemporaryDirectory() as temp_dir:
        output_dir = Path(temp_dir)
        result = run([sys.executable, str(script), str(nexusc), str(repo), str(output_dir)])
        require(result.returncode == 0, result.stderr or "toolchain comparison script failed")
        require(
            "Generated toolchain comparison report" in result.stdout,
            "toolchain comparison script did not report successful completion",
        )

        summary_md = output_dir / "toolchain_comparison_summary.md"
        summary_json = output_dir / "toolchain_comparison_summary.json"
        require(summary_md.exists(), "toolchain comparison summary markdown was not generated")
        require(summary_json.exists(), "toolchain comparison summary json was not generated")

        summary_text = summary_md.read_text()
        require("Toolchain Comparison Summary" in summary_text, "summary markdown missing title")
        require("nexusc compile" in summary_text, "summary markdown missing nexusc command details")
        require("clang -S -O0" in summary_text, "summary markdown missing clang O0 details")
        require("clang -S -O1" in summary_text, "summary markdown missing clang O1 details")
        require("interproc-constfold" in summary_text, "summary markdown missing pass comparison")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
