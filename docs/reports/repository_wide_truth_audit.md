# Repository-Wide Truth Audit

Date: 2026-04-22

## 1. AUDIT PLAN

- inspected the maintained project-contract files: `README.md`, `ROADMAP.md`, `STATUS.md`,
  `ARCHITECTURE.md`, `CHANGELOG.md`, `CMakeLists.txt`, and `Makefile`
- inventoried the repository structure, then inspected `docs/`, `src/`, `tests/`, `examples/`,
  `scripts/`, `tools/`, `benchmarks/`, `config/`, and `cmake/`
- cross-checked code claims against unit tests, integration tests, golden tests, examples, CLI help
  surfaces, and support scripts
- used the six course syllabi as formal inputs; five were present directly under `data/`, and the
  sixth was available as the same-named repo-local PDF under `data/` rather than `/mnt/data/`
- validated buildability and test status with the existing `build/` tree using low-memory commands:
  `ninja -j2` and `ctest --output-on-failure -j1`
- treated `README.md`, `ROADMAP.md`, `STATUS.md`, `ARCHITECTURE.md`, and `CHANGELOG.md` as the
  maintained project brief/phase-plan surface because no separate standalone "mega-project brief"
  file was present in the repository snapshot

## 2. REPOSITORY-WIDE SUMMARY

Nexus is a bounded educational/research-grade repository that combines:

- a real handwritten compiler pipeline for a small NexusLang source language
- a bounded MIPS backend plus functional, single-cycle, multi-cycle, pipeline, advanced, and
  parallel simulator modes
- a small family of HDL modules and testbenches
- tiny OpenMP, MPI, SIMD, and optional CUDA benchmark/demo paths
- literature notes, course mapping, validation reports, and final-audit documentation

Top-level executable entry points and deliverables verified during the audit:

- `build/bin/nexusc`
- `build/bin/mips-sim`
- `./parallel-bench`
- `./hdl-test`
- `build/bin/openmp-bench`
- `build/bin/mpi-bench`
- `build/bin/simd-bench`
- optional `build/bin/gpu-bench` when configured with `-DNEXUS_ENABLE_CUDA=ON`

## 3. WHAT IS DONE OVERALL

- handwritten lexer, recursive-descent parser, AST construction, semantic analysis, diagnostics, IR
  lowering, CFG construction, dominators, liveness, and MIPS backend generation are all present as
  real code paths with tests and CLI exposure
- the core simulator ladder is real and validated: functional, single-cycle, multi-cycle,
  pipelined, advanced sandbox, and bounded multicore/parallel modes
- the repository has working machine-level support for stack frames, calls/returns, arrays, basic
  arithmetic, memory access, and end-to-end compile/run flows
- the pipeline model includes hazards, forwarding, load-use stalls, flushes, branch prediction, and
  metrics output
- the memory and system layer includes bounded cache modeling, L1 plus optional L2 support, I/O,
  timer interrupts, and DMA
- advanced and compiler-research slices exist as executable bounded experiments rather than prose
  only: symbolic analysis, region-liveness, alias summaries, interprocedural summaries, loop
  unrolling, affine/locality analysis, VLIW-lite, scoreboard, coherence-lite, and consistency-lite
- OpenMP, MPI, SIMD, optional CUDA, and Verilog HDL artifacts are all present in repository flow
- the repository contains enough literature/report material to make theory-heavy topics explicit
  without pretending they are fully industrial implementations

## 4. WHAT IS ONLY PARTIAL / EXPERIMENTAL

- FPU-lite floating/fixed-point demo support in `src/common/`
- L1 plus optional L2 hierarchy in the memory/pipeline path
- Flex/Bison LR parser comparison path
- scoreboard-based advanced scheduling
- symbolic analysis, region-based liveness, alias summaries, interprocedural summaries, and
  loop/affine/locality passes
- advanced architecture sandbox features such as width experiments, VLIW-lite, and speculative
  flush accounting
