# Semantic Models

Phase 2 implements a bounded semantic-analysis pass over the AST. The goal is not a full attribute
grammar engine yet, but the structure is intentionally close to attribute-inspired analysis.

## Phase 2 Methodology

The semantic pass mixes two styles of information flow:

- inherited information:
  - current scope chain
  - current function return type
  - global function signature table
- synthesized information:
  - expression type
  - statement return-guarantee result

This mirrors an attribute-oriented view:

- identifiers inherit lookup context from enclosing scopes
- expressions synthesize a type back to their parent node
- blocks synthesize whether they guarantee a return

## Implemented Checks

- duplicate function definitions
- duplicate parameter or local names in the same scope
- undefined identifiers
- direct function-call resolution and argument-count checks
- basic type checking for unary and binary operators
- assignment target validation
- variable initializer type checking
- `if` and `while` condition type checking
- return type checking
- bounded "not all control paths return" check for non-void functions

## Scope Model

- functions are collected into a global signature table before body analysis
- each block introduces a new lexical scope
- symbol lookup walks outward through parent scopes
- recursion works because function signatures are available before body checking

## Type Model

Phase 2 supports:

- `int`
- `bool`
- `void`
- array types with postfix extents such as `int[4]`

Current restrictions:

- no implicit conversions
- no structs
- no array return types
- equality is limited to matching scalar operands

## Example

Source:

```nexus
fn main() -> int {
  var flag: bool = true;
  var x: int = flag;
  return x;
}
```

Semantic interpretation:

- `flag` synthesizes type `bool`
- initializer of `x` synthesizes `bool`
- declaration of `x` expects `int`
- mismatch diagnostic is emitted at the initializer span

## Future Extension Points

- richer attribute propagation for control-flow-sensitive typing
- typed symbol payloads for functions, globals, and user-defined aggregates
- deeper IR-facing annotation and lowering hooks beyond the current checked-AST to IR path
- data-flow-driven semantic refinement in later phases

## Advanced Type-System Notes

Nexus intentionally keeps the production language small, but it now documents richer type-system
ideas with worked examples:

- product-type idea: a record-like value can be modeled as named fields with a fixed layout
- effect-style idea: a function summary can record whether it writes memory or behaves as a pure
  expression
- parametric-shape idea: `int[n]` can be viewed as a simple type constructor over an element type
  and extent

Worked example:

```text
record Pair { left: int, right: int }
```

The repository does not implement this record syntax in the frontend, but the example is useful for
explaining why later semantic and alias analyses often need richer type payloads than the current
Phase 2 checker.
