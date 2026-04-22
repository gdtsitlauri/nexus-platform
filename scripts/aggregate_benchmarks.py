#!/usr/bin/env python3
from __future__ import annotations

import csv
import sys
from collections import defaultdict
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: aggregate_benchmarks.py <raw.csv> <summary.csv> <summary.md>")

    raw_csv = Path(sys.argv[1])
    summary_csv = Path(sys.argv[2])
    summary_md = Path(sys.argv[3])

    with raw_csv.open(newline="") as handle:
        rows = list(csv.DictReader(handle))

    grouped: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        grouped[row["benchmark"]].append(row)

    summary_rows: list[dict[str, str]] = []
    markdown_lines = [
        "# Phase 7 Benchmark Summary",
        "",
        "| Benchmark | Fastest run | Cycles | Best cache-backed run | Cache cycles |",
        "| --- | --- | ---: | --- | ---: |",
    ]

    for benchmark, benchmark_rows in sorted(grouped.items()):
        fastest = min(benchmark_rows, key=lambda row: int(row["cycles"]))
        cache_rows = [row for row in benchmark_rows if row["run"].startswith("pipeline-") and row["run"] != "pipeline-off"]
        best_cache = min(cache_rows, key=lambda row: int(row["cycles"])) if cache_rows else fastest
        summary_row = {
            "benchmark": benchmark,
            "fastest_run": fastest["run"],
            "fastest_cycles": fastest["cycles"],
            "best_cache_run": best_cache["run"],
            "best_cache_cycles": best_cache["cycles"],
        }
        summary_rows.append(summary_row)
        markdown_lines.append(
            f"| {benchmark} | {fastest['run']} | {fastest['cycles']} | {best_cache['run']} | {best_cache['cycles']} |"
        )

    summary_csv.parent.mkdir(parents=True, exist_ok=True)
    with summary_csv.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=["benchmark", "fastest_run", "fastest_cycles", "best_cache_run", "best_cache_cycles"],
        )
        writer.writeheader()
        writer.writerows(summary_rows)

    summary_md.parent.mkdir(parents=True, exist_ok=True)
    summary_md.write_text("\n".join(markdown_lines) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
