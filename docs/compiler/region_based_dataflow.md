# Region-Based Data Flow

Phase 3 introduced iterative liveness over basic blocks. Phase 9 adds a second bounded prototype:
region-based liveness over a condensed control-flow graph.

## Implemented Prototype

Implementation lives in `src/compiler/analysis/src/region_flow.cpp`.

The solver works in four stages:

1. Build the normal CFG.
2. Detect loop backedges using dominators.
3. Collapse a loop header/body pair into a single region summary.
4. Compute liveness over the condensed region graph in reverse topological order.

This is not a full structured-region framework, but it is a real non-iterative prototype for the
small CFG shapes that Nexus currently emits.

CLI:

```bash
./build/bin/nexusc opt examples/source_lang/arrays_and_loops.nx --analysis region-liveness
```

Example output:

```text
func sum4 region-liveness:
  region0 (block) {bb0.entry}
  region1 (loop) {bb1.while.cond, bb2.while.body}
  region2 (block) {bb3.while.cont}
```

## Comparison With Iterative Liveness

- iterative baseline:
  - operates directly on every basic block
  - uses repeated fixed-point iteration
- region prototype:
  - summarizes small structured loop regions first
  - solves on a smaller graph when that structure exists

For the bounded `while` loops emitted by the Phase 3 lowering path, both analyses agree on the
important live-in/live-out sets used by the tests.

## Limitations

- only simple natural-loop patterns are recognized
- no arbitrary interval hierarchy is built
- irreducible CFGs are not handled specially
- this prototype summarizes liveness only; it is not yet a generic region framework for every data-flow problem

## Evidence

- code: `src/compiler/analysis/{include,src}/region_flow.*`
- CLI: `nexusc opt ... --analysis region-liveness`
- tests: `tests/unit/symbolic_analysis_test.cpp`
