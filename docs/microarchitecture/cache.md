# Cache

## Purpose

This document describes the bounded memory/cache layer used by `mips-sim --mode pipeline` in the
final repository state.

## Implemented Scope

- flat backing memory with deterministic word-addressed correctness
- configurable flat-memory latency for cache-off runs
- direct-mapped or set-associative L1 cache modes
- optional bounded L2 cache beneath an enabled L1
- configurable set count, line size, way count, hit latency, and miss penalty for both levels
- hit/miss/access statistics exposed in `--stats` output

## Current Configuration Surface

`mips-sim` supports:

```bash
./build/bin/mips-sim run tests/golden/cache_stats_demo.s --mode pipeline --cache off --stats
./build/bin/mips-sim run tests/golden/cache_stats_demo.s --mode pipeline --cache direct --stats
./build/bin/mips-sim run tests/golden/cache_stats_demo.s --mode pipeline --cache assoc --ways 2 --stats
./build/bin/mips-sim run /tmp/arrays.s --mode pipeline --cache direct --sets 1 --line-words 1 --cache-l2 assoc --l2-ways 2 --l2-sets 2 --l2-line-words 1 --stats
```

Supported cache flags:

- `--cache off|direct|assoc`
- `--sets N`
- `--line-words N`
- `--ways N`
- `--cache-hit-latency N`
- `--cache-miss-penalty N`
- `--memory-latency N`
- `--cache-l2 off|direct|assoc`
- `--l2-sets N`
- `--l2-line-words N`
- `--l2-ways N`
- `--l2-hit-latency N`
- `--l2-miss-penalty N`

## Educational Model

The final cache model is still intentionally bounded, but it is no longer single-level only.
Nexus now supports an executable L1 plus optional L2 hierarchy for small educational experiments.
The goals remain:

- visible address to block/set/tag mapping
- visible hits versus misses
- visible latency contribution to pipeline stalls
- visible contrast among direct-mapped, bounded associativity, and bounded L2 rescue behavior

Memory correctness stays anchored in the flat backing store. Cache state adds timing/statistics
behavior plus copied line contents for the educational hit/miss model.

## Statistics

`mips-sim --stats` reports:

- memory accesses, reads, and writes
- aggregate cache hits and misses
- L1 hits and misses
- L2 hits and misses when L2 is enabled
- miss rates
- cycles, CPI, and IPC where the execution mode supports them

Example excerpt:

```text
Mode: pipeline
Cache: direct-mapped + L2 2-way set-associative
Program exited with code 10
Cache hits: 1
Cache misses: 2
L1 hits: 0
L1 misses: 3
L2 hits: 1
L2 misses: 2
```

## Validation Evidence

- `src/sim/memory/include/nexus/sim/memory/system.hpp`
- `src/sim/memory/src/system.cpp`
- `tests/unit/memory_cache_test.cpp`
- `tests/unit/pipeline_memory_system_test.cpp`
- `tests/integration/nexusc_cli_test.py`
- `tests/golden/cache_stats_demo.s`

## Current Limitations

- write-through educational policy only; no write-back implementation
- no TLB or virtual-memory layer
- L2 remains a bounded teaching model rather than a large industrial hierarchy
- the executable hierarchy is validated through small examples, not large trace-driven studies
