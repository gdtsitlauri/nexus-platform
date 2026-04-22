# Consistency

## Implemented Models

Nexus provides:

- `sc`: sequential-consistency baseline
- `weak-lite`: buffered-store educational model

The weak model is intentionally small. It does not attempt to formalize a real ISA memory model;
instead it demonstrates a concrete and testable difference in visibility timing.

## SC Baseline

In `sc` mode:

- stores become visible to all cores immediately
- loads read the shared committed state
- lock and barrier operations act directly on globally visible state

## Weak-Lite Model

In `weak-lite` mode:

- stores first enter a per-core buffer
- a deterministic drain policy commits pending stores later
- loads may forward from the issuing core's own store buffer
- remote cores do not observe buffered stores until drain

This makes the same tiny litmus program retire with a different cycle count and explicit
`Store-buffer flushes` accounting while preserving correctness for the supported examples.

## Synchronization Primitives

The parallel simulator adds bounded memory-mapped synchronization operations:

- lock acquire
- lock release
- barrier arrival/release
- atomic fetch-and-increment

These are sufficient for the tiny Phase 10 demos and tests:

- `tests/unit/parallel_model_test.cpp`
- `tests/golden/parallel_atomic_demo.s`
- `tests/golden/parallel_consistency_demo.s`

## CLI

```bash
./build/bin/mips-sim run tests/golden/parallel_consistency_demo.s --mode parallel --consistency sc --stats
./build/bin/mips-sim run tests/golden/parallel_consistency_demo.s --mode parallel --consistency weak-lite --stats
```

## Known Limits

- no formal proof machinery
- no fences beyond the bounded synchronization abstractions above
- no attempt to model full TSO, Power, ARM, or C/C++ memory-model semantics
