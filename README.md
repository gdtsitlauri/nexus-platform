# NEXUS

**Can one codebase cover six computer-systems courses with programs that run and are checked, not just notes?**

NEXUS (version 1.0.0) is a C++20 / Verilog project. It contains:
- a compiler for a small language (NexusLang) down to MIPS;
- execution models from a functional interpreter up to an out-of-order superscalar core;
- multicore, network-on-chip and GPU (SIMT) models;
- a synthesisable pipelined MIPS CPU in Verilog;
- executable tools for the theory parts: automata, grammars, type inference, SSA, polyhedral loop
  transformations, computer arithmetic.

Every topic of the six University of Thessaly course outlines in `docs/course_outlines/` maps to code
and an automated test (`docs/reports/full_syllabus_checklist.md`). The only exceptions are reading
lists and history.

| course | what runs |
| --- | --- |
| NEY221 Principles of Computer Operation | MIPS toolchain; stack (JVM-like), accumulator and IA-32-like back ends with interpreters; bit-exact IEEE-754 soft float; integer encodings; Booth, non-restoring division, carry lookahead; UTF-8 |
| EY321 Computer Organization | single-cycle and multi-cycle CPUs (hardwired and microprogrammed control); 5-stage pipeline with hazards, forwarding, prediction; L1/L2 caches, I/O, interrupts, DMA; Verilog datapath units and a full pipelined CPU |
| NEY613 Compilers | hand-written lexer and parser, Flex/Bison path, semantic analysis, three-address code and quadruples, MIPS code generation, linear-scan register allocation; regex to minimal DFA; LL(1), LR(0), SLR(1), LR(1), LALR(1) with attribute evaluation |
| NEY606 Computer Architecture | superscalar, VLIW, scoreboard and Tomasulo scheduling with reorder buffer, register renaming, speculation and return-address stack; deeper pipelines; Amdahl studies; coherence and consistency |
| NEY709 Advanced Compiler Topics | Earley (ambiguous grammars) and parallel parsing; Hindley-Milner inference; SSA and SCCP; iterative and region-based data flow; symbolic, alias and interprocedural analysis; loop unrolling; polyhedral dependence analysis and transformations |
| NEY704 Parallel Systems | SMT; multicore with snooping/directory coherence, SC/weak consistency, locks, barriers, atomics; bus, switch, ring and 2-D mesh networks up to 64 cores; asymmetric cores; SIMT GPU model; OpenMP, MPI, SIMD and CUDA programs |

## Main results

All numbers come from `ctest` and the commands in `docs/`; the full record is `docs/reports/validation_report.md`.

1. **Every timing model reproduces the reference result.** All 12 test programs, compiled with and
   without register allocation, give the functional interpreter's exit code on 12 CPU models: single
   and multi-cycle, the pipeline with and without caches, scoreboard, Tomasulo, dual issue and
   multicore. That is 288 runs.
2. **The Verilog CPU matches the simulator instruction for instruction.** The compiler's output runs
   on the 5-stage RTL core (`nexusc` -> `mips-sim encode` -> Icarus Verilog). It gives the same exit
   code and the same retired-instruction count as the simulator in all 16 runs, and Yosys synthesises
   the core.
3. **The arithmetic is exact.** The software IEEE-754 add/sub/mul/div is bit-identical to the
   hardware FPU on 2,000,000 random operand pairs, including subnormals, infinities, NaNs and rounding
   ties.
4. **The theory tools agree with independent references.**
   - The regex engine agrees with `std::regex` and reproduces the Dragon-book results: (a|b)*abb
     gives a 4-state minimal DFA; grammar 4.49 has 10 LALR(1) states against 14 LR(1).
   - The Earley parser counts Catalan-number parse trees.
   - Hindley-Milner infers the principal types and rejects `fun x -> x x` by the occurs check.
   - Of 600 random polyhedral transformations, all 341 judged legal reproduce the original results
     exactly, and Fourier-Motzkin never misses a dependence.
5. **The architecture trade-offs come out as in the textbooks.**
   - Linear-scan allocation removes 21-58% of executed instructions.
   - On matrix multiplication, a 4-wide Tomasulo core with a 2-bit predictor reaches IPC 1.76,
     against 0.95 for the in-order core.
   - JVM-like stack code is the densest; MIPS with allocation moves 13x less data than stack-frame
     MIPS.
   - Divergent GPU kernels drop to 14% SIMD efficiency; strided accesses need 32 memory transactions
     per warp request against 1.
6. **OpenMP, MPI and CUDA run on real toolchains.** On Google Colab (Tesla T4) the complete suite
   of 68 tests ran, including the OpenMP and MPI benchmarks and the CUDA demo, whose GPU checksums
   match the CPU (`results/colab/`). The run also exposed one portability bug, now fixed: GCC named
   type variables in a different order than Clang.
