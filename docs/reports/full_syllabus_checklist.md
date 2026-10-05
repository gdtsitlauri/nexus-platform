# Full Syllabus Checklist (version 1.0.0)

Every topic of the six course outlines in `docs/course_outlines/` mapped to the code that implements
it and the automated test that checks it. Test names are CTest names (`ctest -R <name>`).

## Status Legend

- **implemented and tested**: executable code plus an automated check in `ctest`
- **implemented, toolchain-dependent**: executable code whose test runs only when an external
  toolchain is installed (OpenMP compiler, MPI, CUDA, clang); the test is skipped otherwise
- **notes**: topics that are not programs (history, reading lists, taxonomies); covered in `docs/`

No topic of the six outlines is left outside the project.

## 1. NEY221 Principles of Computer Operation

| Topic | Status | Code | Test |
| --- | --- | --- | --- |
| computer organization overview | implemented and tested | `src/sim/` (functional to pipelined models), `ARCHITECTURE.md` | `nexus_cross_model_differential` |
| MIPS assembly programming, registers, memory, stack, subroutines | implemented and tested | `src/mips/`, `src/sim/functional/`, `nexusc compile` | `nexus_mips_functional_test`, `nexus_phase6_golden` |
| accumulator ISA | implemented and tested | `src/compiler/isa_styles/` (`nexusc isa --style accumulator`) | `nexus_isa_styles` |
| stack ISA and Java bytecode (JVM-like) | implemented and tested | `src/compiler/isa_styles/` (`--style stack`: iload/istore, if_icmpXX, invokestatic, iaload) | `nexus_isa_styles` |
| general-purpose-register (load/store) ISA | implemented and tested | MIPS back end | `nexus_backend_mips_test` |
| IA-32 (register-memory, two-address) | implemented and tested | `src/compiler/isa_styles/` (`--style register-memory`: mov/add/imul/idiv/setcc, [ebp+d], [base+index*4], cdecl) | `nexus_isa_styles` |
| ISA trade-offs (code size, instruction count, memory traffic) | implemented and tested | `nexusc isa <file>` comparison table | `nexus_isa_styles` |
| signed/unsigned integers: sign-magnitude, one's and two's complement, excess-K, BCD | implemented and tested | `src/common/src/number_systems.cpp` (`mips-sim arith repr`) | `nexus_number_systems_test` |
| fixed point | implemented and tested | `src/common/src/fpu_lite.cpp` (Q8.8) | `nexus_fpu_lite_test` |
| floating point (IEEE-754 binary32) | implemented and tested | `soft_add/sub/mul/div`: round-to-nearest-even, subnormals, signed zero, Inf/NaN, bit-exact with hardware (`mips-sim arith float`) | `nexus_number_systems_test` (2M random pairs x 4 ops) |
| non-numeric data (characters, UTF-8) | implemented and tested | strict UTF-8 encoder/decoder (`mips-sim arith utf8`) | `nexus_number_systems_test` |
| adders (ripple carry, carry lookahead) | implemented and tested | `src/common/src/arithmetic.cpp`, `carry_lookahead_add`, `src/hdl/alu/nexus_adder.v` | `nexus_arithmetic_units_test`, `nexus_number_systems_test`, `nexus_hdl_adder` |
| multipliers (shift-add, Booth radix-2) | implemented and tested | `arithmetic.cpp`, `booth_multiply` (`mips-sim arith booth`), `nexus_iterative_multiplier.v` | `nexus_number_systems_test`, `nexus_hdl_multiplier` |
| dividers (restoring, non-restoring) | implemented and tested | `arithmetic.cpp`, `nonrestoring_divide` (`mips-sim arith divide`), `nexus_iterative_divider.v` | `nexus_number_systems_test`, `nexus_hdl_divider` |

## 2. EY321 Computer Organization

| Topic | Status | Code | Test |
| --- | --- | --- | --- |
| history of computing | notes | `docs/reports/history_of_computing_evolution.md` | `nexus_report_docs` |
| MIPS machine code | implemented and tested | MIPS32 encoder (`mips-sim encode`) | `nexus_hdl_pipeline_cosim` |
| datapath and control, micro-operations | implemented and tested | `src/sim/single_cycle/`, `src/sim/multi_cycle/` | `nexus_single_cycle_model_test`, `nexus_multi_cycle_model_test` |
| hardwired control | implemented and tested | single/multi-cycle control, `src/hdl/control/` | `nexus_multi_cycle_model_test`, `nexus_hdl_control` |
| microprogrammed control | implemented and tested | multi-cycle `--control microcode` | `nexus_multi_cycle_model_test` |
| pipelining, hazards, forwarding, stalls | implemented and tested | `src/sim/pipeline/` | `nexus_pipeline_model_test`, `nexus_cross_model_differential` |
| branch prediction | implemented and tested | static predictors (pipeline), 2-bit (advanced) | `nexus_advanced_predictor_test` |
| memory hierarchy, L1 + L2 caches | implemented and tested | `src/sim/memory/` | `nexus_memory_cache_test`, `nexus_pipeline_memory_system_test` |
| I/O, buses, interrupts, DMA | implemented and tested | `src/sim/io/` | `nexus_io_system_test` |
| performance evaluation (CPI, IPC, speedup) | implemented and tested | `--stats` on every model, `scripts/run_benchmarks.py` | `nexus_phase7_benchmarks` |
| HDL laboratory: ALU, register file, control, pipeline registers | implemented and tested | `src/hdl/` | `nexus_hdl_*` |
| complete CPU in HDL | implemented and tested | `src/hdl/cpu_pipeline/nexus_mips_pipeline.v`: synthesisable 5-stage MIPS (forwarding, load-use interlock, branch flush, mult/div) | `nexus_hdl_pipeline_cosim` (RTL vs simulator, plus Yosys synthesis) |

