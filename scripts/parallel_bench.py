#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import re
import subprocess
import sys
from pathlib import Path


def run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, capture_output=True, check=False)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def parse_kv_lines(text: str) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or "benchmark=" not in line:
            continue
        row: dict[str, str] = {}
        for token in line.split():
            if "=" not in token:
                continue
            key, value = token.split("=", 1)
            row[key] = value
        if row:
            rows.append(row)
    return rows


def parse_parallel_summary(text: str) -> dict[str, str]:
    row = {"suite": "parallel", "case": "coherence-demo", "status": "ok"}
    for label in ("Program exited with code", "Cycles", "Instructions", "Coherence", "Consistency", "Interconnect"):
        match = re.search(rf"^{re.escape(label)}:?\s+(.+)$", text, re.MULTILINE)
        if match:
            key = label.lower().replace(" ", "_").replace(":", "")
            row[key] = match.group(1).strip()
    return row


def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    fieldnames = [
        "suite",
        "case",
        "kernel",
        "status",
        "reason",
        "checksum",
        "cpu_checksum",
        "gpu_checksum",
        "threads",
        "ranks",
        "size",
        "program_exited_with_code",
        "cycles",
        "instructions",
        "coherence",
        "consistency",
        "interconnect",
    ]
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            normalized = {key: row.get(key, "") for key in fieldnames}
            writer.writerow(normalized)


def write_markdown(path: Path, rows: list[dict[str, str]]) -> None:
    lines = [
        "# Phase 11 Parallel Benchmark Summary",
        "",
        "| Suite | Case | Kernel | Status | Checksum | Cycles | Reason |",
        "| --- | --- | --- | --- | ---: | ---: | --- |",
    ]
    for row in rows:
        lines.append(
            f"| {row.get('suite', '')} | {row.get('case', '')} | {row.get('kernel', '')} | "
            f"{row.get('status', '')} | {row.get('checksum', '')} | {row.get('cycles', '')} | "
            f"{row.get('reason', '')} |"
        )
    path.write_text("\n".join(lines) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run tiny Phase 11 parallel-system and optional GPU benchmarks.")
    parser.add_argument("--build-dir", default="build", help="Build directory containing the bin/ tree.")
    parser.add_argument("--repo-root", default=".", help="Repository root path.")
    parser.add_argument("--csv", type=Path, help="Optional CSV output path.")
    parser.add_argument("--markdown", type=Path, help="Optional markdown output path.")
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--ranks", type=int, default=2)
    parser.add_argument("--cores", type=int, default=2)
    parser.add_argument("--openmp", action="store_true")
    parser.add_argument("--mpi", action="store_true")
    parser.add_argument("--simd", action="store_true")
    parser.add_argument("--parallel", action="store_true")
    parser.add_argument("--gpu", action="store_true")
    parser.add_argument("--all", action="store_true")
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    build_dir = Path(args.build_dir).resolve()
    bin_dir = build_dir / "bin"

    requested = {
        "openmp": args.openmp,
        "mpi": args.mpi,
        "simd": args.simd,
        "parallel": args.parallel,
        "gpu": args.gpu,
    }
    if args.all or not any(requested.values()):
        for key in requested:
            requested[key] = True

    rows: list[dict[str, str]] = []

    if requested["openmp"]:
        result = run(
            [
                str(bin_dir / "openmp-bench"),
                "--kernel",
                "all",
                "--size",
                "16",
                "--threads",
                str(args.threads),
            ]
        )
        require(result.returncode == 0, f"openmp benchmark failed: {result.stderr}")
        for row in parse_kv_lines(result.stdout):
            row["suite"] = "openmp"
            row["case"] = row.get("kernel", "")
            rows.append(row)

    if requested["mpi"]:
        result = run(
            [
                "mpiexec",
                "--oversubscribe",
                "-n",
                str(args.ranks),
                str(bin_dir / "mpi-bench"),
                "--kernel",
                "all",
                "--size",
                "8",
            ]
        )
        require(result.returncode == 0, f"mpi benchmark failed: {result.stderr}")
        for row in parse_kv_lines(result.stdout):
            row["suite"] = "mpi"
            row["case"] = row.get("kernel", "")
            rows.append(row)

    if requested["simd"]:
        result = run([str(bin_dir / "simd-bench"), "--kernel", "all", "--size", "32"])
        require(result.returncode == 0, f"simd benchmark failed: {result.stderr}")
        for row in parse_kv_lines(result.stdout):
            row["suite"] = "simd"
            row["case"] = row.get("kernel", "")
            rows.append(row)

    if requested["parallel"]:
        result = run(
            [
                str(bin_dir / "mips-sim"),
                "run",
                str(repo_root / "tests" / "golden" / "parallel_coherence_demo.s"),
                "--mode",
                "parallel",
                "--cores",
                str(args.cores),
                "--coherence",
                "snoop",
                "--consistency",
                "sc",
                "--interconnect",
                "bus",
                "--stats",
            ]
        )
        require(result.returncode == 0, f"parallel simulator benchmark failed: {result.stderr}")
        rows.append(parse_parallel_summary(result.stdout))

    if requested["gpu"]:
        gpu_bench = bin_dir / "gpu-bench"
        if not gpu_bench.exists():
            rows.append(
                {
                    "suite": "gpu",
                    "case": "vector-add",
                    "kernel": "vector-add",
                    "status": "skipped",
                    "reason": "cuda-disabled",
                }
            )
        else:
            result = run([str(gpu_bench), "--kernel", "all", "--size", "64"])
            require(result.returncode == 0, f"gpu benchmark failed: {result.stderr}")
            parsed = parse_kv_lines(result.stdout)
            require(parsed, "gpu benchmark produced no machine-readable output")
            for row in parsed:
                row["suite"] = "gpu"
                row["case"] = row.get("kernel", "")
                rows.append(row)

    if args.csv:
        write_csv(args.csv, rows)
    if args.markdown:
        write_markdown(args.markdown, rows)

    for row in rows:
        if row.get("suite") == "parallel":
            print(
                f"suite=parallel case={row.get('case', '')} cycles={row.get('cycles', '')} "
                f"status={row.get('status', '')}"
            )
        elif row.get("suite") == "gpu":
            tokens = [
                "suite=gpu",
                f"kernel={row.get('kernel', '')}",
                f"status={row.get('status', '')}",
            ]
            if row.get("reason"):
                tokens.append(f"reason={row.get('reason', '')}")
            if row.get("checksum"):
                tokens.append(f"checksum={row.get('checksum', '')}")
            print(" ".join(tokens))
        else:
            print(
                f"suite={row.get('suite', '')} kernel={row.get('kernel', '')} "
                f"checksum={row.get('checksum', '')} status={row.get('status', '')}"
            )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
