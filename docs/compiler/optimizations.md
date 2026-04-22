# Optimizations

Nexus now has both the Phase 3 analysis substrate and the first bounded optimization-oriented
prototypes from Phase 9.

## Implemented Foundations

- typed three-address IR
- explicit basic blocks and CFG edges
- dominator computation
- iterative liveness
- region-based liveness prototype
- symbolic expression/value analysis
- bounded alias summaries
- bounded interprocedural summaries

## Implemented Phase 9 Passes

### Concrete Loop Unroll

- fully unrolls `i < constant` loops with `+1` induction and trip count `<= 8`
- leaves a note in the textual output describing the transformation

### Symbolic Loop Unroll

- applies factor-2 unrolling to simple affine counted loops
- inserts a residual guard block rather than requiring a fully constant trip count

### Interprocedural Constant Folding

- detects tiny pure callees
- evaluates calls with constant arguments
- rewrites the call site to `const_int` or `const_bool`

CLI examples:

```bash
./build/bin/nexusc opt examples/source_lang/fixed_trip_unroll.nx --pass unroll
./build/bin/nexusc opt examples/source_lang/symbolic_unroll.nx --pass unroll-symbolic
./build/bin/nexusc opt examples/source_lang/interproc_fold.nx --pass interproc-constfold
```

## Data-Flow Framing

The implemented analyses remain deliberately small:

- iterative block solver for the baseline framework
- region-graph solver for a structured-loop prototype
- symbolic values limited to constants, names, and simple expressions
- alias summaries limited to locals, arrays, and call-by-reference summaries
- interprocedural reasoning limited to tiny single-block pure functions

## Deferred Work

- full reaching definitions and available expressions
- dead-code elimination
- copy propagation
- common subexpression elimination
- loop-invariant code motion
- strength reduction
- pass-manager scheduling

## Worked Documentation Topics

### Register Allocation

Nexus currently uses a simple stack-heavy backend discipline rather than a true allocator. A
documented worked example is:

```text
t0 = a + b
t1 = t0 + c
t2 = t1 + d
```

A linear-scan or graph-coloring allocator would try to keep `t0`, `t1`, and `t2` in a small
register set while spilling only when live ranges overlap too much. Nexus instead keeps the backend
deterministic and explicit by reloading or spilling through the frame when needed.

### Region And Locality Thinking

The existing region-based liveness prototype is intentionally small, but it demonstrates the key
idea that structured regions can reduce repeated whole-CFG iteration. Likewise, the loop
optimization documentation records locality and affine-scheduling ideas even where code is not yet
implemented.
