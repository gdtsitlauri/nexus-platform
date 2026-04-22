# Toolchain Comparison

Phase 9 introduced a tiny reproducible comparison workflow under `tools/toolchain_compare/`.
Phase 11 retains it as part of the final validation surface.

## Workflow

```bash
python3 tools/toolchain_compare/run_toolchain_compare.py ./build/bin/nexusc . /tmp/nexus_toolchain_compare
```

Inputs:

- Nexus source: `examples/source_lang/interproc_fold.nx`
- C analogue: `tools/toolchain_compare/interproc_fold.c`

Generated artifacts:

- `nexus_interproc_fold.s`
- `nexus_interproc_fold.folded.ir`
- `clang_interproc_fold_O0.s`
- `clang_interproc_fold_O1.s`
- `toolchain_comparison_summary.json`
- `toolchain_comparison_summary.md`

## What Is Compared

- original Nexus backend output
- Nexus interprocedural constant-folded IR
- local `clang -S -O0`
- local `clang -S -O1`

The current comparison focuses on tiny call-structure changes rather than claiming production
equivalence with `clang`.

## Why It Matters In The Final Repository

This workflow gives Nexus one concrete external point of comparison and helps keep the optimization
claims grounded.
