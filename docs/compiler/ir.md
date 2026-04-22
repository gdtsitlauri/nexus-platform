# Intermediate Representation

Nexus Phase 3 introduces a real, typed, non-SSA intermediate representation designed for analysis
first and backend lowering later. The current IR is intentionally small: it is expressive enough
for structured control flow and later MIPS lowering work, but it avoids premature SSA or backend
complexity.

## Design Goals

- deterministic textual dumps for tests and CLI inspection
- explicit temporaries and local storage slots
- function-level control flow with named basic blocks
- enough typing to support semantic preservation and later code generation
- a structure that is simple to analyze with CFG, dominators, and data-flow

## Core Model

The IR is organized as:

- `Module`: collection of lowered functions
- `Function`: return type, parameters, locals, values, basic blocks, entry block
- `BasicBlock`: block id, human-readable label, instruction list, terminator
- `Instruction`: side-effect-free value definitions or explicit local/array storage operations
- `Terminator`: `jump`, `branch`, or `return`

The current type system mirrors the implemented NexusLang subset:

- `int`
- `bool`
- `void`
- array types such as `int[4]`

## Instruction Set

Phase 3 implements the following instruction families:

- constants:
  - `const_int`
  - `const_bool`
- local storage:
  - `load <local>`
  - `store <local>, <value>`
- array storage:
  - `load_element <local>[<index>]`
  - `store_element <local>[<index>], <value>`
- scalar operations:
  - unary `neg`, `not`
  - binary `add`, `sub`, `mul`, `div`, `mod`
  - comparisons such as `lt`, `le`, `eq`, `ne`
  - logical `and`, `or`
- calls:
  - scalar value arguments
  - local-reference arguments for arrays
- terminators:
  - `jump bbN`
  - `branch %cond, bbT, bbF`
  - `return` or `return %value`

## Lowering Strategy

Lowering consumes the Phase 2 checked AST and emits:

- one `Function` per NexusLang function
- one `LocalInfo` per parameter or mutable local
- one `ValueInfo` per computed temporary
- structured basic blocks for `if` and `while`

Selected lowering rules:

- variable declaration with initializer:
  - lower initializer expression to a value
  - emit `store local, value`
- assignment:
  - identifier target lowers to `store`
  - indexed target lowers to `store_element`
- conditionals:
  - emit condition value
  - create explicit `then`, `else`/fallthrough, and continuation blocks
- loops:
  - create explicit condition, body, and continuation blocks
- calls:
  - scalar arguments pass by value
  - array arguments pass as local references in the current IR

## Textual Dump

`nexusc ir <file>` prints a deterministic textual form. Example from
`examples/source_lang/factorial.nx`:

```text
func factorial(n: int) -> int {
  locals:
    n: int [param]
  bb0.entry:
    %0: int = const_int 1
    %1: int = load n
    %2: bool = le %1, %0
    branch %2, bb1, bb2
  bb1.if.then:
    %3: int = const_int 1
    return %3
  bb2.if.cont:
    %4: int = const_int 1
    %5: int = load n
    %6: int = sub %5, %4
    %7: int = call factorial(%6)
    %8: int = load n
    %9: int = mul %8, %7
    return %9
}
```

## Relation to CFG and Analyses

The IR is deliberately block-structured so later phases can build analyses directly on it:

- `nexusc cfg <file>` shows predecessor/successor relationships
- `nexusc dom <file>` shows dominator sets and immediate dominators
- `nexusc analysis liveness <file>` shows block-local use/def and fixed-point in/out sets

## Phase 3 Limitations

- no SSA form or phi nodes
- no globals
- no aggregate literals
- no backend-specific calling convention or register assignment
- no optimization passes yet; only the analysis substrate is present
