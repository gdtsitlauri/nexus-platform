# Full Syllabus Checklist

This checklist tracks the strongest currently supported evidence for each major course topic in the
final Nexus repository state.

## Audit Inputs

This checklist was cross-checked against:

- repository contract files: `README.md`, `ROADMAP.md`, `STATUS.md`, `ARCHITECTURE.md`, `CHANGELOG.md`
- code, tests, examples, scripts, tools, benchmarks, and CMake targets
- six syllabus PDFs available during the 2026-04-22 audit

Five PDFs were present at the repo-local paths listed under `data/`. The sixth syllabus was not
present at `/mnt/data/...`, but an identically named repo-local copy under `data/` was available
and was used as the formal source for Course 6.

## Status Legend

- fully implemented
- experimentally implemented
- documented with worked examples
- still outside bounded scope
- implemented
- pending
- legacy audit term retained for compatibility: pending

## 1. Principles of Computer Operation

| Topic | Status | Evidence |
| --- | --- | --- |
| general computer organization | experimentally implemented | `ARCHITECTURE.md`, `src/sim/single_cycle/`, `src/sim/multi_cycle/`, `src/sim/pipeline/` |
| MIPS assembly programming | fully implemented | `docs/architecture/mips_isa.md`, `nexusc compile`, `mips-sim run` |
| registers, memory, stack, subroutines | fully implemented | stack frames, `jal`/`jr`, `lw`/`sw`, simulator tests |
| accumulator-based ISA concepts | documented with worked examples | `docs/architecture/isa_comparison.md` |
| stack-based ISA concepts | documented with worked examples | `docs/architecture/isa_comparison.md` |
| general-purpose-register ISA concepts | fully implemented | MIPS backend and simulator |
| IA-32 concepts | documented with worked examples | `docs/architecture/isa_comparison.md` |
| Java bytecode concepts | documented with worked examples | `docs/architecture/isa_comparison.md` |
| signed integer representation | experimentally implemented | `docs/architecture/data_representation.md`, arithmetic helpers/tests |
| unsigned integer representation | experimentally implemented | `docs/architecture/data_representation.md`, unsigned compare support |
| fixed-point concepts | experimentally implemented | `src/common/src/fpu_lite.cpp`, `tests/unit/fpu_lite_test.cpp` |
| floating-point concepts | experimentally implemented | `src/common/src/fpu_lite.cpp`, `mips-sim fp-demo`, `tests/unit/fpu_lite_test.cpp` |
| non-numeric data representation | documented with worked examples | `docs/architecture/data_representation.md` |
| arithmetic algorithms and hardware overview | experimentally implemented | arithmetic helpers, HDL arithmetic modules, docs |
| adders | experimentally implemented | `src/common/src/arithmetic.cpp`, `src/hdl/alu/nexus_adder.v`, tests |
| multipliers | experimentally implemented | shift-add helpers, `src/hdl/alu/nexus_iterative_multiplier.v`, tests |
| dividers | experimentally implemented | restoring divide helpers, `src/hdl/alu/nexus_iterative_divider.v`, tests |
| FPU-lite demo path | experimentally implemented | `src/common/include/nexus/common/fpu_lite.hpp`, `src/common/src/fpu_lite.cpp`, `tests/unit/fpu_lite_test.cpp` |

## 2. Computer Organization