- multicore shared-memory simulator, coherence-lite, consistency-lite, and interconnect-lite modes
- OpenMP/MPI/SIMD benchmark integrations and the optional CUDA demo path
- HDL control/datapath modules and the bounded HDL CPU slice

These are real, buildable repository slices, but each remains intentionally small relative to
industrial tools or research simulators.

## 5. WHAT IS DOC-ONLY

- alternative ISA families beyond the implemented MIPS subset: accumulator, stack, IA-32, and Java
  bytecode comparisons
- generalized parsing beyond the shipped deterministic experimental parsers
- advanced type-system topics beyond the current source-language checker
- Tomasulo/reservation-station and broader out-of-order execution theory
- deeper polyhedral scheduling theory beyond the affine/locality prototype
- parts of the parallel taxonomy such as SMT and asymmetric systems
- literature-study integration across architecture, compiler, and parallel topics

These topics are covered through worked examples, explanatory notes, or reading summaries rather
than through a fully validated executable subsystem.

## 6. WHAT IS NOT DONE / OUTSIDE BOUNDED SCOPE

- full synthesizable HDL CPU
- full IEEE-754 execution pipeline or floating-point register file
- reorder-buffer out-of-order core
- Tomasulo-style rename/broadcast execution as code
- ambiguity-supporting generalized parser such as GLR or Earley
- industrial SSA form and industrial register allocation
- full polyhedral optimizer
- mandatory device-backed GPU success in every validated build
- large manycore platform or industrial-strength coherence hierarchy
- operating-system, virtual-memory, and distributed-runtime infrastructure beyond the bounded demos

## 7. BUILD AND TEST EVIDENCE

Build command used:

```bash
cd build
ninja -j2
```

Focused test commands used:

```bash
cd build
ctest --output-on-failure -j1 -R '^nexus_fpu_lite_test$'
ctest --output-on-failure -j1 -R '^(nexus_memory_cache_test|nexus_pipeline_memory_system_test|nexus_phase10_cli)$'
ctest --output-on-failure -j1 -R '^nexus_experimental_parse_test$'
ctest --output-on-failure -j1 -R '^nexus_advanced_model_test$'
ctest --output-on-failure -j1 -R '^(nexus_affine_analysis_test|nexus_unroll_pass_test)$'
ctest --output-on-failure -j1 -R '^nexus_gpu_optional$'
ctest --output-on-failure -j1 -R '^(nexus_hdl_cpu_slice|nexus_hdl_all)$'
```

Supplementary GPU wrapper probe used:

```bash
./parallel-bench --gpu --build-dir ./build --repo-root .
```

Full-suite command used:

```bash
cd build
ctest --output-on-failure -j1
```

Verified outcomes:

- `ninja -j2`: success, existing build tree reused
- focused slice validation:
  - FPU-lite: passed
  - L1+L2 hierarchy: passed
  - Flex/Bison LR path: passed
  - scoreboard scheduler: passed
  - affine/locality slice: passed
  - stronger GPU demo: passed with clean CUDA-disabled skip
  - HDL CPU slice: passed
- final full-suite result after the post-documentation rerun: 56/56 passed, 0 failed, total real
  time 3.40 sec

GPU reality in the CPU-only validated build:

```text
suite=gpu kernel=vector-add status=skipped reason=cuda-disabled
```

## 8. SIX-COURSE COVERAGE TABLE

