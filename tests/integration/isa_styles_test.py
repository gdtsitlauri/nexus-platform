#!/usr/bin/env python3
"""Every NexusLang test program must give the same result on the stack (JVM-like), accumulator,
register-memory (IA-32-like) and load/store (MIPS) machines, and the classic ISA trade-offs must hold."""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROW = re.compile(r"^(stack|accumulator|register-memory|load-store \(MIPS\)|load-store \(MIPS\+regalloc\))\s+"
                 r"(-?\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", re.M)


def main() -> int:
    nexusc, repo = Path(sys.argv[1]), Path(sys.argv[2])
    programs = sorted((repo / "tests" / "programs").glob("*.nx")) + [repo / "examples" / "source_lang" / "factorial.nx"]
    failures = []
    for program in programs:
        result = subprocess.run([str(nexusc), "isa", str(program)], text=True, capture_output=True, check=False)
        rows = {m.group(1): [int(v) for v in m.groups()[1:]] for m in ROW.finditer(result.stdout)}
        if result.returncode != 0 or len(rows) != 5:
            failures.append(f"{program.name}: isa comparison failed\n{result.stdout}{result.stderr}")
            continue
        exits = {name: values[0] for name, values in rows.items()}
        if len(set(exits.values())) != 1:
            failures.append(f"{program.name}: results differ {exits}")
        bytes_ = {name: values[2] for name, values in rows.items()}
        if not bytes_["stack"] < bytes_["load-store (MIPS)"]:
            failures.append(f"{program.name}: stack code should be denser than fixed 32-bit MIPS ({bytes_})")
        traffic = {name: values[4] + values[5] for name, values in rows.items()}
        if not traffic["load-store (MIPS+regalloc)"] < min(traffic[n] for n in ("stack", "accumulator", "register-memory")):
            failures.append(f"{program.name}: register allocation should minimise data-memory traffic ({traffic})")
        print(f"{program.name}: OK {exits['stack']}")
    for failure in failures:
        print("FAIL:", failure)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
