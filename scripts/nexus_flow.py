#!/usr/bin/env python3
"""The NEXUS flow through the six courses, in one run.

  nexus_flow.py <directory of the NEXUS executables, e.g. build/bin>

One NexusLang program (examples/flow/flow_demo.nx) goes through every course and must give the same observable
result everywhere: the text it sends to the console and its exit code.

 1. NEY613 Compilers                  NexusLang -> IR -> MIPS assembly, with and without linear-scan allocation
 2. NEY709 Advanced Compiler Topics   SSA form and sparse conditional constant propagation of the same program
 3. NEY221 Principles of Operation    the assembly encoded as MIPS32 machine words
 4. EY321  Computer Organization      single-cycle and multi-cycle CPUs (hardwired and microprogrammed control),
                                      the 5-stage pipeline with caches and memory-mapped I/O, and the Verilog CPU
                                      with its console (polling) and timer (interrupt) on the I/O bus
 5. NEY606 Computer Architecture      in-order, scoreboard and Tomasulo out-of-order cores
 6. NEY704 Parallel Systems           the multicore model

Exit codes: 0 every stage agreed, 1 a mismatch, 77 Icarus Verilog missing.
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "examples" / "flow" / "flow_demo.nx"
INTERRUPT_DEMO = ROOT / "tests" / "golden" / "interrupt_demo.s"
HDL = ROOT / "src" / "hdl" / "cpu_pipeline"
WANT_TEXT = "S=5913\\n"
WANT_EXIT = 5913 % 256

STAGES = [
    ("4. EY321 Computer Organization", [
        ["--mode", "functional"],
        ["--mode", "single-cycle"],
        ["--mode", "multi-cycle", "--control", "hardwired"],
        ["--mode", "multi-cycle", "--control", "microcode"],
        ["--mode", "pipeline", "--io-demo"],
        ["--mode", "pipeline", "--io-demo", "--cache", "assoc"],
    ]),
    ("5. NEY606 Computer Architecture", [
        ["--mode", "advanced", "--scheduler", "inorder"],
        ["--mode", "advanced", "--scheduler", "scoreboard"],
        ["--mode", "advanced", "--scheduler", "tomasulo", "--issue-width", "4", "--predictor", "2bit"],
    ]),
    ("6. NEY704 Parallel Systems", [
        ["--mode", "parallel", "--cores", "1"],
    ]),
]

failures = 0


def sh(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(list(args), text=True, capture_output=True, check=False)


def check(ok: bool, stage: str, detail: str) -> None:
    global failures
    failures += 0 if ok else 1
    print(f"  {'ok  ' if ok else 'FAIL'} {stage:58s} {detail}")


def console_text(out: str) -> str:
    return "".join("\\n" if v == "10" else chr(int(v)) for v in re.findall(r"io\[console\]: value=(\d+)", out))


def encode(mips_sim: str, program: Path, out: Path) -> tuple[int, int, int]:
    r = sh(mips_sim, "encode", str(program), "-o", str(out))
    if r.returncode != 0:
        raise SystemExit(f"encode failed: {r.stderr}")
    entry = int(re.search(r"entry (\d+)", r.stderr).group(1))
    handler = re.search(r"handler (\d+)", r.stderr)
    words = sum(1 for line in out.read_text().splitlines() if line.strip())
    return entry, int(handler.group(1)) if handler else 0, words


def rtl(image: Path, hexfile: Path, entry: int, handler: int) -> dict[str, str]:
    r = sh("vvp", "-n", str(image), f"+program={hexfile.as_posix()}", f"+entry={entry}", f"+handler={handler}")
    text = re.search(r"^CONSOLE (.*)$", r.stdout, re.M)
    result = re.search(r"EXIT (-?\d+) INSTRET (\d+) INTERRUPTS (\d+) POLLS (\d+) CYCLES (\d+)", r.stdout)
    if not text or not result:
        return {"text": "?", "exit": "?", "instret": "?", "interrupts": "?", "polls": "?", "cycles": "?"}
    exit_code, instret, interrupts, polls, cycles = result.groups()
    return {"text": text.group(1), "exit": exit_code, "instret": instret, "interrupts": interrupts, "polls": polls,
            "cycles": cycles}


def main() -> int:
    bindir = Path(sys.argv[1])
    exe = ".exe" if (bindir / "nexusc.exe").exists() else ""
    nexusc, mips_sim = str(bindir / f"nexusc{exe}"), str(bindir / f"mips-sim{exe}")
    if shutil.which("iverilog") is None or shutil.which("vvp") is None:
        print("SKIP: iverilog not found")
        return 77

    with tempfile.TemporaryDirectory() as temp_dir:
        work = Path(temp_dir)
        print("1. NEY613 Compilers: NexusLang -> MIPS assembly")
        asm = {}
        for alloc in ("none", "linear-scan"):
            out = work / f"flow_{alloc}.s"
            r = sh(nexusc, "compile", str(SOURCE), "-S", "--regalloc", alloc, "-o", str(out))
            check(r.returncode == 0, f"nexusc compile --regalloc {alloc}", out.name)
            asm[alloc] = out

        print("2. NEY709 Advanced Compiler Topics: analysis of the same program")
        r = sh(nexusc, "analysis", "ssa", str(SOURCE))
        check(r.returncode == 0 and "phi" in r.stdout, "SSA form", "phi nodes present")
        r = sh(nexusc, "analysis", "sccp", str(SOURCE))
        check(r.returncode == 0, "sparse conditional constant propagation", "runs on the IR")

        print("3. NEY221 Principles of Computer Operation: MIPS32 machine code")
        hexfile = work / "flow.hex"
        entry, _, words = encode(mips_sim, asm["linear-scan"], hexfile)
        check(words > 0, "mips-sim encode", f"{words} machine words, entry {entry}")

        for title, models in STAGES:
            print(title + ": the same program, the same result")
            for alloc, path in asm.items():
                for model in models:
                    r = sh(mips_sim, "run", str(path), *model)
                    out = r.stdout + r.stderr
                    code = re.search(r"Program exited with code (-?\d+)", out)
                    text = console_text(out)
                    ok = code is not None and int(code.group(1)) == WANT_EXIT and text == WANT_TEXT
                    name = " ".join(m for m in model[1:] if m not in ("--io-demo", "--control", "--scheduler"))
                    check(ok, f"{name} [{alloc}]", f"console '{text}' exit {code.group(1) if code else '?'}")
            if title.startswith("4."):
                image = work / "io.vvp"
                built = sh("iverilog", "-g2012", "-o", str(image), str(HDL / "nexus_mips_pipeline.v"),
                           str(HDL / "nexus_io_devices.v"), str(HDL / "cpu_io_tb.v"))
                if built.returncode != 0:
                    print(built.stdout + built.stderr)
                    return 1
                got = rtl(image, hexfile, entry, 0)
                check(got["text"] == WANT_TEXT and got["exit"] == str(WANT_EXIT),
                      "Verilog CPU, console polled on the I/O bus",
                      f"console '{got['text']}' exit {got['exit']}, {got['polls']} status polls")
                irq_hex = work / "interrupt.hex"
                irq_entry, irq_handler, _ = encode(mips_sim, INTERRUPT_DEMO, irq_hex)
                sim = sh(mips_sim, "run", str(INTERRUPT_DEMO), "--mode", "pipeline", "--interrupt-demo", "--stats")
                sim_out = sim.stdout + sim.stderr
                sim_code = re.search(r"Program exited with code (-?\d+)", sim_out)
                got = rtl(image, irq_hex, irq_entry, irq_handler)
                check(sim_code is not None and got["exit"] == sim_code.group(1) and got["interrupts"] == "1",
                      "timer interrupt on the Verilog CPU = simulator",
                      f"exit {got['exit']} (simulator {sim_code.group(1) if sim_code else '?'}), "
                      f"{got['interrupts']} interrupt")

    print("PASS" if failures == 0 else f"FAIL ({failures})")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
