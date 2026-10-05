# Loop Optimizations

Phase 9 adds bounded loop transformations and analysis over the existing IR.

## Implemented Passes

Implementation lives in:

- `src/compiler/passes/src/loop_unroll.cpp`
- `src/compiler/analysis/src/affine_analysis.cpp`
- `src/compiler/passes/src/affine_stripmine.cpp`

### Concrete Unroll

The concrete pass fully unrolls a loop when all of these hold:

- loop shape matches the current lowered `while` pattern
- condition is `i < constant`
- induction step is `+1`
- trip count is at most 8

CLI:

```bash
./build/bin/nexusc opt examples/source_lang/fixed_trip_unroll.nx --pass unroll
```

### Symbolic Unroll

The symbolic pass does not require a constant loop bound. Instead it applies bounded factor-2
unrolling when it can recognize:

- condition shape `i < limit`
- induction step `+1`
- normal counted-loop CFG pattern

CLI:

```bash
./build/bin/nexusc opt examples/source_lang/symbolic_unroll.nx --pass unroll-symbolic
```

### Affine Analysis and Strip-Mining

Nexus now includes a bounded affine/locality slice:

- affine-loop recognition
- trip-count reporting where recoverable
- memory-local summary output
- strip-mining with tile factor 2 and a residual guard

CLI:

```bash
./build/bin/nexusc opt examples/source_lang/arrays_and_loops.nx --analysis affine
./build/bin/nexusc opt examples/source_lang/fixed_trip_unroll.nx --pass strip-mine
```

## Locality Interpretation

The affine/locality slice remains intentionally small. It does not claim a full polyhedral
framework, but it does make locality-oriented reasoning executable:

- bounded affine loops can be identified
- memory-local arrays are summarized
- strip-mining exposes a concrete locality-related transformation

## Polyhedral Loop Transformations (1.0.0)

Loop interchange, reversal and skewing with dependence-based legality checks, Fourier-Motzkin
loop-bound generation and execution-based verification are implemented in `src/compiler/polyhedral/`
(`nexusc poly`). See `docs/compiler/polyhedral.md`. Loop-invariant code motion and strength
reduction are not separate passes; SCCP (`nexusc analysis sccp`) covers constant propagation.

## Evidence

- code: `src/compiler/analysis/src/affine_analysis.cpp`, `src/compiler/passes/src/affine_stripmine.cpp`
- tests: `tests/unit/affine_analysis_test.cpp`, `tests/unit/unroll_pass_test.cpp`
- CLI coverage: `tests/integration/nexusc_cli_test.py`
