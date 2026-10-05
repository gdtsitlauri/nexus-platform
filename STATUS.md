# Nexus Status

## Snapshot

- Version: 1.0.0 (2026-10-05)
- Scope: every topic of the six course outlines in `docs/course_outlines/` is implemented as code
  with an automated test, except three reading/history topics (see
  `docs/reports/full_syllabus_checklist.md`)
- Build: CMake + Ninja, C++20; verified with Clang 22 (zig toolchain) on Windows 11
- Tests: 65 CTest tests, 64 passed and 1 skipped (`nexus_toolchain_compare` needs `clang` in PATH);
  the OpenMP and MPI benchmark tests are not registered when those toolchains are absent
- Colab (Tesla T4, CUDA 13.0, GCC 13.3, OpenMPI, Yosys): 68 CTest tests including OpenMP, MPI, the
  bench wrapper and the CUDA demo; 66 passed, 1 skipped (clang), 1 failed (type printing, fixed below and
  re-verified with Clang; not yet re-run on Colab) (`results/colab/`)
- HDL: Icarus Verilog for the 9 testbench suites and the RTL co-simulation, Yosys for synthesis

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
- the HDL core is verified in simulation and synthesised generically, not placed and routed on an FPGA;
- NexusLang has `int`, `bool` and arrays only (no floating point, pointers or globals);
- OpenMP, MPI and CUDA demos need their toolchains; they were run on Colab, not on the Windows host.

## Portability Fix From The Colab Run

`nexus_type_inference_test` failed under GCC: the inferred types were correct but their variables
were named in a different order (`('c -> 'a) -> ...` instead of `('a -> 'b) -> ...`). The printer built
`show(a) + " -> " + show(b)`, and C++ leaves the evaluation order of the two calls unspecified (GCC
evaluated the right side first). It now prints the two sides in separate statements.