## 3. NEY613 Compilers

| Topic | Status | Code | Test |
| --- | --- | --- | --- |
| grammars, languages, automata | implemented and tested | `src/compiler/formal/`: regex -> Thompson NFA -> subset DFA -> minimal DFA (`nexusc regex`) | `nexus_formal_test` (vs `std::regex`) |
| hand-written lexer | implemented and tested | `src/compiler/frontend/src/lexer.cpp` | `nexus_frontend_lexer_test` |
| Flex lexer | implemented and tested | `experimental_parallel_parsing/src/flex_bison_lexer.l` | `nexus_experimental_parse_test` |
| recursive-descent (LL) parser | implemented and tested | `src/compiler/frontend/src/parser.cpp` | `nexus_frontend_parser_test` |
| LL(1): FIRST/FOLLOW, parse table, predictive parsing, left-recursion elimination, left factoring | implemented and tested | `nexusc grammar <g> first-follow / ll1 / transform` | `nexus_formal_test` |
| LR parsing: LR(0), SLR(1), canonical LR(1), LALR(1) tables and parsing | implemented and tested | `nexusc grammar <g> lr0 / slr / lr1 / lalr` | `nexus_formal_test` |
| Bison LR parser | implemented and tested | `flex_bison_parser.y` (`experimental-parse --mode bison-lr`) | `nexus_experimental_parse_test` |
| AST construction | implemented and tested | `nexusc ast` | `nexus_frontend_parser_test` |
| semantic analysis, type checking, symbol tables | implemented and tested | `src/compiler/semantics/` | `nexus_semantics_test` |
| attribute grammars | implemented and tested | S-attributed actions `{ $$ = $1 + $3 }` evaluated on LL and LR parse trees | `nexus_formal_test` |
| intermediate code: AST, three-address code, quadruples | implemented and tested | `src/compiler/ir/` (`nexusc ir`, `nexusc quads`) | `nexus_ir_lowering_test`, `nexus_coverage_cli` |
| code generation, instruction selection, stack frames, calls | implemented and tested | `src/compiler/backend_mips/` | `nexus_backend_mips_test`, `nexus_cross_model_differential` |
| register allocation | implemented and tested | linear scan (`--regalloc linear-scan`, `nexusc analysis regalloc`) | `nexus_cross_model_differential`, `nexus_hdl_pipeline_cosim` |
| introductory optimisation | implemented and tested | folding, unrolling, SCCP | `nexus_unroll_pass_test`, `nexus_ssa_test` |
| complete compiler (source to running machine code) | implemented and tested | `nexusc` + `mips-sim` + RTL core | `nexus_phase10_cli`, `nexus_hdl_pipeline_cosim` |

## 4. NEY606 Computer Architecture

| Topic | Status | Code | Test |
| --- | --- | --- | --- |
| performance, benchmarks, Amdahl's law | implemented and tested | `scripts/generate_amdahl_report.py`, `scripts/run_benchmarks.py` | `nexus_phase8_amdahl`, `nexus_phase7_benchmarks` |
| deeper pipelines | implemented and tested | `--pipeline-depth N` (redirect penalty grows with depth) | `nexus_tomasulo_test`, `nexus_coverage_cli` |
| superscalar issue | implemented and tested | `--issue-width 2` (in order), `--issue-width 1-4` (Tomasulo) | `nexus_advanced_model_test`, `nexus_tomasulo_test` |
| dynamic scheduling: scoreboard | implemented and tested | `--scheduler scoreboard` | `nexus_advanced_model_test` |
| dynamic scheduling: Tomasulo, reservation stations, register renaming, CDB | implemented and tested | `--scheduler tomasulo` | `nexus_tomasulo_test` |
| static scheduling, VLIW | implemented and tested | `--scheduler vliw-lite` | `nexus_advanced_model_test`, `nexus_phase8_golden` |
| branch prediction, speculation, reorder buffer, return-address stack | implemented and tested | 2-bit predictor, ROB with in-order commit, RAS | `nexus_tomasulo_test` |
| memory and peripherals | implemented and tested | caches, I/O, DMA | `nexus_pipeline_memory_system_test`, `nexus_io_system_test` |
| multiprocessors: coherence, consistency, synchronisation | implemented and tested | `src/sim/parallel/` | `nexus_coherence_test`, `nexus_consistency_test`, `nexus_parallel_model_test` |
| literature study | notes | `docs/literature/architecture_readings.md` | `nexus_literature_docs` |

