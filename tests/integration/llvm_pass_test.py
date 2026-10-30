#!/usr/bin/env python3
"""NexusPasses LLVM plugin: strength reduction must remove every power-of-two mul/udiv/urem without changing the
program's output, and the loop report must find the 100 x 10 nest (backedge-taken counts 100 and 10)."""
from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    require(result.returncode == 0, f"{' '.join(command)} failed:\n{result.stderr}")
    return result


POWER_OF_TWO_OP = re.compile(r"= (mul|udiv|urem)\b[^,]*, (2|4|8|16|32|64)\b|= mul\b[^%]*\b(2|4|8|16|32|64), %")


def main() -> int:
    if len(sys.argv) != 5:
        raise SystemExit("usage: llvm_pass_test.py <plugin> <clang> <opt> <repo-root>")
    plugin, clang, opt, repo = sys.argv[1], sys.argv[2], sys.argv[3], Path(sys.argv[4])
    source = repo / "tests" / "llvm" / "strength_loops.c"
    with tempfile.TemporaryDirectory() as work_dir:
        work = Path(work_dir)
        base = work / "base.ll"
        promoted = work / "promoted.ll"
        reduced = work / "reduced.ll"
        run([clang, "-O0", "-Xclang", "-disable-O0-optnone", "-S", "-emit-llvm", str(source), "-o", str(base)])
        run([opt, "-passes=mem2reg", str(base), "-S", "-o", str(promoted)])
        before = [line for line in promoted.read_text().splitlines() if POWER_OF_TWO_OP.search(line)]
        require(len(before) >= 5, "the input should contain power-of-two operations:\n" + "\n".join(before))

        reduce = run([opt, f"-load-pass-plugin={plugin}", "-passes=nexus-strength", str(promoted), "-S", "-o", str(reduced)])
        after_text = reduced.read_text()
        after = [line for line in after_text.splitlines() if POWER_OF_TWO_OP.search(line)]
        require(not after, "power-of-two operations left after nexus-strength:\n" + "\n".join(after))
        require(" shl " in after_text and " lshr " in after_text and " and " in after_text, "no shl/lshr/and emitted")
        require("operation(s) replaced" in reduce.stderr, reduce.stderr)
        run([opt, "-passes=verify", str(reduced), "-disable-output"])

        outputs = []
        for module in (promoted, reduced):
            binary = work / (module.stem + ".exe")
            run([clang, str(module), "-o", str(binary)])
            outputs.append(run([str(binary)]).stdout)
        require(outputs[0] == outputs[1] and outputs[0].strip().isdigit(), f"outputs differ: {outputs}")

        report = run([opt, f"-load-pass-plugin={plugin}", "-passes=nexus-loops", str(promoted), "-disable-output"])
        lines = [line for line in report.stderr.splitlines() if line.startswith("nexus-loops: main:")]
        print(report.stderr)
        require(len(lines) == 2, report.stderr)
        # the loops test their condition at the top, so the header runs 101 (11) times: trip count 101 (11)
        # and backedge-taken count 100 (10), i.e. 100 (10) iterations of the body
        require("depth 1" in lines[0] and "backedge-taken count 100" in lines[0], report.stderr)
        require("depth 2" in lines[1] and "backedge-taken count 10" in lines[1], report.stderr)
    print(f"llvm pass: {len(before)} power-of-two operations reduced, output {outputs[0].strip()} unchanged; loops found")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
