#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


def run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, capture_output=True, check=False)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> int:
    nexusc = Path(sys.argv[1])
    mips_sim = Path(sys.argv[2])
    repo = Path(sys.argv[3])

    factorial = repo / "examples" / "source_lang" / "factorial.nx"
    factorial_expected = (repo / "tests" / "golden" / "factorial_stdout.txt").read_text()
    trace_program = repo / "tests" / "golden" / "trace_demo.s"
    pipeline_branch_program = repo / "tests" / "golden" / "pipeline_branch_demo.s"
    functional_trace_expected = (repo / "tests" / "golden" / "trace_demo.stdout.txt").read_text()
    single_cycle_trace_expected = (repo / "tests" / "golden" / "single_cycle_trace.stdout.txt").read_text()
    multi_cycle_trace_expected = (repo / "tests" / "golden" / "multi_cycle_trace.stdout.txt").read_text()
    pipeline_trace_expected = (repo / "tests" / "golden" / "pipeline_trace.stdout.txt").read_text()
    pipeline_timeline_expected = (repo / "tests" / "golden" / "pipeline_timeline.stdout.txt").read_text()

    with tempfile.TemporaryDirectory() as temp_dir:
        asm_path = Path(temp_dir) / "factorial.s"
        compile_result = run([str(nexusc), "compile", str(factorial), "-S", "-o", str(asm_path)])
        require(compile_result.returncode == 0, "golden compile step failed")

        factorial_run = run([str(mips_sim), "run", str(asm_path), "--mode", "functional"])
        require(factorial_run.returncode == 0, "golden factorial run failed")
        require(
            factorial_run.stdout == factorial_expected,
            "golden factorial output mismatch",
        )

    functional_trace = run([str(mips_sim), "run", str(trace_program), "--mode", "functional", "--trace"])
    require(functional_trace.returncode == 0, "golden functional trace run failed")
    require(functional_trace.stdout == functional_trace_expected, "golden functional trace output mismatch")

    single_cycle_trace = run([str(mips_sim), "run", str(trace_program), "--mode", "single-cycle", "--trace"])
    require(single_cycle_trace.returncode == 0, "golden single-cycle trace run failed")
    require(single_cycle_trace.stdout == single_cycle_trace_expected, "golden single-cycle trace output mismatch")

    multi_cycle_trace = run(
        [str(mips_sim), "run", str(trace_program), "--mode", "multi-cycle", "--control", "microcode", "--trace"]
    )
    require(multi_cycle_trace.returncode == 0, "golden multi-cycle trace run failed")
    require(multi_cycle_trace.stdout == multi_cycle_trace_expected, "golden multi-cycle trace output mismatch")

    pipeline_trace = run([str(mips_sim), "run", str(trace_program), "--mode", "pipeline", "--trace"])
    require(pipeline_trace.returncode == 0, "golden pipeline trace run failed")
    require(pipeline_trace.stdout == pipeline_trace_expected, "golden pipeline trace output mismatch")

    pipeline_timeline = run([str(mips_sim), "run", str(pipeline_branch_program), "--mode", "pipeline", "--timeline"])
    require(pipeline_timeline.returncode == 0, "golden pipeline timeline run failed")
    require(pipeline_timeline.stdout == pipeline_timeline_expected, "golden pipeline timeline output mismatch")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
