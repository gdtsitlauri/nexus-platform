#!/usr/bin/env python3
"""Co-simulation of the Verilog multi-cycle MIPS core, with hardwired and with microprogrammed control.

Every test program is compiled by nexusc (stack-only and linear-scan code), encoded by `mips-sim encode`, run on
the core with Icarus Verilog for MICROCODE = 0 and MICROCODE = 1, and must give the functional simulator's exit
code and retired-instruction count.  The two control units must also take exactly the same number of cycles,
with a CPI between 3 and 5.  When Yosys is installed both versions are synthesised.
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
    rtl = repo / "src" / "hdl" / "cpu_multicycle"
    programs = sorted((repo / "tests" / "programs").glob("*.nx")) + [repo / "examples" / "source_lang" / "factorial.nx"]
    failures: list[str] = []
    with tempfile.TemporaryDirectory() as temp_dir:
        temp = Path(temp_dir)
        images = {}
        for microcode in (0, 1):
            image = temp / f"multicycle{microcode}.vvp"
            built = run(["iverilog", "-g2012", f"-Pmips_multicycle_tb.MICROCODE={microcode}", "-o", str(image),
                         str(rtl / "nexus_mips_multicycle.v"), str(rtl / "mips_multicycle_tb.v")])
            if built.returncode != 0:
                print(built.stdout + built.stderr)
                return 1
            images[microcode] = image
        for program in programs:
            for allocation in ("none", "linear-scan"):
                asm = temp / f"{program.stem}.{allocation}.s"
                hexfile = temp / f"{program.stem}.{allocation}.hex"
                if run([str(nexusc), "compile", str(program), "-S", "--regalloc", allocation, "-o", str(asm)]).returncode != 0:
                    failures.append(f"{program.name} [{allocation}]: compile failed")
                    continue
                encoded = run([str(mips_sim), "encode", str(asm), "-o", str(hexfile)])
                entry = re.search(r"entry (\d+)", encoded.stderr)
                if encoded.returncode != 0 or entry is None:
                    failures.append(f"{program.name} [{allocation}]: encode failed")
                    continue
                reference = run([str(mips_sim), "run", str(asm), "--mode", "functional", "--stats"])
                expected_exit = re.search(r"exited with code (-?\d+)", reference.stdout)
                expected_count = re.search(r"^Instructions: (\d+)", reference.stdout, re.M)
                outcomes = {}
                for microcode, image in images.items():
                    hardware = run(["vvp", "-n", str(image), f"+program={hexfile.as_posix()}", f"+entry={entry.group(1)}"])
                    outcomes[microcode] = re.search(r"EXIT (-?\d+) INSTRET (\d+) CYCLES (\d+)", hardware.stdout)
                if not (expected_exit and expected_count and outcomes[0] and outcomes[1]):
                    failures.append(f"{program.name} [{allocation}]: no result")
                    continue
                hard, micro = outcomes[0].groups(), outcomes[1].groups()
                same = all(o[0] == expected_exit.group(1) and o[1] == expected_count.group(1) for o in (hard, micro))
                cpi = int(hard[2]) / int(hard[1])
                print(f"{program.name:24} {allocation:12} exit={hard[0]} instret={hard[1]} cycles hardwired={hard[2]} "
                      f"microprogrammed={micro[2]} CPI={cpi:.2f}  {'OK' if same and hard[2] == micro[2] else 'MISMATCH'}")
                if not same:
                    failures.append(f"{program.name} [{allocation}]: {hard} / {micro} vs "
                                    f"{expected_exit.group(1)}/{expected_count.group(1)}")
                if hard[2] != micro[2]:
                    failures.append(f"{program.name} [{allocation}]: the two control units differ in cycles")
                if not 3.0 <= cpi <= 5.0:
                    failures.append(f"{program.name} [{allocation}]: CPI {cpi:.2f} outside 3..5")
    if shutil.which("yosys") is not None:
        for microcode in (0, 1):
            synth = run(["yosys", "-p",
                         f"read_verilog {(rtl / 'nexus_mips_multicycle.v').as_posix()}; "
                         f"chparam -set IMEM_WORDS 32 -set DMEM_WORDS 32 -set MICROCODE {microcode} nexus_mips_multicycle; "
                         "synth -top nexus_mips_multicycle -flatten -run begin:fine; stat"])
            cells = re.search(r"(\d+)\s+cells", synth.stdout)
            if synth.returncode != 0 or cells is None:
                failures.append(f"yosys could not synthesise the core (MICROCODE={microcode}):\n" + synth.stdout[-2000:])
            else:
                print(f"yosys: MICROCODE={microcode} synthesises ({cells.group(1)} coarse cells)")
    for failure in failures:
        print("FAIL:", failure)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
