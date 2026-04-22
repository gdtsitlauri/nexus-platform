# Interprocedural Analysis

Phase 9 adds a bounded interprocedural-summary layer.

## What It Computes

For each function, the current prototype records:

- function name
- whether the function is pure in the bounded model
- whether it is tiny enough for Phase 9 consumers
- whether it writes memory
- whether it calls other functions
- a symbolic return summary when one can be derived

## Current Consumer

`src/compiler/passes/src/interprocedural_pass.cpp` uses these summaries to fold constant calls:

- only tiny pure callees are considered
- all call arguments must be compile-time constants in the bounded evaluator
- the call is rewritten to `const_int` or `const_bool` in the IR

CLI:

```bash
./build/bin/nexusc opt examples/source_lang/interproc_fold.nx --analysis interproc
./build/bin/nexusc opt examples/source_lang/interproc_fold.nx --pass interproc-constfold
```

## Limitations

- no global call-graph SCC handling
- no recursive optimization
- no side-effect summaries beyond a bounded purity flag
- no inlining beyond constant-call folding
