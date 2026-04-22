# Parallel Overview

## Final Scope

The final repository contains a bounded parallel-systems layer without replacing the earlier
single-core simulator stack. It now includes:

- a deterministic shared-memory multicore simulator in `src/sim/parallel/`
- coherence-lite support with `snoop` and `directory-lite` modes
- consistency-lite support with `sc` and `weak-lite`
- synchronization primitives implemented as educational memory-mapped operations
- tiny OpenMP, MPI, and SIMD benchmark integrations
- optional CUDA demo integration through a feature gate
- interconnect comparison hooks for `bus`, `switch`, and `noc-lite`

## Taxonomy Notes

The parallel syllabus asks for several taxonomy comparisons that the repository covers at different
depths.

- shared-memory systems: experimentally implemented through `src/sim/parallel/`
- distributed-memory systems: experimentally implemented through the tiny MPI benchmark path
- symmetric multiprocessors: experimentally implemented through the bounded equal-core multicore
  simulator
- asymmetric multiprocessors: documented with worked examples only; Nexus does not ship an
  executable asymmetric-core model
- homogeneous systems: experimentally implemented through the shared-core simulator and CPU-first
  benchmark paths
- heterogeneous systems: experimentally implemented in bounded form through SIMD plus the optional
  CUDA demo path
- multithreaded and SMT concepts: documented with worked examples only; the repository explains the
  concept relative to multicore execution but does not implement hardware-threaded cores

In other words, Nexus implements a small shared-memory SMP-style teaching platform and then uses
MPI, SIMD, and optional CUDA examples to connect that core to broader parallel-system categories.

## Parallel Simulator

`mips-sim` now supports:

```bash
./build/bin/mips-sim run tests/golden/parallel_coherence_demo.s --mode parallel --cores 2
./build/bin/mips-sim run tests/golden/parallel_consistency_demo.s --mode parallel --consistency weak-lite --stats
./build/bin/mips-sim run tests/golden/parallel_atomic_demo.s --mode parallel --trace
```

The simulator is intentionally small:

- up to 2 to 4 educational cores by default
- deterministic round-robin issue
- shared backing memory with private coherence-tracked word caches
- no multicore timing pipeline, cache-hierarchy deepening, or OS/runtime modeling

## Benchmark Paths

- `benchmarks/openmp/openmp_bench.cpp`: vector-add, reduction, matmul, branch-mix
- `benchmarks/mpi/mpi_bench.cpp`: reduction and ring exchange
- `benchmarks/simd/simd_bench.cpp`: scalar vs SIMD vector-add and dot product
- `parallel-bench`: tiny wrapper that runs small OpenMP, MPI, SIMD, and parallel-simulator summaries

## Known Limits

- the multicore simulator is a correctness-first educational model, not a cycle-accurate multicore CPU
- coherence is word-granular and intentionally simplified
- `weak-lite` models delayed visibility through a tiny store buffer; it is not a full formal architecture model
- no hardware multithreading or SMT core model is implemented
- no asymmetric-core executable model is implemented
- MPI runs use small process counts only and are documented around WSL-safe `--oversubscribe` execution
- GPU remains optional and does not block CPU-only validation
