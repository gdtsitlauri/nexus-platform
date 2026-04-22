# Toolchain Compare

Phase 9 uses this directory for tiny, reproducible comparisons between Nexus outputs and local
external toolchains.

Current workflow:

```bash
python3 tools/toolchain_compare/run_toolchain_compare.py ./build/bin/nexusc . /tmp/nexus_toolchain_compare
```

Inputs:

- `examples/source_lang/interproc_fold.nx`
- `tools/toolchain_compare/interproc_fold.c`

Outputs:

- `toolchain_comparison_summary.json`
- `toolchain_comparison_summary.md`
- emitted Nexus assembly and folded IR snapshots
- local `clang -S -O0` and `clang -S -O1` assembly snapshots