## 5. NEY709 Advanced Compiler Topics

| Topic | Status | Code | Test |
| --- | --- | --- | --- |
| generalized parsing (ambiguous grammars) | implemented and tested | Earley parser with parse-tree counting (`nexusc grammar <g> earley`) | `nexus_formal_test` |
| parallel parsing | implemented and tested | `experimental-parse --mode parallel` | `nexus_experimental_parse_test` |
| type systems | implemented and tested | Hindley-Milner (Algorithm W) with let-polymorphism and occurs check (`nexusc infer`) | `nexus_type_inference_test` |
| intermediate representations, SSA | implemented and tested | `src/compiler/analysis/src/ssa.cpp`: dominance frontiers, phi placement, renaming (`analysis ssa`) | `nexus_ssa_test` |
| CFG, basic blocks, dominators | implemented and tested | `cfg.cpp`, `dominators.cpp` | `nexus_cfg_analysis_test` |
| lattices and convergence | implemented and tested | SCCP over the constant lattice (`analysis sccp`) | `nexus_ssa_test` |
| iterative data-flow analysis | implemented and tested | `data_flow.cpp`, `liveness.cpp` | `nexus_cfg_analysis_test` |
| region-based data-flow analysis | implemented and tested | `region_flow.cpp` | `nexus_phase10_cli` |
| expression optimisation, symbolic analysis | implemented and tested | `symbolic.cpp` | `nexus_symbolic_analysis_test` |
| loop unrolling | implemented and tested | `loop_unroll.cpp` | `nexus_unroll_pass_test` |
| polyhedral model, linear inequalities | implemented and tested | `src/compiler/polyhedral/`: Fourier-Motzkin dependence tests, distance/direction vectors, unimodular transforms, FM loop-bound generation (`nexusc poly`) | `nexus_polyhedral_test` |
| affine analysis and strip-mining | implemented and tested | `affine_analysis.cpp`, `affine_stripmine.cpp` | `nexus_affine_analysis_test` |
| pointer and alias analysis | implemented and tested | `alias_analysis.cpp` | `nexus_alias_analysis_test` |
| interprocedural analysis | implemented and tested | `interprocedural.cpp`, `interprocedural_pass.cpp` | `nexus_interproc_test` |
| literature study | notes | `docs/literature/compiler_readings.md` | `nexus_literature_docs` |

## 6. NEY704 Parallel Systems and Parallel Programming

| Topic | Status | Code | Test |
| --- | --- | --- | --- |
| taxonomy of parallel architectures (Flynn) | notes | `docs/parallel/overview.md` | `nexus_report_docs` |
| multithreading, simultaneous multithreading | implemented and tested | 2-way SMT on the Tomasulo core (`--smt <thread1.s>`, ICOUNT fetch) | `nexus_tomasulo_test` |
| shared-memory multiprocessors | implemented and tested | `--mode parallel`, SPMD `worker` entry for any core count | `nexus_parallel_model_test`, `nexus_manycore` |
| distributed memory, message passing (MPI) | implemented, toolchain-dependent | `benchmarks/mpi/mpi_bench.cpp` | `nexus_mpi_benchmark` (needs `mpicxx`; passed on Colab, `results/colab/`) |
| symmetric and asymmetric multiprocessors | implemented and tested | `--core-cpi 1,1,4,4` (big/little cores) | `nexus_manycore` |
| snooping and directory coherence | implemented and tested | `--coherence snoop / directory-lite` | `nexus_coherence_test` |
| memory consistency, synchronisation | implemented and tested | `--consistency sc / weak-lite`, locks, barriers, atomics | `nexus_consistency_test`, `nexus_phase10_golden` |
| OpenMP | implemented, toolchain-dependent | `benchmarks/openmp/openmp_bench.cpp` | `nexus_openmp_benchmark` (needs an OpenMP compiler; passed on Colab) |
| vector and SIMD programming | implemented and tested | `benchmarks/simd/simd_bench.cpp` | `nexus_simd_benchmark` |
| GPUs and SIMT execution | implemented and tested | `src/sim/simt/`: warps, reconvergence stack, SIMD efficiency, memory coalescing (`--mode simt`) | `nexus_simt_test` |
| GPU programming on a device (CUDA) | implemented, toolchain-dependent | `benchmarks/gpu_optional/gpu_optional_bench.cu` | `nexus_gpu_optional` (needs `nvcc`; passed on a Colab Tesla T4, GPU checksums equal the CPU) |
| interconnects: bus, switch, ring, 2-D mesh NoC | implemented and tested | `--interconnect bus / switch / ring / mesh / noc-lite` | `nexus_interconnect_test`, `nexus_manycore` |
| manycore (up to 64 cores) | implemented and tested | SPMD reduction on 1-64 cores | `nexus_manycore` |
| literature study | notes | `docs/literature/parallel_readings.md` | `nexus_literature_docs` |
