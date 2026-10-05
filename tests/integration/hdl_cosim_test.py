#!/usr/bin/env python3
"""Co-simulation of the Verilog 5-stage pipelined MIPS core against the C++ functional simulator.

Every test program is compiled by nexusc (stack-only and linear-scan code), encoded to MIPS32
machine words by `mips-sim encode`, executed on the RTL core with Icarus Verilog, and must produce
the same exit code and the same retired-instruction count as `mips-sim --mode functional`.
Optionally (when yosys is installed) the core is synthesised to check that it is synthesisable.
Exit code 77 = skipped (Icarus Verilog not installed).
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def run(cmd: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, capture_output=True, check=False, **kwargs)


def main() -> int:
    nexusc, mips_sim, repo = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    if shutil.which("iverilog") is None or shutil.which("vvp") is None:
        print("SKIP: iverilog/vvp not found in PATH")
        return 77
    rtl = repo / "src" / "hdl" / "cpu_pipeline"
    programs = sorted((repo / "tests" / "programs").glob("*.nx")) + [repo / "examples" / "source_lang" / "factorial.nx"]
    failures: list[str] = []
    with tempfile.TemporaryDirectory() as temp_dir:
        temp = Path(temp_dir)
        image = temp / "cpu.vvp"
        built = run(["iverilog", "-g2012", "-o", str(image), str(rtl / "nexus_mips_pipeline.v"), str(rtl / "mips_pipeline_tb.v")])
        if built.returncode != 0:
            print(built.stdout + built.stderr)
            return 1
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
                    failures.append(f"{program.name} [{allocation}]: encode failed: {encoded.stderr}")
                    continue
                reference = run([str(mips_sim), "run", str(asm), "--mode", "functional", "--stats"])
                expected_exit = re.search(r"exited with code (-?\d+)", reference.stdout)
                expected_count = re.search(r"^Instructions: (\d+)", reference.stdout, re.M)
                hardware = run(["vvp", "-n", str(image), f"+program={hexfile.as_posix()}", f"+entry={entry.group(1)}"])
                outcome = re.search(r"EXIT (-?\d+) INSTRET (\d+) CYCLES (\d+)", hardware.stdout)
                if not (expected_exit and expected_count and outcome):
                    failures.append(f"{program.name} [{allocation}]: no result\n{hardware.stdout}")
                    continue
                same = outcome.group(1) == expected_exit.group(1) and outcome.group(2) == expected_count.group(1)
                print(f"{program.name:24} {allocation:12} rtl exit={outcome.group(1)} instret={outcome.group(2)} "
                      f"cycles={outcome.group(3)}  {'OK' if same else 'MISMATCH'}")
                if not same:
                    failures.append(f"{program.name} [{allocation}]: rtl {outcome.groups()} vs sim "
                                    f"{expected_exit.group(1)}/{expected_count.group(1)}")
    if shutil.which("yosys") is not None:
        synth = run(["yosys", "-p",
                     f"read_verilog {(rtl / 'nexus_mips_pipeline.v').as_posix()}; "
                     "chparam -set IMEM_WORDS 32 -set DMEM_WORDS 32 nexus_mips_pipeline; "
                     "synth -top nexus_mips_pipeline -flatten -run begin:fine; stat"])
        cells = re.search(r"(\d+)\s+cells", synth.stdout)
        if synth.returncode != 0 or cells is None:
            failures.append("yosys could not synthesise the core:\n" + synth.stdout + synth.stderr)
        else:
            print(f"yosys: the core synthesises ({cells.group(1)} coarse cells)")
    for failure in failures:
        print("FAIL:", failure)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
