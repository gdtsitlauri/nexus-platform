# Generalized and Parallel Parsing

Phase 9 keeps the production frontend unchanged and adds bounded experimental parsing prototypes
under `src/compiler/experimental_parallel_parsing/`.

## Production vs Experimental Paths

- production path: handwritten lexer + recursive-descent parser in `src/compiler/frontend/`
- experimental path 1: parallel top-level parsing prototype
- experimental path 2: Flex/Bison LR parser path exposed through `--mode bison-lr`

The handwritten frontend remains the default compilation path. The experimental paths exist to make
alternative parsing strategies concrete without replacing the core teaching implementation.

## Implemented Experimental Prototypes

### Parallel Top-Level Parsing

The bounded parallel parser:

1. lexes the whole source
2. partitions top-level `fn ... { ... }` slices by matching braces
3. launches asynchronous workers per function slice
4. parses each slice with the existing production parser
5. shifts diagnostics back into whole-file coordinates
6. merges function ASTs in source order

CLI:

```bash
./build/bin/nexusc experimental-parse examples/source_lang/factorial.nx --mode parallel
```

### Flex/Bison LR Parsing

The repository also ships an experimentally implemented Flex/Bison LR path:

```bash
./build/bin/nexusc experimental-parse examples/source_lang/factorial.nx --mode bison-lr
```

This path is validated by unit tests and CLI integration, but it remains experimental rather than
the supported production frontend.

## Relation To Generalized Parsing

Nexus does not implement GLR or Earley parsing as executable code. Instead it documents the
connection:

- generalized parsing handles ambiguity and nondeterministic parse states
- the implemented `parallel` and `bison-lr` paths stay deterministic and bounded
- both experimental paths remain small enough to compare directly with the handwritten frontend

## Limitations

- the production compiler still uses the handwritten frontend
- the parallel parser only supports top-level function partitioning
- the `bison-lr` path is an experimental comparison path, not the default
- no generalized parse forest is constructed
- no ambiguity-supporting parser is shipped

## Evidence

- code: `src/compiler/experimental_parallel_parsing/`
- CLI: `nexusc experimental-parse ... --mode parallel|bison-lr`
- tests: `tests/unit/experimental_parse_test.cpp`, `tests/integration/nexusc_cli_test.py`
