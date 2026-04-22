# Formal Foundations

Phase 2 adds concrete formal foundations material that directly maps to the frontend implementation.

## Grammars and Languages

- `docs/compiler/language_spec.md` defines the bounded NexusLang grammar in EBNF.
- the recursive-descent parser consumes an LL-friendly spelling of that grammar
- LR concepts are documented for comparison, but not implemented yet

Worked example:

- `examples/formal/grammar_worked_example.md`

## Finite Automata and State Machines

The handwritten lexer behaves like a deterministic finite automaton:

- start state decides between identifier, integer, operator, delimiter, or comment
- identifier state loops on letters, digits, and underscores
- integer state loops on digits
- operator states distinguish one-character operators from two-character operators such as `<=`
  and `!=`

Worked example:

- `examples/formal/identifier_dfa.md`

## Pushdown Automata Concepts

Nested blocks, parenthesized expressions, and parameter lists require stack discipline. The parser
is implemented as recursive descent, but the nesting behavior still mirrors a pushdown automaton:

- each recursive parse function corresponds to a grammar nonterminal
- entering a block or parenthesized expression pushes context
- consuming the closing delimiter pops that context

Worked example:

- `examples/formal/block_pda.md`

## Trees

The parser builds an AST with explicit nodes for:

- program
- functions
- block statements
- conditionals and loops
- returns
- expressions and calls

The AST is traversed twice in Phase 2:

- pretty-print traversal for `nexusc ast`
- semantic traversal for scope and type checks

## Graphs and Hash Tables

Phase 2 semantic analysis uses:

- nested scope graphs modeled as parent-child block relationships
- hash-table-like symbol tables per scope for declarations
- a function signature table for global lookup and recursion support

Worked example:

- `examples/formal/scope_graph_and_hash_tables.md`

## Traversal and Closure Algorithms

Phase 2 uses bounded but real traversal logic:

- preorder-style AST traversal for pretty printing
- semantic traversal over scopes and expressions
- return-guarantee propagation across blocks and `if`/`else`

Closure reasoning appears in a lightweight form:

- symbol lookup computes visibility through the transitive parent-scope chain
- return analysis computes whether both branches of a conditional close all control-flow paths

## LL and LR Discussion

Phase 2 implementation choice:

- implemented: handwritten recursive descent, aligned with LL-style grammar factoring
- documented only: LR-oriented toolchains such as Bison, which become more attractive when the
  grammar grows or when ambiguity experiments are part of the curriculum

This keeps the current frontend easy to read while still preserving the theory connection needed by
the course coverage.

### Worked Generated-Tool Example

To connect the handwritten path to tool-generated alternatives, consider the rule:

```text
expr -> expr '+' term | term
```

- in LL style, Nexus factors precedence into dedicated parse functions
- in LR style, a Bison grammar can keep this direct left-recursive shape and rely on precedence
  declarations

That contrast is the main reason the repository documents both styles even though only the
handwritten path is used in the build.
