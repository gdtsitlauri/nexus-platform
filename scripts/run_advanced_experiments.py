#!/usr/bin/env python3
from __future__ import annotations

import csv
import subprocess
import sys
import tempfile
from pathlib import Path


def run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, capture_output=True, check=False)


def parse_summary(output: str) -> dict[str, str]:
    data: dict[str, str] = {}
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        if line.startswith("Program exited with code "):
            data["exit_code"] = line.removeprefix("Program exited with code ").strip()
            continue
        if ": " not in line:
            continue
        key, value = line.split(": ", 1)
        data[key.lower().replace(" ", "_").replace("-", "_")] = value.strip()
    return data


def add_row(rows: list[dict[str, str]], benchmark: str, configuration: str, summary: dict[str, str]) -> None:
    rows.append(
        {
            "benchmark": benchmark,
            "configuration": configuration,
            "mode": summary.get("mode", ""),
            "scheduler": summary.get("scheduler", ""),
            "predictor": summary.get("predictor", ""),
            "issue_width": summary.get("issue_width", ""),
            "exit_code": summary.get("exit_code", ""),
            "instructions": summary.get("instructions", ""),
            "cycles": summary.get("cycles", ""),
            "cpi": summary.get("cpi", ""),
            "ipc": summary.get("ipc", ""),
            "branch_predictions": summary.get("branch_predictions", ""),
            "branch_mispredictions": summary.get("branch_mispredictions", ""),
            "speculative_flush_cycles": summary.get("speculative_flush_cycles", ""),
            "slot_utilization": summary.get("slot_utilization", ""),
        }
    )


def main() -> int:
    if len(sys.argv) != 5:
        raise SystemExit("usage: run_advanced_experiments.py <nexusc> <mips-sim> <repo> <out.csv>")

    nexusc = Path(sys.argv[1])
    mips_sim = Path(sys.argv[2])
    repo = Path(sys.argv[3])
    out_csv = Path(sys.argv[4])

    arrays_source = repo / "examples" / "source_lang" / "arrays_and_loops.nx"
    predictor_program = repo / "tests" / "golden" / "advanced_branch_demo.s"
    vliw_program = repo / "tests" / "golden" / "advanced_vliw_demo.s"

    rows: list[dict[str, str]] = []
    with tempfile.TemporaryDirectory() as temp_dir:
        temp = Path(temp_dir)
        arrays_asm = temp / "arrays_and_loops.s"
        compile_result = run([str(nexusc), "compile", str(arrays_source), "-S", "-o", str(arrays_asm)])
        if compile_result.returncode != 0:
            raise SystemExit(f"failed to compile arrays_and_loops: {compile_result.stderr}")

        experiment_matrix = [
            (
                "compiled_arrays",
                [str(arrays_asm)],
                [
                    ("pipeline-baseline", ["--mode", "pipeline", "--predictor", "static-not-taken", "--stats"]),
                    (
                        "advanced-width1",
                        ["--mode", "advanced", "--predictor", "static-not-taken", "--issue-width", "1", "--stats"],
                    ),
                    (
                        "advanced-width2",
                        ["--mode", "advanced", "--predictor", "static-not-taken", "--issue-width", "2", "--stats"],
                    ),
                ],
            ),
            (
                "predictor_loop",
                [str(predictor_program)],
                [
                    (
                        "advanced-static",
                        ["--mode", "advanced", "--predictor", "static-not-taken", "--issue-width", "1", "--stats"],
                    ),
                    ("advanced-2bit", ["--mode", "advanced", "--predictor", "2bit", "--issue-width", "1", "--stats"]),
                ],
            ),
            (
                "vliw_bundle",
                [str(vliw_program)],
                [
                    (
                        "advanced-inorder-width2",
                        ["--mode", "advanced", "--predictor", "static-not-taken", "--issue-width", "2", "--stats"],
                    ),
                    (
                        "advanced-vliw-lite",
                        [
                            "--mode",
                            "advanced",
                            "--predictor",
                            "static-not-taken",
                            "--scheduler",
                            "vliw-lite",
                            "--issue-width",
                            "2",
                            "--stats",
                        ],
                    ),
                ],
            ),
        ]

        for benchmark, program_args, configs in experiment_matrix:
            for configuration, extra_args in configs:
                result = run([str(mips_sim), "run", *program_args, *extra_args])
                if result.returncode != 0:
                    raise SystemExit(
                        f"advanced experiment '{configuration}' failed for '{benchmark}': {result.stderr}"
                    )
                add_row(rows, benchmark, configuration, parse_summary(result.stdout))

    out_csv.parent.mkdir(parents=True, exist_ok=True)
    with out_csv.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "benchmark",
                "configuration",
                "mode",
                "scheduler",
                "predictor",
                "issue_width",
                "exit_code",
                "instructions",
                "cycles",
                "cpi",
                "ipc",
                "branch_predictions",
                "branch_mispredictions",
                "speculative_flush_cycles",
                "slot_utilization",
            ],
        )
        writer.writeheader()
        writer.writerows(rows)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
