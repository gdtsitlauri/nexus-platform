#!/usr/bin/env python3
from __future__ import annotations

import csv
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: generate_benchmark_report.py <raw.csv> <summary.md> <report.md>")

    raw_csv = Path(sys.argv[1])
    summary_md = Path(sys.argv[2])
    report_md = Path(sys.argv[3])

    with raw_csv.open(newline="") as handle:
        rows = list(csv.DictReader(handle))

    unique_benchmarks = sorted({row["benchmark"] for row in rows})
    unique_runs = sorted({row["run"] for row in rows})
    summary = summary_md.read_text().rstrip()

    report = "\n".join(
        [
            "# Phase 7 Benchmark Report",
            "",
            "## Scope",
            "",
            f"- benchmarks: {', '.join(unique_benchmarks)}",
            f"- run configurations: {', '.join(unique_runs)}",
            "- metrics: instructions, cycles, CPI, IPC, stalls, flushes, cache hits, cache misses",
            "",
            "## Aggregated Summary",
            "",
            summary,
            "",
            "## Notes",
            "",
            "- functional mode is the correctness reference and does not model cache behavior",
            "- single-cycle and multi-cycle runs remain cache-free in Phase 7",
            "- pipeline-off, pipeline-direct, and pipeline-assoc expose the new cache/performance layer",
            "",
        ]
    )
    report_md.parent.mkdir(parents=True, exist_ok=True)
    report_md.write_text(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