| Topic | Status | Evidence |
| --- | --- | --- |
| modern computer organization | experimentally implemented | simulator stack plus architecture docs |
| history of computing evolution | documented with worked examples | `docs/reports/history_of_computing_evolution.md` |
| ISA and arithmetic foundations review | documented with worked examples | `docs/architecture/mips_isa.md`, `docs/architecture/isa_comparison.md` |
| MIPS machine-level instruction understanding | fully implemented | backend, loader, interpreter, docs |
| CPU datapath and control | experimentally implemented | single-cycle/multi-cycle/pipeline code plus docs |
| micro-operations | experimentally implemented | multi-cycle sequencing and trace output |
| single-cycle execution model | fully implemented | `src/sim/single_cycle/`, tests |
| multi-cycle execution model | fully implemented | `src/sim/multi_cycle/`, tests |
| hardwired control | fully implemented | control decode in single-cycle and multi-cycle modes; HDL control unit |
| microprogrammed control | fully implemented | multi-cycle microcode control mode |
| pipelining | fully implemented | `src/sim/pipeline/`, tests, docs |
| hazards | fully implemented | `docs/microarchitecture/hazards.md`, pipeline tests |
| forwarding/bypassing | fully implemented | pipeline forwarding logic and tests |
| stalls/freezes | fully implemented | load-use stall logic and timeline markers |
| basic branch prediction | fully implemented | static pipeline predictors and tests |
| memory hierarchy | experimentally implemented | `src/sim/memory/`, cache docs, cache tests |
| L1+L2 hierarchy | experimentally implemented | `src/sim/memory/`, `tests/unit/memory_cache_test.cpp`, `tests/unit/pipeline_memory_system_test.cpp`, CLI coverage |
| I/O subsystems | experimentally implemented | `src/sim/io/`, tests, demos |
| buses | experimentally implemented | `src/sim/parallel/`, `docs/parallel/interconnects.md` |
| interrupts | experimentally implemented | timer interrupt path, tests, demos |
| DMA | experimentally implemented | DMA controller path, tests, demos |
| performance evaluation techniques | experimentally implemented | benchmark and metrics scripts, reports |
| HDL-oriented laboratory support | experimentally implemented | `src/hdl/`, `scripts/test_hdl.sh`, HDL testbenches |
| HDL CPU slice | experimentally implemented | `src/hdl/cpu_slice/nexus_cpu_slice.v`, `src/hdl/cpu_slice/cpu_slice_tb.v`, HDL tests |

## 3. Compilers

| Topic | Status | Evidence |
| --- | --- | --- |
| grammars, languages, automata, and state machines | documented with worked examples | `docs/compiler/formal_foundations.md`, `examples/formal/` |
| trees, graphs, hash tables, traversal and closure algorithms | documented with worked examples | `docs/compiler/formal_foundations.md`, examples |
| handwritten lexical analysis | fully implemented | lexer code and tests |
| Flex-based lexical analysis | experimentally implemented | `src/compiler/experimental_parallel_parsing/src/flex_bison_lexer.l`, experimental parse tests |
| manual syntax analysis where feasible | fully implemented | parser code and tests |
| Bison-based syntax analysis | experimentally implemented | `src/compiler/experimental_parallel_parsing/src/flex_bison_parser.y`, `nexusc experimental-parse --mode bison-lr` |
| LL parsing concepts | documented with worked examples | `docs/compiler/formal_foundations.md` |
| LR parsing concepts | experimentally implemented | experimental `bison-lr` path, docs, tests |
| AST construction | fully implemented | AST headers, parser tests, `nexusc ast` |
| source-language functions | fully implemented | parser, semantics, IR lowering, code generation, examples |
| source-language conditionals | fully implemented | parser, AST, IR lowering, simulator-backed examples |
| source-language loops | fully implemented | parser, AST, IR lowering, optimization tests |
| source-language recursion | fully implemented | `examples/source_lang/factorial.nx`, end-to-end compile/run flow |
| source-language arrays | fully implemented | frontend type system, IR lowering, backend support, examples |
| diagnostics with line and column support | fully implemented | lexer/parser/semantic diagnostics and negative tests |
| semantic analysis | fully implemented | semantic analyzer, tests |
| type checking | fully implemented | semantic analyzer, negative tests |
| symbol tables | fully implemented | nested scope maps |
| attribute-inspired semantic methodology | documented with worked examples | `docs/compiler/semantic_models.md` |
| AST-oriented intermediate code | fully implemented | IR lowering, tests |
| quadruple or three-address-code-like IR | fully implemented | `src/compiler/ir/`, `docs/compiler/ir.md` |
| final code generation | fully implemented | `src/compiler/backend_mips/`, end-to-end tests |
| instruction selection | fully implemented | bounded IR-to-MIPS lowering |
| register allocation | documented with worked examples | `docs/compiler/optimizations.md`, `docs/architecture/mips_isa.md` |
| stack-frame layout | fully implemented | `docs/architecture/mips_isa.md`, backend lowering, call tests |
| function calls and returns | fully implemented | `jal`/`jr` support, backend lowering, functional simulator tests |
| functional ISA execution | fully implemented | `src/sim/functional/`, tests, `mips-sim --mode functional` |
| introductory optimization | experimentally implemented | symbolic simplification, loop unrolling, affine strip-mining, interprocedural folding |
| complete educational compiler integration | fully implemented | `nexusc`, `mips-sim`, end-to-end tests |

