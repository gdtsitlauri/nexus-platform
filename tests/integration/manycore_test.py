#!/usr/bin/env python3
"""Manycore checks: the SPMD reduction is correct for 1..64 cores on every interconnect, mesh
messages cost more hops as the mesh grows, and slower "little" cores delay the barrier."""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


def run(mips_sim: Path, program: Path, *args: str) -> dict[str, str]:
    result = subprocess.run([str(mips_sim), "run", str(program), "--mode", "parallel", *args, "--stats"],
                            text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise SystemExit(f"run failed: {args}\n{result.stderr}")
    data = {"exit": re.search(r"exited with code (-?\d+)", result.stdout).group(1)}
    for key in ("Cycles", "Interconnect cycles", "Interconnect messages"):
        match = re.search(rf"^{key}: (\d+)", result.stdout, re.M)
        if match:
            data[key] = match.group(1)
    return data


def main() -> int:
    mips_sim, repo = Path(sys.argv[1]), Path(sys.argv[2])
    program = repo / "examples" / "parallel" / "spmd_sum.s"
    failures = []
    for interconnect in ("bus", "switch", "noc-lite", "ring", "mesh"):
        for cores in (1, 2, 4, 8, 16, 32, 64):
            data = run(mips_sim, program, "--cores", str(cores), "--interconnect", interconnect,
                       "--coherence", "directory-lite")
            expected = (8 * cores) * (8 * cores - 1) // 2
            if int(data["exit"]) != expected:
                failures.append(f"{interconnect}/{cores}: expected {expected}, got {data['exit']}")
    small = run(mips_sim, program, "--cores", "4", "--interconnect", "mesh")
    large = run(mips_sim, program, "--cores", "64", "--interconnect", "mesh")
    per_message_small = int(small["Interconnect cycles"]) / max(1, int(small["Interconnect messages"]))
    per_message_large = int(large["Interconnect cycles"]) / max(1, int(large["Interconnect messages"]))
    if not per_message_large > per_message_small:
        failures.append("an 8x8 mesh must need more hops per message than a 2x2 mesh")
    symmetric = run(mips_sim, program, "--cores", "4")
    asymmetric = run(mips_sim, program, "--cores", "4", "--core-cpi", "1,1,4,4")
    if not int(asymmetric["Cycles"]) > int(symmetric["Cycles"]) or asymmetric["exit"] != symmetric["exit"]:
        failures.append("little cores must slow the barrier without changing the result")
    for failure in failures:
        print("FAIL:", failure)
    print(f"manycore: {35 - len(failures)} configurations checked")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