| Course | Topic | Classification | Evidence | Notes/Gaps |
| --- | --- | --- | --- | --- |
| 1. Principles of Computer Operation | general computer organization | experimentally implemented | `ARCHITECTURE.md`, `src/sim/single_cycle/`, `src/sim/multi_cycle/`, `src/sim/pipeline/` | real machine-model ladder exists, but bounded to the Nexus subset |
| 1. Principles of Computer Operation | MIPS assembly programming | fully implemented | `docs/architecture/mips_isa.md`, `src/compiler/backend_mips/`, `src/sim/functional/`, `nexusc compile`, `mips-sim run` | implemented around a bounded educational MIPS subset |
| 1. Principles of Computer Operation | registers, memory, stack, subroutines | fully implemented | backend stack-frame lowering, `jal`/`jr`, `lw`/`sw`, simulator tests | validated in compiler, loader, and simulator flow |
| 1. Principles of Computer Operation | accumulator ISA concepts | documented with worked examples | `docs/architecture/isa_comparison.md` | comparison only; no executable accumulator machine |
| 1. Principles of Computer Operation | stack ISA concepts | documented with worked examples | `docs/architecture/isa_comparison.md` | comparison only; no executable stack VM |
| 1. Principles of Computer Operation | general-purpose-register ISA concepts | fully implemented | MIPS backend and simulator stack | implemented through the shipped MIPS path |
| 1. Principles of Computer Operation | IA-32 comparison concepts | documented with worked examples | `docs/architecture/isa_comparison.md` | comparison only |
| 1. Principles of Computer Operation | Java bytecode comparison concepts | documented with worked examples | `docs/architecture/isa_comparison.md` | comparison only |
| 1. Principles of Computer Operation | signed representation | experimentally implemented | `docs/architecture/data_representation.md`, `src/common/src/arithmetic.cpp`, `tests/unit/arithmetic_units_test.cpp` | real helpers and machine behavior, but no overflow-trap machinery |
| 1. Principles of Computer Operation | unsigned representation | experimentally implemented | `sltu`/`sltiu` paths, arithmetic helpers/tests, `docs/architecture/data_representation.md` | real support in the bounded subset |
| 1. Principles of Computer Operation | fixed-point concepts | experimentally implemented | `src/common/src/fpu_lite.cpp`, `tests/unit/fpu_lite_test.cpp`, `mips-sim fp-demo` | demo/library support only, not ISA-integrated fixed-point execution |
| 1. Principles of Computer Operation | floating-point concepts | experimentally implemented | `src/common/src/fpu_lite.cpp`, `tests/unit/fpu_lite_test.cpp`, `mips-sim fp-demo` | no FP register file or full floating-point pipeline |
| 1. Principles of Computer Operation | non-numeric data representation | documented with worked examples | `docs/architecture/data_representation.md` | strings/bytes/struct lowering are not fully implemented |
| 1. Principles of Computer Operation | arithmetic algorithms overview | experimentally implemented | `src/common/src/arithmetic.cpp`, HDL arithmetic modules, docs | bounded integer arithmetic algorithms are real |
| 1. Principles of Computer Operation | adder hardware/model | experimentally implemented | `src/common/src/arithmetic.cpp`, `src/hdl/alu/nexus_adder.v`, tests | educational model and HDL module, not a full CPU datapath proof |
| 1. Principles of Computer Operation | multiplier hardware/model | experimentally implemented | `src/common/src/arithmetic.cpp`, `src/hdl/alu/nexus_iterative_multiplier.v`, tests | shift-add style educational implementation |
| 1. Principles of Computer Operation | divider hardware/model | experimentally implemented | `src/common/src/arithmetic.cpp`, `src/hdl/alu/nexus_iterative_divider.v`, tests | restoring divider style educational implementation |
| 2. Computer Organization | modern computer organization | experimentally implemented | simulator stack, architecture docs, reports | broad course theme is covered through layered executable models |
| 2. Computer Organization | history/evolution coverage | documented with worked examples | `docs/reports/history_of_computing_evolution.md` | literature/report coverage rather than code |
| 2. Computer Organization | MIPS machine-level understanding | fully implemented | backend, loader, interpreter, ISA docs, tests | strong executable support in repo flow |
| 2. Computer Organization | datapath and control | experimentally implemented | single-cycle/multi-cycle/pipeline code, HDL control unit, docs | bounded educational datapaths rather than full hardware implementation |
| 2. Computer Organization | micro-operations | experimentally implemented | multi-cycle traces and microcode/hardwired control steps | exposed through traceable control sequencing |
| 2. Computer Organization | single-cycle model | fully implemented | `src/sim/single_cycle/`, `tests/unit/single_cycle_model_test.cpp` | validated executable model |
| 2. Computer Organization | multi-cycle model | fully implemented | `src/sim/multi_cycle/`, `tests/unit/multi_cycle_model_test.cpp` | validated executable model |
| 2. Computer Organization | hardwired control | fully implemented | single-cycle decode, multi-cycle hardwired mode, HDL control unit | real code and HDL evidence |
| 2. Computer Organization | microprogrammed control | fully implemented | `--control microcode`, `src/sim/multi_cycle/src/control.cpp`, tests | bounded microcode control store model |
| 2. Computer Organization | pipelining | fully implemented | `src/sim/pipeline/`, `tests/unit/pipeline_model_test.cpp`, docs | implemented 5-stage model |
| 2. Computer Organization | hazards | fully implemented | pipeline tests, `docs/microarchitecture/hazards.md` | data/control hazards are real and tested |
| 2. Computer Organization | forwarding | fully implemented | pipeline forwarding logic/tests | forwarding events checked in trace output |
| 2. Computer Organization | stalls and flushes | fully implemented | load-use stall tests, branch flush tests | bounded but real pipeline behavior |
| 2. Computer Organization | branch prediction | fully implemented | pipeline predictor support, advanced predictor tests, docs | bounded predictors, not industrial branch hardware |
| 2. Computer Organization | memory hierarchy and caches | experimentally implemented | `src/sim/memory/`, `docs/microarchitecture/cache.md`, cache tests | bounded cache model only |
| 2. Computer Organization | I/O | experimentally implemented | `src/sim/io/`, `tests/unit/io_system_test.cpp`, demos | memory-mapped educational I/O |
| 2. Computer Organization | interrupts | experimentally implemented | timer interrupt path, tests, demos | bounded deterministic interrupt model |
| 2. Computer Organization | DMA | experimentally implemented | DMA controller path, tests, demos | bounded deterministic DMA model |
| 2. Computer Organization | performance evaluation | experimentally implemented | benchmark scripts, metrics output, reports | tiny deterministic workloads only |
| 2. Computer Organization | HDL/lab support | experimentally implemented | `src/hdl/`, `scripts/test_hdl.sh`, HDL tests | real modules/testbenches, not full-lab hardware flow |
| 3. Compilers | grammars, languages, automata, state machines | documented with worked examples | `docs/compiler/formal_foundations.md`, `examples/formal/` | theory/examples plus relation to frontend |
| 3. Compilers | trees, graphs, hash tables, traversal, closure algorithms | documented with worked examples | `docs/compiler/formal_foundations.md`, `examples/formal/scope_graph_and_hash_tables.md` | concepts explained; concrete structures also appear in code |
| 3. Compilers | handwritten lexer | fully implemented | `src/compiler/frontend/src/lexer.cpp`, `tests/unit/frontend_lexer_test.cpp` | production lexer path |
| 3. Compilers | Flex lexer | experimentally implemented | `src/compiler/experimental_parallel_parsing/src/flex_bison_lexer.l`, parser tests | comparison path only |
| 3. Compilers | handwritten parser | fully implemented | `src/compiler/frontend/src/parser.cpp`, `tests/unit/frontend_parser_test.cpp` | production parser path |
| 3. Compilers | Bison parser | experimentally implemented | `src/compiler/experimental_parallel_parsing/src/flex_bison_parser.y`, `tests/unit/experimental_parse_test.cpp` | experimental comparison path only |
| 3. Compilers | LL coverage | documented with worked examples | `docs/compiler/formal_foundations.md`, `examples/formal/grammar_worked_example.md` | production parser is LL-friendly recursive descent |
| 3. Compilers | LR coverage | experimentally implemented | `--mode bison-lr`, Flex/Bison sources, tests, docs | bounded LR comparison path only |
| 3. Compilers | AST | fully implemented | AST headers, parser tests, `nexusc ast` | real AST construction and printing |
| 3. Compilers | semantic analysis | fully implemented | `src/compiler/semantics/src/semantic_analyzer.cpp`, tests | real checker in build flow |
| 3. Compilers | type checking | fully implemented | semantic analyzer plus negative tests | bounded source-language type system only |
| 3. Compilers | semantic methodology docs | documented with worked examples | `docs/compiler/semantic_models.md` | documentation-backed theory |
| 3. Compilers | intermediate code / TAC / quadruple-style IR | fully implemented | `src/compiler/ir/`, `docs/compiler/ir.md`, IR tests | typed non-SSA three-address-style IR |
| 3. Compilers | code generation | fully implemented | `src/compiler/backend_mips/`, end-to-end CLI/tests | bounded MIPS backend only |
| 3. Compilers | instruction selection | fully implemented | IR-to-MIPS lowering in backend | simple direct lowering, not industrial instruction selection |
| 3. Compilers | register allocation | documented with worked examples | `docs/compiler/optimizations.md`, `docs/architecture/mips_isa.md` | no real allocator is implemented |
| 3. Compilers | introductory optimization | experimentally implemented | symbolic, unroll, affine, alias, interproc analyses/passes | several bounded optimization slices are real |
| 4. Computer Architecture | benchmarks and Amdahl | experimentally implemented | scripts, `docs/reports/amdahl_evaluation.md`, tests | tiny experiments only |
| 4. Computer Architecture | deeper pipelining concepts | documented with worked examples | `docs/microarchitecture/pipeline.md` | executable model remains 5-stage |
| 4. Computer Architecture | superscalar concepts | experimentally implemented | width-2 issue experiments, tests, docs | bounded issue-width sandbox only |
| 4. Computer Architecture | out-of-order concepts | documented with worked examples | `docs/microarchitecture/advanced_scheduling.md` | no full OOO core |
| 4. Computer Architecture | scoreboard scheduler | experimentally implemented | `src/sim/advanced/src/model.cpp`, `tests/unit/advanced_model_test.cpp`, CLI coverage | executable bounded dynamic-scheduling experiment |
| 4. Computer Architecture | Tomasulo / reservation-station concepts | documented with worked examples | `docs/microarchitecture/advanced_scheduling.md`, literature notes | no executable Tomasulo engine |
| 4. Computer Architecture | static scheduling | experimentally implemented | VLIW-lite scheduler, tests, docs | bounded list scheduling only |
| 4. Computer Architecture | VLIW concepts | experimentally implemented | advanced VLIW-lite traces and tests | bounded issue width 2 only |
| 4. Computer Architecture | branch prediction | fully implemented | pipeline and advanced predictor support, tests, docs | bounded static and 2-bit predictors |
| 4. Computer Architecture | speculative execution concepts | experimentally implemented | speculative flush accounting in advanced mode | no reorder-buffer recovery |
| 4. Computer Architecture | advanced memory/peripheral organization | experimentally implemented | cache, L1+L2, I/O, DMA, interrupt code/tests | bounded educational memory/peripheral layer |
| 4. Computer Architecture | multiprocessor basics | experimentally implemented | `src/sim/parallel/`, tests, docs | correctness-first multicore model |
| 4. Computer Architecture | coherence | experimentally implemented | `--coherence snoop|directory-lite`, tests, docs | lite models only |
| 4. Computer Architecture | consistency | experimentally implemented | `--consistency sc|weak-lite`, tests, docs | weak-lite is educational, not formal ISA-level |
| 4. Computer Architecture | synchronization | experimentally implemented | lock/barrier/atomic operations, tests, demos | bounded primitives only |
| 4. Computer Architecture | literature-study integration | documented with worked examples | `docs/literature/architecture_readings.md` | reading/report layer |
| 4. Computer Architecture | simulation and HDL support | experimentally implemented | simulator stack, HDL modules/testbenches | HDL is module-oriented, not a full architecture reproduction |
| 5. Advanced Compiler Topics | generalized parsing | documented with worked examples | `docs/compiler/generalized_and_parallel_parsing.md` | no executable GLR/Earley parser |
| 5. Advanced Compiler Topics | parallel parsing | experimentally implemented | `src/compiler/experimental_parallel_parsing/src/prototype.cpp`, tests, CLI | bounded top-level partitioning only |
| 5. Advanced Compiler Topics | advanced type-system topics | documented with worked examples | `docs/compiler/semantic_models.md` | broader type theory is not implemented in the checker |
| 5. Advanced Compiler Topics | optimization-oriented IR | fully implemented | `src/compiler/ir/`, `docs/compiler/ir.md` | non-SSA but real optimization substrate |
| 5. Advanced Compiler Topics | CFG and basic blocks | fully implemented | CFG code/tests, CLI | real control-flow analysis support |
| 5. Advanced Compiler Topics | dominators | fully implemented | dominator code/tests, `nexusc dom` | real implementation |
| 5. Advanced Compiler Topics | convergence and fixpoint | fully implemented | iterative liveness solver, tests, CLI output | real fixpoint iteration is present |
| 5. Advanced Compiler Topics | lattice-oriented theory | documented with worked examples | `docs/compiler/optimizations.md` | lattice framing is documented more than encoded formally |
| 5. Advanced Compiler Topics | iterative data-flow | fully implemented | `src/compiler/analysis/src/data_flow.cpp`, liveness tests | real iterative solver |
| 5. Advanced Compiler Topics | region-based or non-iterative data-flow | experimentally implemented | `src/compiler/analysis/src/region_flow.cpp`, docs, tests | bounded region-liveness prototype only |
| 5. Advanced Compiler Topics | expression optimization | experimentally implemented | symbolic simplification and interproc fold passes | bounded simplification only |
| 5. Advanced Compiler Topics | symbolic analysis | experimentally implemented | `src/compiler/analysis/src/symbolic.cpp`, tests, CLI | deterministic, non-SMT prototype |
| 5. Advanced Compiler Topics | concrete loop unrolling | experimentally implemented | `src/compiler/passes/src/loop_unroll.cpp`, tests | bounded counted-loop support only |
| 5. Advanced Compiler Topics | symbolic loop unrolling | experimentally implemented | `src/compiler/passes/src/loop_unroll.cpp`, tests | bounded factor-2 support only |
| 5. Advanced Compiler Topics | locality-oriented loop transformations | experimentally implemented | affine/locality analysis and strip-mining, tests, docs | small locality slice only |
| 5. Advanced Compiler Topics | polyhedral-inspired affine transformation | experimentally implemented | `src/compiler/analysis/src/affine_analysis.cpp`, `src/compiler/passes/src/affine_stripmine.cpp`, tests | not a full polyhedral optimizer |
| 5. Advanced Compiler Topics | pointer / alias analysis | experimentally implemented | `src/compiler/analysis/src/alias_analysis.cpp`, tests | bounded alias summary model |
| 5. Advanced Compiler Topics | interprocedural optimization | experimentally implemented | interprocedural summaries and constant-call folding, tests | small-function summary model only |
| 5. Advanced Compiler Topics | literature integration | documented with worked examples | `docs/literature/compiler_readings.md`, toolchain comparison report | literature/report support rather than a deeper implementation |
| 6. Parallel Systems and Parallel Programming | taxonomy of parallel architectures | documented with worked examples | `docs/parallel/overview.md`, `docs/parallel/interconnects.md` | taxonomy is explained, not simulated in full breadth |
| 6. Parallel Systems and Parallel Programming | SMT / multithreading concepts | documented with worked examples | `docs/parallel/overview.md` | no hardware-threaded core model |
| 6. Parallel Systems and Parallel Programming | shared-memory systems | experimentally implemented | `src/sim/parallel/`, tests, docs | bounded shared-memory multicore simulator |
| 6. Parallel Systems and Parallel Programming | distributed-memory systems | experimentally implemented | `benchmarks/mpi/mpi_bench.cpp`, integration tests, docs | benchmark/demo path, not a distributed simulator |
| 6. Parallel Systems and Parallel Programming | symmetric systems | experimentally implemented | bounded equal-core multicore model | small SMP-style model only |
| 6. Parallel Systems and Parallel Programming | asymmetric systems | documented with worked examples | `docs/parallel/overview.md` | no executable asymmetric-core model |
| 6. Parallel Systems and Parallel Programming | homogeneous systems | experimentally implemented | shared-core simulator, CPU-first benchmark paths | bounded homogeneous model |
| 6. Parallel Systems and Parallel Programming | heterogeneous systems | experimentally implemented | SIMD benchmark, optional CUDA demo, docs | GPU success is optional, not mandatory |
| 6. Parallel Systems and Parallel Programming | snooping coherence | experimentally implemented | `--coherence snoop`, coherence tests/docs | lite coherence model only |
| 6. Parallel Systems and Parallel Programming | directory-based coherence | experimentally implemented | `--coherence directory-lite`, coherence tests/docs | lite directory model only |
| 6. Parallel Systems and Parallel Programming | memory consistency | experimentally implemented | `--consistency sc|weak-lite`, tests/docs | weak-lite is pedagogical, not industrial semantics |
| 6. Parallel Systems and Parallel Programming | synchronization | experimentally implemented | lock/barrier/atomic support, tests, demos | bounded primitives only |
| 6. Parallel Systems and Parallel Programming | OpenMP | experimentally implemented | `benchmarks/openmp/openmp_bench.cpp`, tests, docs | tiny kernels only |
| 6. Parallel Systems and Parallel Programming | MPI | experimentally implemented | `benchmarks/mpi/mpi_bench.cpp`, tests, docs | tiny kernels only |
| 6. Parallel Systems and Parallel Programming | SIMD / vectorization | experimentally implemented | `benchmarks/simd/simd_bench.cpp`, tests | tiny checksum-style kernels |
| 6. Parallel Systems and Parallel Programming | GPU path | experimentally implemented | `benchmarks/gpu_optional/gpu_optional_bench.cu`, `parallel-bench`, tests/docs | optional feature gate; current CPU-only audit validated clean skip |
| 6. Parallel Systems and Parallel Programming | buses / switches / NoC | experimentally implemented | `docs/parallel/interconnects.md`, `tests/unit/interconnect_demo_test.cpp`, CLI | lite interconnect abstractions only |
| 6. Parallel Systems and Parallel Programming | simulation and/or HDL support for a parallel-system case study | experimentally implemented | bounded parallel simulator, tests, reports | satisfied through simulation; HDL remains generic CPU-side rather than parallel-specific |

## 9. OVER-CLAIM CHECK

No material over-claim remained in the maintained audit/report files after this pass. The project
now consistently presents course coverage as a mix of:

- fully implemented core paths
- experimentally implemented bounded extensions
- documented worked examples
- explicit out-of-scope boundaries

The main documentation drift found during the audit was the opposite problem: some older
phase-oriented reference docs understate later additions. Examples include `docs/compiler/formal_foundations.md`,
`docs/compiler/ir.md`, and `docs/architecture/data_representation.md`, which still carry earlier
phase wording even though later bounded features were added elsewhere in the repository.

## 10. FINAL VERDICT

Verdict: covers all six syllabi in bounded educational/research-grade form.

This verdict is justified only under explicit limitations:

- not every syllabus topic is fully implemented as executable code
- several advanced topics are present only as bounded experiments
- several theory-heavy topics are covered through documentation and worked examples rather than full
  implementations
- a small set of large industrial topics remains intentionally outside bounded scope

Within those limits, the repository now honestly spans all six syllabi with evidence-backed
coverage and a passing validated build/test surface.
