# Benchmark Report Template

## Purpose

This template standardizes the tiny benchmark and experiment outputs used by Nexus.

## Current Inputs

- raw CSV from `scripts/run_benchmarks.py`
- aggregated CSV and markdown from `scripts/aggregate_benchmarks.py`
- final markdown from `scripts/generate_benchmark_report.py`
- raw CSV from `scripts/run_advanced_experiments.py`
- markdown plus Amdahl report from `scripts/generate_amdahl_report.py`
- raw CSV or markdown from `parallel-bench`

## Suggested Workflow

```bash
python3 scripts/run_benchmarks.py ./build/bin/nexusc ./build/bin/mips-sim . /tmp/nexus_bench.csv
python3 scripts/aggregate_benchmarks.py /tmp/nexus_bench.csv /tmp/nexus_summary.csv /tmp/nexus_summary.md
python3 scripts/generate_benchmark_report.py /tmp/nexus_bench.csv /tmp/nexus_summary.md /tmp/nexus_report.md
python3 scripts/run_advanced_experiments.py ./build/bin/nexusc ./build/bin/mips-sim . /tmp/nexus_adv.csv
python3 scripts/generate_amdahl_report.py /tmp/nexus_adv.csv /tmp/nexus_adv_summary.md /tmp/nexus_amdahl.md
./parallel-bench --all --build-dir ./build --repo-root . --csv /tmp/nexus_parallel.csv --markdown /tmp/nexus_parallel.md
```

Optional CUDA run:

```bash
./parallel-bench --gpu --build-dir ./build-cuda --repo-root .
```

## Suggested Sections

1. Scope and tiny workload selection
2. Execution modes compared
3. Metrics captured
4. Correctness notes versus timing notes
5. Parallel benchmark summary
6. Optional GPU status
7. Known limitations

## Metrics Used Across The Repository

- instructions
- cycles
- CPI
- IPC
- stalls
- flushes
- cache hits and misses
- branch prediction counts
- interconnect and synchronization counts
- checksum-based benchmark correctness summaries
