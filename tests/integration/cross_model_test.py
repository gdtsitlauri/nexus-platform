#!/usr/bin/env python3
"""Differential test: every program, compiled with and without register allocation, must exit with
the same code on every CPU model as on the functional reference interpreter.

It also checks that linear-scan allocation never increases the dynamic instruction count.
"""
from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path

MODELS: list[tuple[str, list[str]]] = [
    ("single-cycle", ["--mode", "single-cycle"]),
    ("multi-cycle-hardwired", ["--mode", "multi-cycle", "--control", "hardwired"]),
    ("multi-cycle-microcode", ["--mode", "multi-cycle", "--control", "microcode"]),
    ("pipeline", ["--mode", "pipeline"]),
    ("pipeline-btfnt", ["--mode", "pipeline", "--predictor", "static-btfnt"]),
    ("pipeline-direct-cache", ["--mode", "pipeline", "--cache", "direct"]),
    ("pipeline-assoc-l2", ["--mode", "pipeline", "--cache", "assoc", "--ways", "2", "--cache-l2", "direct"]),
    ("advanced-2bit", ["--mode", "advanced", "--predictor", "2bit"]),
    ("advanced-scoreboard", ["--mode", "advanced", "--scheduler", "scoreboard"]),
    ("advanced-dual-issue", ["--mode", "advanced", "--issue-width", "2"]),
    ("advanced-tomasulo", ["--mode", "advanced", "--scheduler", "tomasulo", "--predictor", "2bit"]),
    ("parallel-1-core", ["--mode", "parallel", "--cores", "1"]),
]

EXIT_RE = re.compile(r"Program exited with code (-?\d+)")
INSTR_RE = re.compile(r"^Instructions: (\d+)", re.M)


def run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, capture_output=True, check=False)


def exit_code(result: subprocess.CompletedProcess[str]) -> str:
    match = EXIT_RE.search(result.stdout)
    return match.group(1) if match else f"<no exit: {(result.stderr or result.stdout).strip()[:120]}>"


def main() -> int:
    nexusc, mips_sim, repo = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    programs = sorted((repo / "tests" / "programs").glob("*.nx"))
    programs += [
        repo / "examples" / "source_lang" / name
        for name in ("factorial.nx", "arrays_and_loops.nx", "fixed_trip_unroll.nx", "interproc_fold.nx", "symbolic_unroll.nx")
    ]
    failures: list[str] = []
    checked = 0
    with tempfile.TemporaryDirectory() as temp_dir:
        for program in programs:
            counts: dict[str, int] = {}
            for allocation in ("none", "linear-scan"):
                asm = Path(temp_dir) / f"{program.stem}.{allocation}.s"
                compiled = run([str(nexusc), "compile", str(program), "-S", "--regalloc", allocation, "-o", str(asm)])
                if compiled.returncode != 0:
                    failures.append(f"{program.name} [{allocation}]: compile failed: {compiled.stderr.strip()}")
                    continue
                reference = run([str(mips_sim), "run", str(asm), "--mode", "functional", "--stats"])
                expected = exit_code(reference)
                instructions = INSTR_RE.search(reference.stdout)
                if instructions:
                    counts[allocation] = int(instructions.group(1))
                for name, args in MODELS:
                    got = exit_code(run([str(mips_sim), "run", str(asm), *args]))
                    checked += 1
                    if got != expected:
                        failures.append(f"{program.name} [{allocation}] {name}: expected {expected}, got {got}")
            if len(counts) == 2 and counts["linear-scan"] > counts["none"]:
                failures.append(f"{program.name}: linear-scan executed more instructions ({counts})")
            if len(counts) == 2:
                saved = 100.0 * (counts["none"] - counts["linear-scan"]) / counts["none"]
                print(f"{program.name}: {counts['none']} -> {counts['linear-scan']} instructions ({saved:.1f}% fewer)")

    print(f"checked {checked} (program, allocation, model) runs")
    for failure in failures:
        print("FAIL:", failure)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
