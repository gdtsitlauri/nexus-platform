# Interconnects

## Implemented Demos

The Phase 10 parallel simulator exposes three bounded interconnect abstractions:

- `bus`
- `switch`
- `noc-lite`

These do not model a full network stack. They provide deterministic message and latency accounting
for coherence and shared-memory traffic.

## Usage

```bash
./build/bin/mips-sim run tests/golden/parallel_coherence_demo.s --mode parallel --interconnect bus --stats
./build/bin/mips-sim run tests/golden/parallel_coherence_demo.s --mode parallel --interconnect switch --stats
./build/bin/mips-sim run tests/golden/parallel_coherence_demo.s --mode parallel --interconnect noc-lite --stats
```

## What Changes

- `bus`: broadcast-like cost, highest shared traffic penalty in the tiny demos
- `switch`: point-to-point style lower penalty
- `noc-lite`: hop-based penalty using a tiny 2x2-grid intuition for up to 4 cores

## Validation

`tests/unit/interconnect_demo_test.cpp` checks that the same sharing workload produces different
`Interconnect cycles` while preserving the same architectural result.
