# Nexus Architecture

Nexus is one repository in layers: a compiler for NexusLang, a family of MIPS execution models from a
functional interpreter to an out-of-order core, parallel and GPU models, a synthesisable Verilog CPU,
and stand-alone tools for the theory topics (automata, grammars, types, polyhedra, arithmetic).
Version 3.0.0 covers six courses (NEY613, NEY709, NEY221, EY321, NEY606, NEY704). All layers form one
flow, from a NexusLang program to the Verilog CPU with its I/O devices and to the multicore model;
`docs/FLOW.md` follows it step by step.

```mermaid
flowchart LR
  A[NexusLang source] --> B[lexer / parser / AST]
  B --> C[semantic analysis]
  C --> D[three-address IR, CFG, dominators]
  D --> E[analyses: liveness, SSA, SCCP, symbolic, affine, alias, interprocedural]
  D --> F[passes: unrolling, strip-mining, folding]
  D --> G[MIPS back end + linear-scan allocation]
  D --> H[ISA styles: stack / accumulator / register-memory]
  G --> I[MIPS assembly]
  I --> J[functional, single-cycle, multi-cycle, pipeline + caches/I/O]
  I --> K[advanced: in-order, VLIW, scoreboard, Tomasulo + ROB + SMT]
  I --> L[parallel: coherence, consistency, NoC, manycore]
  I --> M[SIMT GPU model]
  I --> N[MIPS32 encoder] --> O[Verilog 5-stage pipeline]
  O --> P[I/O bus: console, timer interrupt]
  I --> R[multithreading: coarse, fine, SMT]
  D --> W[polyhedral: dependences, unimodular transformations, ILP schedules]
  D --> X[modulo scheduling for VLIW]
  V[vector processor: strip-mining, chaining, lanes]
```

## Source Layout

| path | contents |
| --- | --- |
| `src/common` | build info, ALU helpers, FPU-lite, IEEE-754 soft float, number systems, UTF-8 |
| `src/compiler/frontend`, `semantics` | hand-written lexer and recursive-descent parser, AST, type checking, scopes |
| `src/compiler/ir` | typed three-address IR, lowering, printers (IR and quadruples) |
| `src/compiler/analysis` | CFG, dominators, data flow, liveness, region flow, symbolic, affine, alias, interprocedural, SSA/SCCP |
| `src/compiler/foundations` | hash tables, symbol tables, trees, graph traversal and closure algorithms |
| `src/compiler/passes` | loop unrolling (concrete, symbolic, remainder), affine strip-mining, interprocedural constant folding, modulo scheduling |
| `src/compiler/backend_mips` | MIPS code generation and linear-scan register allocation |
| `src/compiler/isa_styles` | stack (JVM-like), accumulator and register-memory (IA-32-like) back ends with interpreters |
| `src/compiler/experimental_parallel_parsing` | parallel top-level parsing, Flex/Bison LR parser |
| `src/compiler/formal` | regex/NFA/DFA/minimisation, grammars, LL(1), LR family, Earley, attribute grammars with inherited attributes |
| `src/compiler/types` | Hindley-Milner inference for a small ML |
| `src/compiler/polyhedral` | loop-nest dependence analysis, unimodular transformations, exact simplex, branch and bound, hyperplane schedules |
| `src/mips` | ISA tables, assembly text model, loader, MIPS32 encoder |
| `src/sim/functional`, `single_cycle`, `multi_cycle`, `pipeline` | the core simulator ladder (hardwired and microcoded control, hazards, forwarding, prediction) |
| `src/sim/memory`, `io`, `metrics` | L1/L2 caches, memory-mapped I/O, interrupts, DMA, statistics |
| `src/sim/advanced` | trace-driven in-order, VLIW-lite, scoreboard and Tomasulo/ROB/SMT schedulers; coarse/fine/SMT multithreading |
| `src/sim/parallel` | multicore with coherence, consistency, synchronisation and interconnects |
| `src/sim/vector` | VMIPS-style vector processor: strip-mining, chaining, lanes, convoys |
| `src/sim/simt` | warp-based SIMT GPU model |
| `src/hdl` | Verilog ALU, adder, multiplier, divider, register file, control, pipeline registers, CPU slice, pipelined CPU, console and timer devices on its I/O bus |
| `benchmarks/` | OpenMP, MPI, SIMD and optional CUDA programs |

## Design Rules

- every timing model must reproduce the functional result; `nexus_cross_model_differential` and the
  RTL co-simulation enforce this on every test program;
- theory tools are executable and checked against an independent reference (`std::regex`, host FPU,
  closed-form results, execution of original vs transformed loop nests);
- optional toolchains (OpenMP, MPI, CUDA, clang, Icarus Verilog, Yosys) never break the build; their
  tests are skipped or not registered when the tool is missing;
- HDL is plain Verilog-2005/SystemVerilog-2012 that Icarus Verilog simulates and Yosys synthesises.
