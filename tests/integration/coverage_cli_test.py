#!/usr/bin/env python3
"""Smoke test for every command added to close the course-coverage gaps (one check per topic)."""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    nexusc, mips_sim, repo = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    examples = repo / "examples"
    programs = repo / "tests" / "programs"
    failures: list[str] = []

    def check(name: str, command: list[str], *needles: str) -> None:
        result = subprocess.run(command, text=True, capture_output=True, check=False)
        output = result.stdout + result.stderr
        missing = [needle for needle in needles if needle not in output]
        if result.returncode != 0 or missing:
            failures.append(f"{name}: rc={result.returncode} missing={missing}\n{output[-800:]}")

    with tempfile.TemporaryDirectory() as temp_dir:
        asm = Path(temp_dir) / "factorial.s"
        subprocess.run([str(nexusc), "compile", str(examples / "source_lang" / "factorial.nx"), "-S", "-o", str(asm)],
                       check=True, capture_output=True)
        check("quadruples (NEY613)", [str(nexusc), "quads", str(examples / "source_lang" / "factorial.nx")],
              "func factorial quadruples:", "param", "call")
        check("register allocation (NEY613)", [str(nexusc), "analysis", "regalloc", str(programs / "register_pressure.nx")],
              "linear-scan", "spilled")
        check("SSA (NEY709)", [str(nexusc), "analysis", "ssa", str(programs / "constant_branches.nx")], "phi")
        check("SCCP lattice (NEY709)", [str(nexusc), "analysis", "sccp", str(programs / "constant_branches.nx")],
              "1 unreachable block")
        check("regex automata (NEY613)", [str(nexusc), "regex", "(a|b)*abb", "--match", "aabb"],
              "minimal DFA states: 4", "accept")
        check("LL(1) (NEY613)", [str(nexusc), "grammar", str(examples / "grammars" / "expr_ll1.g"), "ll1", "2 + 3 * 4"],
              "synthesized value = 14")
        check("LALR(1) (NEY613)", [str(nexusc), "grammar", str(examples / "grammars" / "lvalue.g"), "lalr"],
              "the grammar is LALR(1)")
        check("Earley (NEY709)", [str(nexusc), "grammar", str(examples / "grammars" / "ambiguous.g"), "earley", "1 + 2 * 3"],
              "parse trees: 2")
        check("type inference (NEY709)", [str(nexusc), "infer", "fun f -> fun x -> f (f x)"], "('a -> 'a) -> 'a -> 'a")
        check("polyhedral (NEY709)", [str(nexusc), "poly", str(examples / "loops" / "wavefront.loop"), "skew", "2", "1", "1",
                                      "interchange", "1", "2"], "identical to the original nest")
        check("ISA styles (NEY221)", [str(nexusc), "isa", str(examples / "source_lang" / "factorial.nx")],
              "All ISA styles agree")
        check("soft float (NEY221)", [str(mips_sim), "arith", "float", "0.1", "add", "0.2"], "bit-exact match")
        check("representations (NEY221)", [str(mips_sim), "arith", "repr", "-5", "8"], "11111011")
        check("Booth (NEY221)", [str(mips_sim), "arith", "booth", "-3", "5", "4"], "product = -15")
        check("encoder (EY321)", [str(mips_sim), "encode", str(asm)], "entry")
        check("Tomasulo + ROB (NEY606)", [str(mips_sim), "run", str(asm), "--mode", "advanced", "--scheduler", "tomasulo",
                                          "--issue-width", "2", "--predictor", "2bit"], "Program exited with code 120",
              "ROB entries")
        check("SMT (NEY704)", [str(mips_sim), "run", str(asm), "--mode", "advanced", "--scheduler", "tomasulo",
                               "--smt", str(asm)], "Thread 1: instructions=")
        check("deeper pipeline (NEY606)", [str(mips_sim), "run", str(asm), "--mode", "advanced", "--pipeline-depth", "12"],
              "Program exited with code 120")
        check("SIMT GPU (NEY704)", [str(mips_sim), "run", str(examples / "gpu" / "strided.s"), "--mode", "simt",
                                    "--threads", "64"], "Transactions per request: 32")
        check("mesh NoC (NEY704)", [str(mips_sim), "run", str(examples / "parallel" / "spmd_sum.s"), "--mode", "parallel",
                                    "--cores", "16", "--interconnect", "mesh"], "Program exited with code 8128")
    for failure in failures:
        print("FAIL:", failure)
    print(f"coverage CLI: {20 - len(failures)} / 20 topic commands OK")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
