# Validation Report (version 2.0.0)

## Environment

- Date: 2026-10-14 (version 2.0.0; the 1.0.0 run was on 2026-10-05)
- Host: Windows 11 Pro, 12-core CPU
- Toolchain: CMake 3.30.5, Ninja 1.12.1, Clang 22 (zig c++), win_flex/win_bison 2.5.25, Python 3.12,
  Icarus Verilog, Yosys, nextpnr-ecp5 and ecppack from the OSS CAD Suite
- Build: `cmake -S . -B build -G Ninja && cmake --build build` (150 targets, no errors)

## Full Suite

```bash
cd build && ctest --output-on-failure -j8
```

78 tests: 76 passed, 2 skipped (`nexus_toolchain_compare`, `clang` not in PATH; `nexus_posix_lab`, not a
POSIX system). The new code builds without warnings under `-Wall -Wextra -Wconversion
-Wsign-conversion`. The OpenMP and MPI
benchmark tests are not registered on this host because no OpenMP-capable compiler and no `mpicxx`
were found; `nexus_gpu_optional` passes through its CUDA-disabled path.

## Cross-Checks Against Independent References

| check | reference | scale | result |
| --- | --- | --- | --- |
| every CPU model and both code generators | functional interpreter | 12 programs x 2 allocators x 12 models (288 runs) | all equal |
| Verilog pipelined core | functional simulator (exit code and retired instructions) | 16 runs | all equal |
| ISA styles | each other and MIPS | 8 programs x 5 machines | all equal |
| soft float add/sub/mul/div | host FPU | 2,000,000 random pairs + 324 special pairs | bit-exact |
| Booth, non-restoring division, CLA | native arithmetic | 20,000 random cases | equal |
| regex NFA/DFA/minimal DFA | `std::regex` | 8 patterns x 400 strings | equal |
| LR/LL parsers with attributes | hand-computed values | 4 grammars | equal |
| Earley tree counts | Catalan numbers | up to 6 operands | equal |
| polyhedral transformations | execution of the original nest | 600 random cases + 4 examples | every legal case identical |
| Fourier-Motzkin dependence tests | enumeration of the concrete domain | 120 random nests | no missed dependence |
| SIMT kernels | closed form and warp size 1 | 64-96 threads | equal |
| SPMD reduction | closed form | 1-64 cores x 5 networks | equal |
| NAND/NOR-only rewriting, BDD canonicity and model counts | truth tables | 300 + 200 random expressions | equal |
| CDCL SAT solver | brute-force enumeration | 300 random 3-SAT formulas | equal |
| PLA row minimisation | brute-force search over all cubes; truth tables | 20 + 80 random multi-output functions | minimal and correct |
| Hamming SEC decoding | every single-bit error | 40 random words per size | all corrected |
| FSM minimisation | product-machine equivalence | 120 random machines | equivalent and minimal |
| synthesised FSM Verilog (behavioural and structural) | state-table simulation in Icarus Verilog | 2 machines x 4 encodings/flip-flops, 64 random inputs | equal |
| logical-effort path sizing | numerical optimisation | 20 random paths | equal (1e-5) |
| static CMOS complex gates | exhaustive switch-level simulation | 60 random pull-down networks | complementary, correct function |
| technology mapping | SAT miter; Yosys `sat -prove` on c17 and the control unit | 25 random circuits + 2 designs | proved equivalent |
| static timing | path enumeration | 25 random circuits | equal |
| stuck-at faults | random patterns + SAT ATPG | 6 random circuits | every fault detected or proved redundant |
| A* routing | Lee wave expansion | 40 random grids | same optimal cost, fewer expansions |
| Steiner trees | exact RSMT | 30 random nets | RSMT <= 1-Steiner <= MST <= 1.5 RSMT |
| SRT / SPN scheduling | other policies; exhaustive non-preemptive optimum | 200 random workloads | SRT minimal, SPN optimal |
| Banker's safety | enumeration of all orders | 300 random states | equal |
| page replacement | OPT lower bound, LRU stack property | 200 random strings | hold |
| response-time analysis, EDF test | simulation over the hyperperiod | 400 random task sets | equal |
| SoC programs | C++ pipeline model, RTL, gate-level netlist | 3 programs | identical exit codes, UART text, interrupt counts |
| the full flow (`nexus_full_flow`) | one compiled program on 10 CPU models x 2 allocators, SoC RTL, gate level; control unit mapped and proved; kernel vs RR Gantt | 1 program, 1 kernel | identical UART text and exit code; dispatch order = Gantt chart |
| ECP5 bitstream | read back with ecpunpack (device ID, 13294 frames, DONE) | 1 design | valid |

## Linux / GCC Run of Version 2.0.0 (Google Colab, 2026-10-14)

The 2.0.0 tree was built with GCC on Colab (Ubuntu, Icarus Verilog and Yosys from apt, OpenMPI and
OpenMP present, no nextpnr): 81 CTest tests, 79 passed, 2 skipped (`nexus_toolchain_compare`, no
clang; `nexus_soc_fpga`, no nextpnr). This includes `nexus_full_flow` (every stage except the FPGA),
`nexus_posix_lab`, the OpenMP and MPI benchmarks, the gate-level SoC run and
`nexus_type_inference_test` (the GCC evaluation-order fix of 1.0.0). With the Yosys version of Colab
the control unit maps to 260 cells (263 with the Yosys of the Windows host); the equivalence proof and
the routing (overflow 0) hold with both. The
first attempt exposed two headers that Clang's library included implicitly and GCC's does not
(`<limits>`, `<cmath>`); they are now included explicitly. Summary: `results/colab/linux_gcc_2.0.0.md`.

## Defects Found In The 0.11 Code

1. Pipeline with caches returned wrong results for programs with calls: an instruction held in ID/EX
   during a memory freeze kept stale operands, and the load-use stall path could drop a frozen memory
   access. Fixed and covered by the differential test.
2. Passing a row of a two-dimensional array used the wrong address in the MIPS back end. Fixed and
   covered by `tests/programs/array_rows.nx`.

The 0.11 test suite did not run any compiled program through the cached pipeline or pass array rows,
which is why neither defect was visible before.

## Toolchain-Dependent Runs (Google Colab)

`notebooks/NEXUS_Colab_OpenMP_MPI_CUDA.ipynb` on a Tesla T4 (CUDA 13.0, GCC 13.3, OpenMPI, CMake 3.31,
Icarus Verilog, Yosys), output in `results/colab/`:

- 68 CTest tests registered (OpenMP, MPI, bench wrapper and CUDA included); 66 passed, 1 skipped
  (`clang`), 1 failed: `nexus_type_inference_test`. It failed because of a GCC/Clang difference in
  evaluation order when naming type variables, which is now fixed (`STATUS.md`).
- OpenMP (2 threads, n = 1024): vector-add, reduction, matmul and branch-mix all `status=ok`.
- MPI (2 ranks, n = 1024): reduce and ring `status=ok`.
- CUDA: vector-add and reduction with GPU checksum equal to the CPU checksum (4096 / 4096, 253 / 253).
- Verilog co-simulation and Yosys synthesis also passed under Linux.
