#!/usr/bin/env python3
from __future__ import annotations

import csv
import sys
from pathlib import Path


def find_row(rows: list[dict[str, str]], benchmark: str, configuration: str) -> dict[str, str]:
    for row in rows:
        if row["benchmark"] == benchmark and row["configuration"] == configuration:
            return row
    raise SystemExit(f"missing row for {benchmark}/{configuration}")


def cycles(row: dict[str, str]) -> int:
    return int(row["cycles"])


def speedup(base: dict[str, str], improved: dict[str, str]) -> float:
    return cycles(base) / cycles(improved)


def effective_fraction(measured_speedup: float, width: float) -> float:
    if measured_speedup <= 1.0 or width <= 1.0:
        return 0.0
    return max(0.0, min(1.0, (1.0 - (1.0 / measured_speedup)) / (1.0 - (1.0 / width))))


def amdahl_speedup(parallel_fraction: float, width: float) -> float:
    return 1.0 / ((1.0 - parallel_fraction) + (parallel_fraction / width))


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: generate_amdahl_report.py <raw.csv> <summary.md> <report.md>")

    raw_csv = Path(sys.argv[1])
    summary_md = Path(sys.argv[2])
    report_md = Path(sys.argv[3])

    with raw_csv.open(newline="") as handle:
        rows = list(csv.DictReader(handle))

    arrays_pipeline = find_row(rows, "compiled_arrays", "pipeline-baseline")
    arrays_width1 = find_row(rows, "compiled_arrays", "advanced-width1")
    arrays_width2 = find_row(rows, "compiled_arrays", "advanced-width2")
    predictor_static = find_row(rows, "predictor_loop", "advanced-static")
    predictor_two_bit = find_row(rows, "predictor_loop", "advanced-2bit")
    vliw_inorder = find_row(rows, "vliw_bundle", "advanced-inorder-width2")
    vliw_lite = find_row(rows, "vliw_bundle", "advanced-vliw-lite")

    arrays_speedup = speedup(arrays_width1, arrays_width2)
    arrays_fraction = effective_fraction(arrays_speedup, 2.0)
    projected_width4 = amdahl_speedup(arrays_fraction, 4.0)

    predictor_speedup = speedup(predictor_static, predictor_two_bit)
    vliw_speedup = speedup(vliw_inorder, vliw_lite)

    summary_lines = [
        "# Phase 8 Advanced Summary",
        "",
        "| Benchmark | Baseline | Improved | Speedup | Notes |",
        "| --- | --- | --- | ---: | --- |",
        f"| compiled_arrays | advanced-width1 ({arrays_width1['cycles']}) | advanced-width2 ({arrays_width2['cycles']}) | {arrays_speedup:.2f} | width-2 issue over compiled NexusLang loop |",
        f"| predictor_loop | advanced-static ({predictor_static['cycles']}) | advanced-2bit ({predictor_two_bit['cycles']}) | {predictor_speedup:.2f} | 2-bit predictor reduces loop mispredictions |",
        f"| vliw_bundle | advanced-inorder-width2 ({vliw_inorder['cycles']}) | advanced-vliw-lite ({vliw_lite['cycles']}) | {vliw_speedup:.2f} | static bundling reorders independent operations |",
    ]

    report_lines = [
        "# Phase 8 Amdahl Evaluation",
        "",
        "## Scope",
        "",
        "- tiny workloads only: one compiled NexusLang loop nest and two hand-written MIPS microbenchmarks",
        "- advanced features covered here: 2-bit branch prediction, width-2 issue, and VLIW-lite bundling",
        "- correctness is still validated separately against the functional model; this report focuses on timing-style sandbox comparisons",
        "",
        "## Predictor Comparison",
        "",
        "| Configuration | Cycles | Branch predictions | Branch mispredictions |",
        "| --- | ---: | ---: | ---: |",
        f"| advanced-static | {predictor_static['cycles']} | {predictor_static['branch_predictions']} | {predictor_static['branch_mispredictions']} |",
        f"| advanced-2bit | {predictor_two_bit['cycles']} | {predictor_two_bit['branch_predictions']} | {predictor_two_bit['branch_mispredictions']} |",
        "",
        "## Scheduling Comparison",
        "",
        "| Configuration | Cycles | IPC | Slot utilization |",
        "| --- | ---: | ---: | ---: |",
        f"| advanced-inorder-width2 | {vliw_inorder['cycles']} | {vliw_inorder['ipc']} | {vliw_inorder['slot_utilization']} |",
        f"| advanced-vliw-lite | {vliw_lite['cycles']} | {vliw_lite['ipc']} | {vliw_lite['slot_utilization']} |",
        "",
        "## Pipeline Baseline Comparison",
        "",
        "| Configuration | Cycles | CPI |",
        "| --- | ---: | ---: |",
        f"| pipeline-baseline | {arrays_pipeline['cycles']} | {arrays_pipeline['cpi']} |",
        f"| advanced-width1 | {arrays_width1['cycles']} | {arrays_width1['cpi']} |",
        f"| advanced-width2 | {arrays_width2['cycles']} | {arrays_width2['cpi']} |",
        "",
        "## Amdahl Projection",
        "",
        f"- measured width-1 -> width-2 speedup on `compiled_arrays`: **{arrays_speedup:.2f}x**",
        f"- effective accelerated fraction estimated with Amdahl's Law (`k=2`): **{arrays_fraction:.2%}**",
        f"- projected speedup if the same accelerated fraction scaled to width 4: **{projected_width4:.2f}x**",
        "",
        "## Interpretation",
        "",
        "- the width-2 model improves throughput on independent instruction groups, but serial dependence chains still cap speedup",
        "- the 2-bit predictor helps the loop benchmark by learning repeated taken outcomes after the initial miss",
        "- VLIW-lite gains come from static reordering inside a basic block; no global scheduling or register renaming is modeled",
        "",
        "## Limitations",
        "",
        "- this is an educational sandbox, not a full out-of-order or superscalar core",
        "- no reorder buffer, scoreboard, Tomasulo, cache hierarchy extension, or multicore effects are modeled here",
        "- Amdahl estimates are derived from tiny workloads and should be read as bounded teaching examples, not production measurements",
        "",
    ]

    summary_md.parent.mkdir(parents=True, exist_ok=True)
    summary_md.write_text("\n".join(summary_lines) + "\n")
    report_md.parent.mkdir(parents=True, exist_ok=True)
    report_md.write_text("\n".join(report_lines) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
