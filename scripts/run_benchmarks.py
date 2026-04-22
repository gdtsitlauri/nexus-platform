#!/usr/bin/env python3
from __future__ import annotations

import csv
import subprocess
import sys
import tempfile
from pathlib import Path


BENCHMARKS = [
    (
        "factorial",
        "examples/source_lang/factorial.nx",
        [
            ("functional", ["--mode", "functional", "--stats"]),
            ("single-cycle", ["--mode", "single-cycle", "--stats"]),
            ("multi-cycle-hardwired", ["--mode", "multi-cycle", "--control", "hardwired", "--stats"]),
            ("pipeline-off", ["--mode", "pipeline", "--stats"]),
        ],
    ),
    (
        "arrays_and_loops",
        "examples/source_lang/arrays_and_loops.nx",
        [
            ("functional", ["--mode", "functional", "--stats"]),
            ("single-cycle", ["--mode", "single-cycle", "--stats"]),
            ("multi-cycle-hardwired", ["--mode", "multi-cycle", "--control", "hardwired", "--stats"]),
            ("pipeline-off", ["--mode", "pipeline", "--stats"]),
            ("pipeline-direct", ["--mode", "pipeline", "--cache", "direct", "--stats"]),
            ("pipeline-assoc", ["--mode", "pipeline", "--cache", "assoc", "--ways", "2", "--stats"]),
        ],
    ),
]


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
        normalized = key.lower().replace(" ", "_").replace("-", "_")
        data[normalized] = value.strip()
    return data


def main() -> int:
    if len(sys.argv) != 5:
        raise SystemExit("usage: run_benchmarks.py <nexusc> <mips-sim> <repo> <out.csv>")

    nexusc = Path(sys.argv[1])
    mips_sim = Path(sys.argv[2])
    repo = Path(sys.argv[3])
    out_csv = Path(sys.argv[4])

    rows: list[dict[str, str]] = []
    with tempfile.TemporaryDirectory() as temp_dir:
        temp_path = Path(temp_dir)
        for benchmark_name, relative_source, run_configs in BENCHMARKS:
            source = repo / relative_source
            asm_path = temp_path / f"{benchmark_name}.s"
            compile_result = run([str(nexusc), "compile", str(source), "-S", "-o", str(asm_path)])
            if compile_result.returncode != 0:
                raise SystemExit(f"failed to compile benchmark '{benchmark_name}': {compile_result.stderr}")

            for run_name, extra_args in run_configs:
                result = run([str(mips_sim), "run", str(asm_path), *extra_args])
                if result.returncode != 0:
                    raise SystemExit(
                        f"benchmark run '{run_name}' failed for '{benchmark_name}': {result.stderr}"
                    )
                summary = parse_summary(result.stdout)
                row = {
                    "benchmark": benchmark_name,
                    "run": run_name,
                    "mode": summary.get("mode", ""),
                    "control": summary.get("control", ""),
                    "predictor": summary.get("predictor", ""),
                    "cache": summary.get("cache", ""),
                    "exit_code": summary.get("exit_code", ""),
                    "instructions": summary.get("instructions", ""),
                    "cycles": summary.get("cycles", ""),
                    "cpi": summary.get("cpi", ""),
                    "ipc": summary.get("ipc", ""),
                    "stalls": summary.get("stalls", ""),
                    "flushes": summary.get("flushes", ""),
                    "cache_hits": summary.get("cache_hits", ""),
                    "cache_misses": summary.get("cache_misses", ""),
                    "cache_miss_rate": summary.get("cache_miss_rate", ""),
                }
                rows.append(row)

    out_csv.parent.mkdir(parents=True, exist_ok=True)
    with out_csv.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "benchmark",
                "run",
                "mode",
                "control",
                "predictor",
                "cache",
                "exit_code",
                "instructions",
                "cycles",
                "cpi",
                "ipc",
                "stalls",
                "flushes",
                "cache_hits",
                "cache_misses",
                "cache_miss_rate",
            ],
        )
        writer.writeheader()
        writer.writerows(rows)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
