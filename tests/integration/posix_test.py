#!/usr/bin/env python3
"""EY517 Linux lab programs (fork/pipe/wait, exec/signals, POSIX threads). Built and run only on POSIX systems;
exit code 77 (skip) elsewhere. Usage: posix_test.py <build dir>"""
import os
import subprocess
import sys
from pathlib import Path

if os.name != "posix":
    print("SKIP: POSIX programs need Linux or macOS")
    sys.exit(77)
bindir = Path(sys.argv[1]) / "bin"
expect = {"nexus_posix_fork_pipe": "total 5050", "nexus_posix_exec_signal": "signal handled 1 time(s)",
          "nexus_posix_threads": "counter 400000"}
failures = 0
for exe, want in expect.items():
    path = bindir / exe
    if not path.exists():
        print("SKIP: not built:", exe)
        sys.exit(77)
    r = subprocess.run([str(path)], text=True, capture_output=True, check=False)
    ok = r.returncode == 0 and want in r.stdout
    failures += 0 if ok else 1
    print(f"{exe}: {'ok' if ok else 'FAIL'} {r.stdout.strip()}")
sys.exit(1 if failures else 0)