## 4. Computer Architecture

| Topic | Status | Evidence |
| --- | --- | --- |
| performance evaluation with benchmarks | experimentally implemented | benchmark scripts and reports |
| Amdahl's law | experimentally implemented | Phase 8 scripts and `docs/reports/amdahl_evaluation.md` |
| branch miss-rate / predictor comparison | experimentally implemented | advanced predictor tests, stats output, `docs/microarchitecture/branch_prediction.md` |
| deeper pipelining | documented with worked examples | `docs/microarchitecture/pipeline.md` |
| superscalar concepts | experimentally implemented | width-2 issue experiments |
| out-of-order execution concepts | documented with worked examples | `docs/microarchitecture/advanced_scheduling.md` |
| scoreboard concepts | experimentally implemented | `src/sim/advanced/src/model.cpp`, `tests/unit/advanced_model_test.cpp`, CLI coverage |
| reservation-station or Tomasulo-lite concepts | documented with worked examples | `docs/microarchitecture/advanced_scheduling.md` |
| static scheduling | experimentally implemented | Phase 8 VLIW-lite scheduling |
| VLIW concepts | experimentally implemented | advanced-mode VLIW-lite traces and docs |
| branch prediction | fully implemented | pipeline and advanced predictor support |
| speculative execution concepts | experimentally implemented | advanced-mode speculative flush accounting |
| advanced memory/peripheral organization | experimentally implemented | bounded cache, L1+L2, I/O, interrupt, DMA models |
| multiprocessors and multicomputers introduction | experimentally implemented | `src/sim/parallel/`, docs |
| cache coherence | experimentally implemented | `--coherence snoop|directory-lite`, tests |
| memory consistency | experimentally implemented | `--consistency sc|weak-lite`, tests |
| synchronization | experimentally implemented | lock/barrier/atomic support and tests |
| literature-study integration | documented with worked examples | `docs/literature/architecture_readings.md` |
| architecture-oriented simulation or HDL implementation | experimentally implemented | simulator stack plus HDL modules/testbenches |
| HDL ALU module | experimentally implemented | `src/hdl/alu/nexus_alu.v`, `src/hdl/alu/alu_tb.v` |
| HDL adder module | experimentally implemented | `src/hdl/alu/nexus_adder.v`, `src/hdl/alu/adder_tb.v` |
| HDL register-file module | experimentally implemented | `src/hdl/register_file/nexus_register_file.v`, testbench |
| HDL multiplier module | experimentally implemented | `src/hdl/alu/nexus_iterative_multiplier.v`, testbench |
| HDL divider module | experimentally implemented | `src/hdl/alu/nexus_iterative_divider.v`, testbench |
| HDL control-unit module | experimentally implemented | `src/hdl/control/nexus_control_unit.v`, testbench |
| HDL pipeline-register module | experimentally implemented | `src/hdl/pipeline_regs/nexus_pipeline_reg.v`, testbench |
| HDL CPU slice | experimentally implemented | `src/hdl/cpu_slice/nexus_cpu_slice.v`, `tests/integration/CMakeLists.txt` |
| full synthesizable HDL CPU | still outside bounded scope | bounded Phase 11 scope stops at validated modules and a CPU slice |

## 5. Advanced Compiler Topics

