#!/usr/bin/env python3
"""Portable Icarus Verilog runner for the Nexus HDL testbenches (Linux, macOS and Windows).

Exit codes: 0 = all selected testbenches passed, 1 = failure, 77 = Icarus Verilog not installed (skip).
"""
from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HDL = ROOT / "src" / "hdl"

SUITES: dict[str, list[str]] = {
    "adder": ["alu/nexus_adder.v", "alu/adder_tb.v"],
    "alu": ["alu/nexus_adder.v", "alu/nexus_alu.v", "alu/alu_tb.v"],
    "multiplier": ["alu/nexus_iterative_multiplier.v", "alu/iterative_multiplier_tb.v"],
    "divider": ["alu/nexus_iterative_divider.v", "alu/iterative_divider_tb.v"],
    "register-file": ["register_file/nexus_register_file.v", "register_file/register_file_tb.v"],
    "control": ["control/nexus_control_unit.v", "control/control_unit_tb.v"],
    "pipeline-reg": ["pipeline_regs/nexus_pipeline_reg.v", "pipeline_regs/pipeline_reg_tb.v"],
    "cpu-slice": [
        "alu/nexus_adder.v",
        "alu/nexus_alu.v",
        "control/nexus_control_unit.v",
        "register_file/nexus_register_file.v",
        "pipeline_regs/nexus_pipeline_reg.v",
        "cpu_slice/nexus_cpu_slice.v",
        "cpu_slice/cpu_slice_tb.v",
    ],
}


def run_suite(name: str, build_dir: Path) -> bool:
    output = build_dir / f"{name}.vvp"
    sources = [str(HDL / rel) for rel in SUITES[name]]
    compile_result = subprocess.run(
        ["iverilog", "-g2012", "-o", str(output), *sources], text=True, capture_output=True, check=False
    )
    if compile_result.returncode != 0:
        print(f"== {name}: COMPILE FAILED\n{compile_result.stdout}{compile_result.stderr}")
        return False
    sim = subprocess.run(["vvp", str(output)], text=True, capture_output=True, check=False)
    text = sim.stdout + sim.stderr
    passed = sim.returncode == 0 and "PASS" in text and "FAIL" not in text
    print(f"== {name}: {'PASS' if passed else 'FAIL'}")
    if not passed:
        print(text)
    return passed


def main() -> int:
    target = sys.argv[1] if len(sys.argv) > 1 else "all"
    if target not in SUITES and target != "all":
        print(f"usage: test_hdl.py {{all|{'|'.join(SUITES)}}}", file=sys.stderr)
        return 1
    if shutil.which("iverilog") is None or shutil.which("vvp") is None:
        print("SKIP: iverilog/vvp not found in PATH")
        return 77
    build_dir = ROOT / "build" / "hdl"
    build_dir.mkdir(parents=True, exist_ok=True)
    names = list(SUITES) if target == "all" else [target]
    results = [run_suite(name, build_dir) for name in names]
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
