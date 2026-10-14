#!/usr/bin/env python3
"""The NEXUS flow from a source program to hardware, in one run.

  nexus_flow.py <directory of the NEXUS executables, e.g. build/bin> [--gates] [--fpga]

One NexusLang program (examples/flow/flow_demo.nx) goes through every layer and must give the same observable
result everywhere: the text it sends to the UART and its exit code.

 1. compiler      NexusLang -> IR/SSA -> MIPS assembly, with and without linear-scan register allocation
 2. CPU models    the same binary on every execution model (ISA level, single/multi-cycle, pipeline with
                  caches, in-order/scoreboard/Tomasulo out-of-order, multicore)
 3. hardware      the binary encoded as MIPS32 machine code and run on the Verilog CPU inside the SoC
                  [--gates: and on the gate-level netlist that Yosys synthesises from the same RTL]
 4. chip design   the CPU's own control unit: Yosys -> BLIF -> nexus-eda technology mapping (proved equivalent with
                  the SAT solver of the logic-design library), static timing, partitioning, placement, routing
 5. FPGA          [--fpga] the SoC with this program in its boot ROM -> ECP5 placement, routing, bitstream
 6. OS            the round-robin kernel on the SoC: its dispatch order (sent to the UART) equals the Gantt chart
                  that the scheduling library computes for the same jobs

Exit codes: 0 every stage that ran agreed, 1 a mismatch, 77 Icarus Verilog missing.
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import soc_flow  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "examples" / "flow" / "flow_demo.nx"
WANT_UART = "S=5913\\n"
WANT_EXIT = 5913 % 256

MODELS = [
    ["--mode", "functional"],
    ["--mode", "single-cycle"],
    ["--mode", "multi-cycle", "--control", "hardwired"],
    ["--mode", "multi-cycle", "--control", "microcode"],
    ["--mode", "pipeline", "--io-demo"],
    ["--mode", "pipeline", "--io-demo", "--cache", "assoc"],
    ["--mode", "advanced", "--scheduler", "inorder"],
    ["--mode", "advanced", "--scheduler", "scoreboard"],
    ["--mode", "advanced", "--scheduler", "tomasulo", "--issue-width", "4", "--predictor", "2bit"],
    ["--mode", "parallel", "--cores", "1"],
]

failures = 0


def sh(*args: str, cwd: Path | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(list(args), text=True, capture_output=True, check=False, cwd=cwd)


def check(ok: bool, stage: str, detail: str) -> None:
    global failures
    failures += 0 if ok else 1
    print(f"  {'ok  ' if ok else 'FAIL'} {stage:58s} {detail}")


def console_text(out: str) -> str:
    return "".join("\\n" if v == "10" else chr(int(v)) for v in re.findall(r"io\[console\]: value=(\d+)", out))


def main() -> int:
    bindir = Path(sys.argv[1])
    exe = ".exe" if (bindir / "nexusc.exe").exists() else ""
    nexusc, mips_sim = str(bindir / f"nexusc{exe}"), str(bindir / f"mips-sim{exe}")
    eda, nexus_os = str(bindir / f"nexus-eda{exe}"), str(bindir / f"nexus-os{exe}")
    if shutil.which("iverilog") is None or shutil.which("vvp") is None:
        print("SKIP: iverilog not found")
        return 77
    work = soc_flow.BUILD
    work.mkdir(parents=True, exist_ok=True)

    print("1. compiler: NexusLang -> MIPS")
    asm = {}
    for alloc in ("none", "linear-scan"):
        out = work / f"flow_{alloc}.s"
        r = sh(nexusc, "compile", str(SOURCE), "-S", "--regalloc", alloc, "-o", str(out))
        check(r.returncode == 0, f"nexusc compile --regalloc {alloc}", out.name)
        asm[alloc] = out
    r = sh(nexusc, "analysis", "ssa", str(SOURCE))
    check(r.returncode == 0 and "phi" in r.stdout, "SSA form of the same program", "phi nodes present")

    print("2. CPU models: the same binary everywhere")
    for alloc, path in asm.items():
        for model in MODELS:
            r = sh(mips_sim, "run", str(path), *model)
            out = r.stdout + r.stderr
            code = re.search(r"Program exited with code (-?\d+)", out)
            text = console_text(out)
            ok = code is not None and int(code.group(1)) == WANT_EXIT and text == WANT_UART
            name = " ".join(m for m in model[1:] if m not in ("--io-demo", "--control", "--scheduler"))
            check(ok, f"{name} [{alloc}]",
                  f"uart '{text}' exit {code.group(1) if code else '?'}")

    print("3. hardware: MIPS32 machine code on the Verilog SoC")
    hexfile = work / "flow.hex"
    entry, handler = soc_flow.encode(mips_sim, asm["linear-scan"], hexfile)
    vvp = work / "soc.vvp"
    r = sh("iverilog", "-g2012", "-o", str(vvp), str(soc_flow.HDL / "cpu_pipeline" / "nexus_mips_pipeline.v"),
           str(soc_flow.HDL / "soc" / "nexus_soc.v"), str(soc_flow.HDL / "soc" / "soc_tb.v"))
    if r.returncode != 0:
        print(r.stderr)
        return 1
    code, uart, _ = soc_flow.rtl(vvp, hexfile, entry, handler, 0)
    check(code == WANT_EXIT and uart == WANT_UART, "SoC RTL (Icarus Verilog)", f"uart '{uart}' exit {code}")
    if "--gates" in sys.argv:
        code, uart, _ = soc_flow.gates(hexfile, entry, handler, 0, imem_words=256)
        check(code == WANT_EXIT and uart == WANT_UART, "gate-level netlist (Yosys)", f"uart '{uart}' exit {code}")

    print("4. chip design: the CPU's control unit through nexus-eda")
    if shutil.which("yosys") is not None:
        blif = work / "control.blif"
        r = sh("yosys", "-q", "-p", f"read_verilog {soc_flow.HDL / 'control' / 'nexus_control_unit.v'}; "
               f"synth -flatten -top nexus_control_unit; abc -lut 4; opt_clean; write_blif {blif}")
        check(r.returncode == 0, "Yosys: control unit -> BLIF", blif.name)
        r = sh(eda, "flow", str(blif))
        out = r.stdout
        cells = re.search(r"technology mapping \(area\): (\d+) cells", out)
        routed = re.search(r"overflow (\d+)", out)
        check(r.returncode == 0 and "PROVED" in out, "mapping proved equivalent (SAT)",
              f"{cells.group(1) if cells else '?'} cells")
        check(routed is not None and routed.group(1) == "0", "placement and routing", "overflow 0" if routed else "?")
    else:
        print("  skip yosys not found")

    print("5. FPGA")
    if "--fpga" in sys.argv:
        if shutil.which("yosys") and shutil.which("nextpnr-ecp5"):
            ok = soc_flow.fpga(mips_sim, asm["linear-scan"])
            check(ok, "ECP5 placement, routing, bitstream", "build/soc/fpga_report.txt")
        else:
            print("  skip nextpnr-ecp5 not found")
    else:
        print("  skip (run with --fpga)")

    print("6. OS: the kernel on the SoC against the scheduling library")
    kernel = ROOT / "examples" / "os" / "rr_kernel.s"
    khex = work / "rr_kernel.hex"
    kentry, khandler = soc_flow.encode(mips_sim, kernel, khex)
    code, uart, irqs = soc_flow.rtl(vvp, khex, kentry, khandler, 0)
    r = sh(nexus_os, "schedule", str(ROOT / "examples" / "os" / "rr_kernel.jobs"), "--policy", "rr", "--quantum", "1")
    gantt = "".join(re.findall(r"\[\d+-\d+ (\w+)\]", r.stdout))
    check(code == len(gantt) and irqs == len(gantt) and uart == gantt, "kernel dispatch order = RR Gantt chart",
          f"SoC '{uart}', nexus-os '{gantt}'")

    print("PASS" if failures == 0 else f"FAIL ({failures})")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
