# Changelog

## [0.11.0-phase11] - 2026-04-22

- Added real Verilog HDL modules for the ALU, adder, iterative multiplier, iterative divider, register file, control unit, and pipeline register
- Added deterministic HDL testbenches plus `hdl-test` and `scripts/test_hdl.sh`
- Added an optional CUDA vector-add demo behind `-DNEXUS_ENABLE_CUDA=ON`
- Extended `parallel-bench` with optional GPU run/skip reporting
- Completed literature notes and final reports, including the conservative final completeness audit
- Added final documentation/report validation tests

## [0.10.0-phase10] - 2026-04-21

- Added `src/sim/parallel` with a deterministic bounded multicore simulator mode
- Implemented coherence-lite support with `snoop` and `directory-lite` options
- Implemented consistency-lite support with `sc` and `weak-lite` plus store-buffer accounting
- Added lock, barrier, and atomic-fetch-increment synchronization primitives for tiny shared-memory demos
- Added OpenMP, MPI, and SIMD benchmark binaries plus the `parallel-bench` wrapper for tiny CSV/markdown summaries
- Added bounded `bus`, `switch`, and `noc-lite` interconnect comparisons, Phase 10 tests, and parallel documentation/report updates

## [0.9.0-phase9] - 2026-04-21

- Added an experimental parallel top-level parsing prototype under `src/compiler/experimental_parallel_parsing`
- Added symbolic IR analysis and a bounded region-based liveness prototype under `src/compiler/analysis`
- Added bounded concrete loop unrolling, symbolic factor-2 loop unrolling, and interprocedural constant folding
- Added bounded alias-analysis and interprocedural-summary foundations with CLI exposure through `nexusc opt`
- Added tiny Phase 9 source examples, focused unit/integration tests, and a local `clang` toolchain-comparison workflow
- Updated compiler docs, course mapping, checklist, and validation reporting for Phase 9

## [0.8.0-phase8] - 2026-04-21

- Added `src/sim/advanced` with a deterministic advanced-architecture sandbox
- Implemented selectable advanced-mode issue-width experiments for width 1 and width 2
- Implemented bounded VLIW-lite static scheduling for reorderable basic blocks
- Implemented advanced branch-predictor experiments with `static-not-taken` and `2bit`
- Added speculative flush-cycle accounting, slot-utilization reporting, and advanced-mode traces
- Added tiny Phase 8 experiment and Amdahl-evaluation scripts plus focused unit/integration/golden tests

## [0.6.0-phase6] - 2026-04-21

- Added a real 5-stage IF/ID/EX/MEM/WB pipeline simulator
- Implemented explicit pipeline-register state, RAW hazard handling, EX-stage forwarding, and load-use stalls
- Implemented control flushes plus bounded static branch prediction with deterministic misprediction handling
- Extended `mips-sim` with `--mode pipeline`, `--trace`, `--timeline`, and `--predictor`
- Added pipeline unit, integration, and golden-output coverage for traces and timeline output

## [0.5.0-phase5] - 2026-04-21

- Implemented arithmetic-unit helpers for ripple/native adders, ALU operations, shift-add multiply, and restoring division
- Added a real single-cycle educational CPU model with explicit hardwired control decoding
- Added a real multi-cycle educational CPU model with hardwired and microprogrammed control plans
- Extended `mips-sim` with `--mode functional`, `--mode single-cycle`, and `--mode multi-cycle`
- Added deterministic instruction/cycle summaries plus stable trace output for single-cycle and multi-cycle execution
- Added Phase 5 unit, integration, comparison, and golden-output coverage

## [0.1.0-phase1] - 2026-04-21

- Bootstrapped the Nexus repository layout for Phase 1
- Added a modular CMake/Ninja build with `build/bin` runtime output
- Added stub CLI targets: `nexusc` and `mips-sim`
- Added `nexus_common` shared utility code and smoke tests
- Added required documentation skeletons, scripts, and syllabus audit scaffolding
- Verified `cmake -S . -B build -G Ninja`, `cmake --build build`, and `ctest --test-dir build --output-on-failure`

## [0.2.0-phase2] - 2026-04-21

- Implemented a handwritten NexusLang lexer with line/column diagnostics
- Implemented a recursive-descent parser, AST, and AST pretty-printer
- Implemented bounded semantic analysis with scopes, symbol tables, type checking, and return checks
- Extended `nexusc` with `lex`, `parse`, `ast`, and `check` frontend commands
- Added formal foundations documentation and worked examples
- Added Phase 2 unit and CLI integration tests plus example source files

## [0.3.0-phase3] - 2026-04-21

- Implemented a typed three-address-style IR for functions, locals, temporaries, and blocks
- Implemented AST-to-IR lowering for declarations, expressions, control flow, arrays, calls, and returns
- Added deterministic textual dumps for IR, CFGs, dominators, and liveness
- Implemented basic block formation, CFG construction, dominator analysis, and iterative data-flow
- Extended `nexusc` with `ir`, `cfg`, `dom`, and `analysis liveness`
- Added Phase 3 lowering, CFG, dominator, liveness, and CLI integration tests

## [0.4.0-phase4] - 2026-04-21

- Implemented a bounded educational MIPS subset with documented registers, calling convention, and memory model
- Implemented stack-based IR-to-MIPS lowering for arithmetic, control flow, calls, locals, and arrays
- Added deterministic textual assembly emission and an internal assembly loader with label resolution
- Implemented a functional MIPS interpreter with register file, stack memory, `jal`/`jr`, `lw`/`sw`, and optional trace mode
- Extended `nexusc` with `compile` and `mips-sim` with `run --mode functional [--trace]`
- Added backend, loader, simulator, CLI integration, and golden-output tests
