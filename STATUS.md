# Nexus Status

## Snapshot

- Version: 3.0.0 (2026-10-30); 2.0.0 was released on 2026-10-14, 1.0.0 on 2026-10-05
- Scope: the six course outlines in `docs/course_outlines/` (NEY613, NEY709, NEY221, EY321, NEY606,
  NEY704); every topic is implemented as code with an automated test, except history, taxonomy and
  reading-list topics, which are notes (see `docs/reports/full_syllabus_checklist.md`)
- Build: CMake 4.4 + Ninja, C++20; verified with Clang (zig 0.17 toolchain) on Windows 11
- Tests: 73 CTest tests on Windows with Icarus Verilog and Yosys (OSS CAD Suite) installed: 72 passed,
  1 skipped (`nexus_toolchain_compare`, needs `clang` on the PATH); the OpenMP and MPI benchmark tests are
  not registered when those toolchains are absent
- Colab runs (Tesla T4 with CUDA, and Linux/GCC) were made with version 2.0.0 and are kept in
  `results/colab/`; they include tests of the four courses that 3.0.0 removed
- HDL: Icarus Verilog for the testbench suites, the RTL co-simulation and the I/O system; Yosys for synthesis

## What 3.0.0 Changed

| area | change | test |
| --- | --- | --- |
| scope | the project now covers six courses; Embedded Systems (NEY604), VLSI Design (NEY705), Logic Design (EY121) and Operating Systems (EY517) were removed with their code, tests, docs and outlines | - |
| the flow | one program through the six courses: compiler, SSA/SCCP, MIPS32 machine code, every CPU model, the Verilog CPU with a polled console and a timer interrupt on its I/O bus | `nexus_full_flow` |
| NEY613 | hash tables and symbol tables, tree traversals, DFS/BFS, topological order, SCCs, transitive closure | `nexus_foundations_test` |
| NEY613 | attribute grammars with inherited attributes: dependency graphs, L-attributed test, one-pass evaluation | `nexus_attribute_grammar_test` |
| NEY709 | polyhedral transformations by linear programming: exact simplex, branch and bound, hyperplane schedules | `nexus_lp_schedule_test` |
| NEY709 | symbolic unrolling by any factor with a remainder loop; loop detection for `while (n > i)` | `nexus_remainder_unroll_test` |
| EY321 | console and timer devices on the Verilog CPU's I/O bus | `nexus_full_flow` |
| NEY606 | software pipelining by iterative modulo scheduling | `nexus_modulo_schedule_test` |
| NEY704 | coarse-grained, fine-grained and simultaneous multithreading | `nexus_multithreading_test` |
| NEY704 | vector processor with strip-mining, chaining, lanes and strided access | `nexus_vector_test` |

## What 2.0.0 Added Over 1.0.0

| area | addition | test |
| --- | --- | --- |
| the flow | one NexusLang program through compiler, all CPU models, SoC RTL, gate level and FPGA; control unit through the chip-design tools; kernel against the scheduling library | `nexus_full_flow` |
| EY121 logic design | minimisation, BDD, SAT, codes, gate-level latches, FSM minimisation/analysis/synthesis, Verilog blocks | `nexus_logic_test`, `nexus_eda_cli`, `nexus_hdl_logic_comb`, `nexus_hdl_logic_seq` |
| NEY705 VLSI | switch-level CMOS, layout and DRC, logical effort, mapping, STA, power, ATPG, partitioning, floorplanning, placement, routing | `nexus_vlsi_test`, `nexus_eda_cli` |
| EY517 operating systems | schedulers, model checker, deadlock, memory management, disks, file system, kernel on the CPU, POSIX lab | `nexus_os_test`, `nexus_os_cli`, `nexus_posix_lab` |
| NEY604 embedded systems | SoC with UART, timer interrupts and GPIO; cross-level checks; ECP5 bitstream; real-time scheduling, SDF, Petri nets | `nexus_soc_levels`, `nexus_soc_fpga`, `nexus_embedded_test`, `nexus_embedded_rt_cli` |

## What 1.0.0 Added Over 0.11

| area | addition | test |
| --- | --- | --- |
| compiler back end | linear-scan register allocation | `nexus_cross_model_differential` |
| ISA comparison | stack (JVM-like), accumulator and register-memory (IA-32-like) back ends with interpreters | `nexus_isa_styles` |
| formal languages | regex/NFA/DFA/minimal DFA, FIRST/FOLLOW, LL(1), LR(0)/SLR/LR(1)/LALR, Earley, S-attributed evaluation | `nexus_formal_test` |
| type systems | Hindley-Milner inference | `nexus_type_inference_test` |
| optimisation theory | SSA construction, SCCP | `nexus_ssa_test` |
| loop optimisation | polyhedral dependence analysis, unimodular transformations, FM code generation | `nexus_polyhedral_test` |
| microarchitecture | Tomasulo with ROB, renaming, CDB, store forwarding, RAS, deeper pipelines, 2-way SMT | `nexus_tomasulo_test` |
| arithmetic | bit-exact IEEE-754 binary32 soft float, integer encodings, Booth, non-restoring division, CLA, UTF-8 | `nexus_number_systems_test` |
| HDL | synthesisable 5-stage pipelined MIPS core, MIPS32 encoder, RTL/simulator co-simulation | `nexus_hdl_pipeline_cosim` |
| parallel | SIMT GPU model, mesh and ring NoC, SPMD manycore (1-64 cores), asymmetric cores | `nexus_simt_test`, `nexus_manycore` |
| portability | Python HDL runner, optional OpenMP/MPI, Windows-friendly integration tests | full `ctest` on Windows |

## Defects Found and Fixed In Existing Code

1. **Pipeline with caches gave wrong results.** With `--cache direct|assoc`, `factorial` returned 0
   instead of 120 and recursive programs failed with out-of-bounds accesses. Cause: an instruction
   held in ID/EX during a multi-cycle memory freeze kept the operand values it read at decode, while
   its producer drained through write-back and left the forwarding window. A second path let the
   load-use stall overwrite a frozen EX/MEM register. Both are fixed in `src/sim/pipeline/src/model.cpp`.
2. **Passing a row of a 2-D array used the wrong address** (`row_sum(grid[1])` read row 0 data
   scaled wrongly; a test program returned 125 instead of 270). The MIPS back end did not scale a
   partial index by the size of the selected sub-array. Fixed in `codegen.cpp`.

Both are covered by `nexus_cross_model_differential` (`tests/programs/array_rows.nx` and every
program on the cached pipeline).

## Intentional Limits

- the simulators are educational models: the Tomasulo core is trace-driven, the SIMT model has no
  thread blocks or shared memory, and cycle counts are model metrics, not hardware measurements;
- the HDL core and SoC are verified in simulation (RTL and gate level) and placed and routed on an
  ECP5 at 6.8 MHz (single-cycle divider); no board pin constraints and no board test;
- the VLSI tools are teaching-scale (global routing only, no GDSII, delays in logical-effort units);
- the POSIX lab programs run only on a POSIX system (they passed on Colab Linux, not on the Windows host);
- NexusLang has `int`, `bool` and arrays only (no floating point, pointers or globals);
- OpenMP, MPI and CUDA demos need their toolchains; they were run on Colab, not on the Windows host.

## Portability Fix From The Colab Run

`nexus_type_inference_test` failed under GCC: the inferred types were correct but their variables
were named in a different order (`('c -> 'a) -> ...` instead of `('a -> 'b) -> ...`). The printer built
`show(a) + " -> " + show(b)`, and C++ leaves the evaluation order of the two calls unspecified (GCC
evaluated the right side first). It now prints the two sides in separate statements.