7. **Two defects in the earlier version were found and fixed** (`STATUS.md`):
   - the cached pipeline returned wrong results for programs with calls;
   - passing a row of a 2-D array used the wrong address.

## Limitations (reported as such)

- The simulators are teaching models. The Tomasulo core is trace-driven; the SIMT model has no thread
  blocks, shared memory or latency hiding; cycle counts are model metrics, not measurements of real
  hardware.
- The Verilog CPU is verified in simulation and synthesised generically. It has not been placed and
  routed on an FPGA. Multiply and divide are single-cycle combinational blocks.
- NexusLang has `int`, `bool` and arrays only (no floating point, pointers or globals). The IA-32 and
  JVM back ends are faithful subsets, not complete instruction sets.
- The polyhedral tool handles perfect loop nests with concrete parameter values and does not generate
  tiled code.
- The OpenMP, MPI and CUDA programs need their toolchains. They were skipped on the Windows host and run
  on Google Colab (Tesla T4, CUDA 13.0, GCC 13.3, OpenMPI) instead: all their tests passed there
  (`results/colab/`). These are small correctness demos, not performance studies.

## Folder map

```
nexus-platform/
  README.md, README_GR.md, LICENSE (MIT), CITATION.cff, CHANGELOG.md, STATUS.md, ARCHITECTURE.md
  src/
    compiler/   frontend, semantics, ir, analysis (incl. SSA/SCCP), passes, backend_mips (incl. register
                allocation), isa_styles, formal (automata, grammars), types (Hindley-Milner), polyhedral,
                experimental_parallel_parsing (parallel + Flex/Bison)
    sim/        functional, single_cycle, multi_cycle, pipeline, memory, io, advanced (incl. Tomasulo/SMT),
                parallel (coherence, NoC, manycore), simt (GPU), metrics
    mips/       ISA tables, assembly printer, loader, MIPS32 encoder
    hdl/        Verilog units, CPU slice, cpu_pipeline (5-stage CPU + testbench)
    common/     arithmetic, FPU-lite, IEEE-754 soft float, number systems
    nexusc_main.cpp, mips_sim_main.cpp
  benchmarks/   OpenMP, MPI, SIMD, optional CUDA
  examples/     source_lang/ (NexusLang), grammars/, loops/, gpu/, parallel/, formal/
  tests/        unit/, integration/, golden/, programs/ (shared test programs)
  notebooks/    Colab notebook for the OpenMP, MPI and CUDA runs
  results/colab/  output of that notebook on a Tesla T4 (ctest log, benchmark outputs, environment)
  docs/
    course_outlines/   the six course outlines (PDF)
    reports/           full_syllabus_checklist.md, course_mapping.md, validation_report.md, studies
    architecture/ compiler/ microarchitecture/ parallel/ hdl/   one note per topic
    literature/        reading notes per course area
    history/           phase-by-phase roadmap and the 0.11 audit
  scripts/, tools/     benchmark, HDL and toolchain-comparison helpers
```

## Building and running

Requirements: CMake 3.25+, a C++20 compiler, Python 3, Flex and Bison. Optional: Icarus Verilog and
Yosys (HDL tests), an OpenMP compiler, MPI, CUDA, clang.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && ctest --output-on-failure
```

A few entry points (more in each `docs/` note):

| task | command |
| --- | --- |
| compile and run | `nexusc compile prog.nx -S --regalloc linear-scan -o prog.s` then `mips-sim run prog.s --mode pipeline --cache assoc --stats` |
| out-of-order core | `mips-sim run prog.s --mode advanced --scheduler tomasulo --issue-width 4 --predictor 2bit` |
| compare ISAs | `nexusc isa prog.nx` |
| grammars and automata | `nexusc grammar examples/grammars/lvalue.g lalr "* id = id"`, `nexusc regex '(a|b)*abb'` |
| type inference | `nexusc infer "fun f -> fun x -> f (f x)"` |
| SSA / SCCP | `nexusc analysis sccp tests/programs/constant_branches.nx` |
| polyhedral | `nexusc poly examples/loops/wavefront.loop skew 2 1 1 interchange 1 2` |
| arithmetic | `mips-sim arith float 0.1 add 0.2`, `mips-sim arith booth -3 5 4` |
| Verilog CPU | `mips-sim encode prog.s -o prog.hex`, then see `docs/hdl/pipelined_cpu.md` |
| GPU / manycore | `mips-sim run examples/gpu/strided.s --mode simt --threads 64`, `mips-sim run examples/parallel/spmd_sum.s --mode parallel --cores 64 --interconnect mesh` |

## Status and what remains

Version 1.0.0 is complete for its purpose: the six course outlines are covered and tested. Possible
extensions:
- run the Verilog CPU on an FPGA board;
- add thread blocks and shared memory to the SIMT model;
- add tiled code generation to the polyhedral tool.

## Citation and license

George David Tsitlauri, University of Thessaly, 2026. MIT License (`LICENSE`); citation metadata in
`CITATION.cff`.
