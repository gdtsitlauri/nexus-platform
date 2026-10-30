# NexusLang Examples

Example programs (used by the study guides in [docs/courses](../../docs/courses/README.md)):

- `tiny.nx`: one expression, to follow every compiler phase (lex, ast, quads, ir, compile)
- `factorial.nx`: recursion and function calls, used for IR dumps and end-to-end MIPS execution
- `arrays_and_loops.nx`: arrays, indexing, while loops, CFGs, dominators, liveness, and backend execution
- `fixed_trip_unroll.nx`: fixed-trip counted loop used by the concrete unroll pass
- `symbolic_unroll.nx`: affine counted loop used by the symbolic factor-2 unroll pass
- `interproc_fold.nx`: tiny pure-call example used by interprocedural summaries and call folding
- `invalid_syntax.nx`: parser-negative example
- `invalid_semantics.nx`: semantic-negative example
