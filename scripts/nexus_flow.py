#!/usr/bin/env python3
"""Follow one sum through the six courses with the two built NEXUS tools.

  python scripts/nexus_flow.py build/bin
  python scripts/nexus_flow.py build/bin --console
  python scripts/nexus_flow.py build/bin --console --require-rtl

Default: sum 1..8 = 36, compiler representations, CPU models and a separate
parallel implementation of the same fixed work. --console uses the longer
factorial/console example and adds optional Verilog/interrupt checks.
Only --require-rtl can skip with code 77 when Icarus Verilog is absent.
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HDL = ROOT / "src/hdl/cpu_pipeline"
INTERRUPT_DEMO = ROOT / "tests/golden/interrupt_demo.s"

STAGES = [
    ("4. EY321: same assembly, different datapaths and memory", [
        ["--mode", "functional"],
        ["--mode", "single-cycle"],
        ["--mode", "multi-cycle", "--control", "hardwired"],
        ["--mode", "multi-cycle", "--control", "microcode"],
        ["--mode", "pipeline", "--io-demo"],
        ["--mode", "pipeline", "--io-demo", "--cache", "assoc"],
    ]),
    ("5. NEY606: same answer, different instruction schedules", [
        ["--mode", "advanced", "--scheduler", "inorder"],
        ["--mode", "advanced", "--scheduler", "scoreboard"],
        ["--mode", "advanced", "--scheduler", "tomasulo", "--issue-width", "4", "--predictor", "2bit"],
    ]),
]


def require(ok: bool, message: str) -> None:
    if not ok:
        raise RuntimeError(message)


def command(*args: str | Path) -> str:
    result = subprocess.run([str(a) for a in args], text=True, capture_output=True, check=False,
                            encoding="utf-8", errors="replace", timeout=30, cwd=ROOT)
    output = result.stdout + result.stderr
    require(result.returncode == 0, f"{' '.join(map(str, args))} failed\n{output}")
    return output


def console_text(output: str) -> str:
    return "".join("\\n" if value == "10" else chr(int(value))
                   for value in re.findall(r"io\[console\]: value=(\d+)", output))


def simulate(sim: Path, assembly: Path, value: int, text: str, options: list[str]) -> str:
    output = command(sim, "run", assembly, *options, "--stats")
    code = re.search(r"Program exited with code (-?\d+)", output)
    require(code is not None and int(code.group(1)) == value and console_text(output) == text,
            f"observable result changed: {assembly.name} {' '.join(options)}\n{output}")
    return output


def metric(output: str, name: str) -> int:
    match = re.search(rf"^{re.escape(name)}: (\d+)", output, re.M)
    require(match is not None, f"missing {name}\n{output}")
    return int(match.group(1))


def encode(sim: Path, program: Path, target: Path) -> tuple[int, int, int]:
    output = command(sim, "encode", program, "-o", target)
    entry = re.search(r"entry (\d+)", output)
    require(entry is not None, f"missing entry point\n{output}")
    handler = re.search(r"handler (\d+)", output)
    words = sum(bool(line.strip()) for line in target.read_text().splitlines())
    return int(entry.group(1)), int(handler.group(1)) if handler else 0, words


def rtl(image: Path, program: Path, entry: int, handler: int) -> dict[str, str]:
    output = command("vvp", "-n", image, f"+program={program.as_posix()}", f"+entry={entry}", f"+handler={handler}")
    text = re.search(r"^CONSOLE (.*)$", output, re.M)
    result = re.search(r"EXIT (-?\d+) INSTRET (\d+) INTERRUPTS (\d+) POLLS (\d+) CYCLES (\d+)", output)
    require(text is not None and result is not None, f"incomplete RTL result\n{output}")
    code, instructions, interrupts, polls, cycles = result.groups()
    return dict(text=text.group(1), code=code, instructions=instructions, interrupts=interrupts,
                polls=polls, cycles=cycles)


def check_rtl(sim: Path, work: Path, program: Path, entry: int, want_text: str, want_code: int) -> None:
    print("Further lab: Verilog CPU, console polling and a timer interrupt")
    image = work / "io.vvp"
    command("iverilog", "-g2012", "-o", image, HDL / "nexus_mips_pipeline.v",
            HDL / "nexus_io_devices.v", HDL / "cpu_io_tb.v")
    got = rtl(image, program, entry, 0)
    require(got["text"] == want_text and got["code"] == str(want_code), f"RTL console mismatch: {got}")
    print(f"  ok: console {want_text!r}, result {want_code}, status polls {got['polls']}")
    irq_program = work / "interrupt.hex"
    irq_entry, handler, _ = encode(sim, INTERRUPT_DEMO, irq_program)
    reference = command(sim, "run", INTERRUPT_DEMO, "--mode", "pipeline", "--interrupt-demo", "--stats")
    code = re.search(r"Program exited with code (-?\d+)", reference)
    got = rtl(image, irq_program, irq_entry, handler)
    require(code is not None and got["code"] == code.group(1) and got["interrupts"] == "1",
            f"RTL timer mismatch: {got}\n{reference}")
    print(f"  ok: timer interrupt, RTL and simulator result {got['code']}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bindir", type=Path, help="directory containing nexusc and mips-sim")
    parser.add_argument("--console", action="store_true", help="use the longer factorial/console example")
    parser.add_argument("--require-rtl", action="store_true", help="require Verilog; implies --console; skip with 77 if absent")
    args = parser.parse_args()
    bindir = args.bindir.resolve()
    extension = ".exe" if (bindir / "nexusc.exe").exists() else ""
    compiler, sim = (bindir / f"{name}{extension}" for name in ("nexusc", "mips-sim"))
    require(compiler.is_file() and sim.is_file(), f"build nexusc and mips-sim first; not found in {bindir}")
    has_rtl = shutil.which("iverilog") is not None and shutil.which("vvp") is not None
    if args.require_rtl and not has_rtl:
        print("SKIP required RTL lab: install Icarus Verilog (iverilog and vvp).")
        return 77
    use_console = args.console or args.require_rtl
    source = ROOT / "examples/flow" / ("flow_demo.nx" if use_console else "sum.nx")
    want_code, want_text = (25, "S=5913\\n") if use_console else (36, "")
    print(f"NEXUS flow: {source.relative_to(ROOT)}; expected result {want_code}")

    with tempfile.TemporaryDirectory(prefix="nexus-flow-") as tmp:
        work = Path(tmp)
        print("1. NEY613: source -> tokens -> checked syntax -> IR -> MIPS")
        for phase in ("lex", "ast", "check", "ir"):
            command(compiler, phase, source)
        assembly = {}
        for allocation in ("none", "linear-scan"):
            target = work / f"flow_{allocation}.s"
            command(compiler, "compile", source, "-S", "--regalloc", allocation, "-o", target)
            assembly[allocation] = target
        print("  ok: frontend phases; assembly with stack slots and register allocation")

        print("2. NEY709: analyse that IR; preserve the meaning of the program")
        ssa = command(compiler, "analysis", "ssa", source)
        require("phi" in ssa, "loop SSA should contain phi nodes")
        command(compiler, "analysis", "sccp", source)
        print("  ok: SSA and SCCP reports (analysis does not itself rewrite emitted MIPS)")
        if not use_console:
            transformed = command(compiler, "opt", source, "--pass", "unroll")
            require("fully unrolled loop with trip count 8" in transformed,
                    f"expected eight unrolled iterations\n{transformed}")
            for transformation in ("cse", "unroll"):
                target = work / f"flow_{transformation}.s"
                command(compiler, "compile", source, "-S", "--pass", transformation,
                        "--regalloc", "linear-scan", "-o", target)
                assembly[f"linear-scan+{transformation}"] = target
            print("  ok: CSE and unrolling also feed real MIPS generation via compile --pass")

        print("3. NEY221: assembly -> encoded instruction words")
        hexfile = work / "flow.hex"
        entry, _, words = encode(sim, assembly["linear-scan"], hexfile)
        require(words > 0, "empty machine-code image")
        print(f"  ok: {words} MIPS32 words, entry instruction index {entry}")

        for title, models in STAGES:
            print(title)
            for allocation, program in assembly.items():
                for model in models:
                    output = simulate(sim, program, want_code, want_text, model)
                    print(f"  ok: {' '.join(model)} [{allocation}]: result {want_code}, {metric(output, 'Cycles')} cycles")

        print("6. NEY704: " + ("return to " if use_console else "explicitly split ") + "the fixed sum 1..8 among workers")
        print("  This is a separate parallel program for the same problem, not automatic parallelisation.")
        for coherence in ("snoop", "directory-lite"):
            for cores in (1, 2, 4, 8):
                output = simulate(sim, ROOT / "examples/flow/sum_parallel.s", 36, "",
                                  ["--mode", "parallel", "--cores", str(cores), "--coherence", coherence])
                require(metric(output, "Cores") == cores and metric(output, "Lock acquisitions") == cores,
                        f"each worker must participate in the reduction\n{output}")
                print(f"  ok: {cores} worker(s), {coherence}: sum=36, {metric(output, 'Cycles')} cycles")
        print("  More workers need not be faster: this tiny sum exposes lock/barrier overhead.")

        if use_console:
            if has_rtl:
                check_rtl(sim, work, hexfile, entry, want_text, want_code)
            else:
                print("SKIP optional RTL lab: Icarus Verilog absent; all compiler/CPU/parallel checks above ran.")
    print("PASS: representations and CPU models preserve the result; parallel workers reproduce the fixed sum.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        raise SystemExit(f"FAIL: {error}")
