# Compiler Overview

NexusLang now has a real frontend, middle-end, and initial backend execution path:

```text
source text
  -> lexer
  -> token stream
  -> parser
  -> AST
  -> semantic analysis
  -> IR lowering
  -> typed three-address IR
  -> CFG construction
  -> analyses and bounded passes
  -> MIPS backend lowering
  -> textual assembly
  -> educational execution models
```

## Implemented Through Phase 11

- handwritten lexer with keywords, operators, punctuation, integer literals, identifiers, and comments
- recursive-descent production parser for the supported NexusLang subset
- AST nodes for modules, functions, blocks, declarations, control flow, expressions, calls, and indexing
- diagnostics with line and column source locations
- semantic checks for scope, duplicate names, undefined identifiers, type mismatches, function calls, and bounded return analysis
- typed function-level IR with explicit temporaries, locals, blocks, and terminators
- CFG construction, dominators, symbolic analysis, alias analysis, interprocedural summaries, affine analysis
- bounded optimization-style passes including unrolling, interprocedural folding, and affine strip-mining
- stack-based IR-to-MIPS lowering with deterministic assembly emission
- functional, single-cycle, multi-cycle, pipeline, advanced, and parallel execution paths
- experimental parse comparison modes:
  - `nexusc experimental-parse <file> --mode parallel`
  - `nexusc experimental-parse <file> --mode bison-lr`

## Implementation Choice

The production frontend uses a handwritten lexer and parser because that keeps the educational
control flow visible and the core build simple.

At the same time, Nexus now includes an experimentally implemented Flex/Bison LR path beside the
handwritten frontend. This lets the repository compare parsing strategies without replacing the
production teaching path.

## Remaining Boundaries

- no production SSA form
- no generated-parser production default
- no industrial register allocator
- no ambiguity-supporting generalized parser
- no large polyhedral optimizer
