#!/usr/bin/env python3
"""Integration test of nexus-os (EY517) and of the round-robin kernel on the NEXUS pipeline.
Usage: os_cli_test.py <nexus-os> <mips-sim> <project root>
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

OS_CLI, MIPS_SIM, ROOT = sys.argv[1], sys.argv[2], Path(sys.argv[3])
EX = ROOT / "examples" / "os"
failures = 0


def run(exe: str, *args: str, ok=(0,)) -> str:
    r = subprocess.run([exe, *args], text=True, capture_output=True, check=False)
    if r.returncode not in ok:
        raise SystemExit(f"{exe} {' '.join(args)} exited {r.returncode}\n{r.stdout}{r.stderr}")
    return r.stdout


def expect(cond: bool, what: str) -> None:
    global failures
    if not cond:
        failures += 1
        print("FAIL:", what)


out = run(OS_CLI, "schedule", str(EX / "stallings.jobs"), "--policy", "srt")
expect("mean turnaround 7.2" in out, "SRT mean turnaround on Stallings' example (7.2)")
out = run(OS_CLI, "check", str(EX / "race.conc"), ok=(2,))
expect("final values of the asserted variable: 1 2" in out and "counterexample" in out, "race condition found")
out = run(OS_CLI, "check", str(EX / "peterson.conc"))
expect("mutual exclusion: holds" in out and "deadlock freedom: holds" in out, "Peterson verified")
out = run(OS_CLI, "check", str(EX / "philosophers.conc"), ok=(2,))
expect("deadlock freedom: VIOLATED" in out, "dining philosophers deadlock found")
out = run(OS_CLI, "banker")
expect("P1 requests (1 0 2): granted" in out and "unsafe state" in out, "Banker's algorithm")
out = run(OS_CLI, "pages", "3", *"1 2 3 4 1 2 5 1 2 3 4 5".split())
expect("FIFO: 9 faults" in out and "OPT: 7 faults" in out, "page replacement on Belady's string")
out = run(OS_CLI, "disk", "100", *"55 58 39 18 90 160 150 38 184".split())
expect("SSTF: movement 248" in out and "optimal movement (exhaustive): 248" in out, "disk scheduling")
out = run(OS_CLI, "fs")
expect("fsck: clean" in out, "file system demo")
# the round-robin kernel on the pipelined MIPS model
out = run(MIPS_SIM, "run", str(EX / "rr_kernel.s"), "--mode", "pipeline", "--io-demo", "--interrupt-demo", "--stats")
expect("Program exited with code 9" in out and "Interrupts handled: 9" in out,
       "round-robin kernel: 9 timer slices with context switches, every task progressed")
dispatch = "".join(chr(int(v)) for v in re.findall(r"io\[console\]: value=(\d+)", out))
gantt = "".join(re.findall(r"\[\d+-\d+ (\w+)\]", run(OS_CLI, "schedule", str(EX / "rr_kernel.jobs"), "--policy", "rr",
                                                      "--quantum", "1")))
expect(dispatch == gantt == "012012121", f"kernel dispatch order {dispatch} equals the RR Gantt chart {gantt}")

if failures:
    print(f"{failures} failure(s)")
    sys.exit(1)
print("os CLI integration test passed")