| Topic | Status | Evidence |
| --- | --- | --- |
| generalized parsing | documented with worked examples | `docs/compiler/generalized_and_parallel_parsing.md` |
| parallel parsing | experimentally implemented | `src/compiler/experimental_parallel_parsing/`, tests |
| Flex/Bison LR parser path | experimentally implemented | `src/compiler/experimental_parallel_parsing/`, `tests/unit/experimental_parse_test.cpp`, CLI coverage |
| advanced type-system topics | documented with worked examples | `docs/compiler/semantic_models.md` |
| intermediate representations for optimization | fully implemented | IR docs and code |
| CFGs and basic blocks | fully implemented | CFG code and tests |
| dominators | fully implemented | dominator code and tests |
| convergence points and lattices | documented with worked examples | `docs/compiler/optimizations.md` |
| iterative data-flow analysis | fully implemented | baseline solver and liveness |
| region-based or non-iterative data-flow analysis | experimentally implemented | `src/compiler/analysis/src/region_flow.cpp` |
| expression optimization | experimentally implemented | symbolic simplification and constant-call folding |
| symbolic analysis | experimentally implemented | `src/compiler/analysis/src/symbolic.cpp` |
| concrete loop unrolling | experimentally implemented | `src/compiler/passes/src/loop_unroll.cpp` |
| symbolic loop unrolling | experimentally implemented | `src/compiler/passes/src/loop_unroll.cpp` |
| affine analysis and strip-mining | experimentally implemented | `src/compiler/analysis/src/affine_analysis.cpp`, `src/compiler/passes/src/affine_stripmine.cpp`, focused tests |
| locality-oriented loop transformations | experimentally implemented | affine/locality analysis plus strip-mining notes and tests |
| polyhedral-inspired affine loop transformations | experimentally implemented | bounded affine analysis and strip-mining, `docs/compiler/loop_optimizations.md` |
| pointer and alias analysis | experimentally implemented | bounded alias analysis, tests |
| interprocedural optimization | experimentally implemented | summaries plus constant-call folding |
| literature-study integration | documented with worked examples | `docs/literature/compiler_readings.md` |
| ambiguity-supporting generalized parser (GLR/Earley class) | still outside bounded scope | theory is documented, but no executable generalized parser is shipped |
| industrial SSA, register allocation, and polyhedral optimizer depth | still outside bounded scope | final repository remains bounded and non-SSA |

## 6. Parallel Systems and Parallel Programming

| Topic | Status | Evidence |
| --- | --- | --- |
| taxonomy of parallel architectures | documented with worked examples | `docs/parallel/overview.md`, `docs/parallel/interconnects.md` |
| multithreaded and simultaneous multithreaded systems | documented with worked examples | `docs/parallel/overview.md` |
| shared-memory systems | experimentally implemented | `src/sim/parallel/`, tests |
| distributed-memory systems | experimentally implemented | `benchmarks/mpi/mpi_bench.cpp`, docs |
| symmetric multiprocessors | experimentally implemented | bounded 2-4 core model |
| asymmetric multiprocessors | documented with worked examples | `docs/parallel/overview.md` |
| homogeneous systems | experimentally implemented | shared-core educational model |
| heterogeneous systems | experimentally implemented | `benchmarks/simd/simd_bench.cpp`, `benchmarks/gpu_optional/gpu_optional_bench.cu`, `docs/parallel/overview.md`, `docs/parallel/openmp_mpi_gpu.md` |
| snooping coherence models | experimentally implemented | `--coherence snoop`, tests |
| directory-based coherence models | experimentally implemented | `--coherence directory-lite`, tests |
| memory consistency models | experimentally implemented | `--consistency sc|weak-lite`, tests |
| synchronization mechanisms in hardware and software | experimentally implemented | lock/barrier/atomic support, OpenMP/MPI examples |
| OpenMP programming | experimentally implemented | `openmp-bench`, integration tests |
| MPI programming | experimentally implemented | `mpi-bench`, integration tests |
| vector and SIMD programming | experimentally implemented | `simd-bench`, integration tests |
| GPU computing and programming | experimentally implemented | optional `gpu-bench`, `docs/parallel/openmp_mpi_gpu.md`, clean skip behavior |
| buses, switches, and networks-on-chip | experimentally implemented | interconnect demos and tests |
| simulation and HDL support for a parallel-system case study | experimentally implemented | bounded parallel simulator with focused tests; HDL support remains generic CPU-side rather than parallel-specific |
| literature notes and reading summaries | documented with worked examples | `docs/literature/*.md` |
| mandatory device-backed GPU execution | still outside bounded scope | CUDA is intentionally optional and not required for repository success |
| large manycore or full-coherence research platform | still outside bounded scope | final parallel layer remains bounded and educational |

## Update Policy

Status remains conservative and evidence-linked. Topics are only moved above `still outside bounded
scope` when the repository contains matching code, tests, documentation, or worked examples.
