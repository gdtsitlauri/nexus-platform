#!/usr/bin/env python3
"""Branch prediction in the Verilog pipelined core (EY321 (e): basic branch prediction).

Every test program runs on the pipelined core with PREDICT = 0 (predict not taken), 1 (static: backward taken,
jumps redirected in IF) and 2 (two-bit counters, jumps redirected in IF).  Prediction may change only the timing:
exit code and retired-instruction count must equal the functional simulator for every predictor, and over all
programs both predictors must save cycles and the two-bit counters must mispredict less than predict-not-taken.
Exit code 77 = skipped (Icarus Verilog not installed).
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, capture_output=True, check=False)


def main() -> int:
    nexusc, mips_sim, repo = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    if shutil.which("iverilog") is None or shutil.which("vvp") is None:
        print("SKIP: iverilog/vvp not found in PATH")
        return 77
    rtl = repo / "src" / "hdl" / "cpu_pipeline"
    programs = sorted((repo / "tests" / "programs").glob("*.nx")) + [repo / "examples" / "source_lang" / "factorial.nx"]
    failures: list[str] = []
    totals = {p: {"cycles": 0, "mispredicts": 0, "branches": 0} for p in (0, 1, 2)}
    with tempfile.TemporaryDirectory() as temp_dir:
        temp = Path(temp_dir)
        images = {}
        for predict in (0, 1, 2):
            image = temp / f"pipeline{predict}.vvp"
            built = run(["iverilog", "-g2012", f"-Pmips_pipeline_tb.PREDICT={predict}", "-o", str(image),
                         str(rtl / "nexus_mips_pipeline.v"), str(rtl / "mips_pipeline_tb.v")])
            if built.returncode != 0:
                print(built.stdout + built.stderr)
                return 1
            images[predict] = image
        for program in programs:
            for allocation in ("none", "linear-scan"):
                asm = temp / f"{program.stem}.{allocation}.s"
                hexfile = temp / f"{program.stem}.{allocation}.hex"
                run([str(nexusc), "compile", str(program), "-S", "--regalloc", allocation, "-o", str(asm)])
                encoded = run([str(mips_sim), "encode", str(asm), "-o", str(hexfile)])
                entry = re.search(r"entry (\d+)", encoded.stderr)
                reference = run([str(mips_sim), "run", str(asm), "--mode", "functional", "--stats"])
                expected_exit = re.search(r"exited with code (-?\d+)", reference.stdout)
                expected_count = re.search(r"^Instructions: (\d+)", reference.stdout, re.M)
                if not (entry and expected_exit and expected_count):
                    failures.append(f"{program.name} [{allocation}]: could not prepare the program")
                    continue
                row = []
                for predict, image in images.items():
                    out = run(["vvp", "-n", str(image), f"+program={hexfile.as_posix()}", f"+entry={entry.group(1)}"]).stdout
                    result = re.search(r"EXIT (-?\d+) INSTRET (\d+) CYCLES (\d+)", out)
                    branch = re.search(r"BRANCHES (\d+) MISPREDICTS (\d+)", out)
                    if not result or not branch:
                        failures.append(f"{program.name} [{allocation}] PREDICT={predict}: no result")
                        continue
                    if result.group(1) != expected_exit.group(1) or result.group(2) != expected_count.group(1):
                        failures.append(f"{program.name} [{allocation}] PREDICT={predict}: {result.groups()} vs "
                                        f"{expected_exit.group(1)}/{expected_count.group(1)}")
                    totals[predict]["cycles"] += int(result.group(3))
                    totals[predict]["branches"] += int(branch.group(1))
                    totals[predict]["mispredicts"] += int(branch.group(2))
                    row.append(f"P{predict}: {result.group(3)} cycles, {branch.group(2)}/{branch.group(1)} mispredicted")
                print(f"{program.name:24} {allocation:12} " + "; ".join(row))
    for predict in (0, 1, 2):
        t = totals[predict]
        print(f"PREDICT={predict}: {t['cycles']} cycles, {t['mispredicts']} of {t['branches']} branches mispredicted")
    if not (totals[1]["cycles"] < totals[0]["cycles"] and totals[2]["cycles"] < totals[0]["cycles"]):
        failures.append("prediction did not save cycles over predict-not-taken")
    if not totals[2]["mispredicts"] < totals[0]["mispredicts"]:
        failures.append("two-bit counters did not mispredict less than predict-not-taken")
    for failure in failures:
        print("FAIL:", failure)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
