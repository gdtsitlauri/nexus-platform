# Validation Report (version 3.1.0)

## Environment

- Date: 2026-10-30 (version 3.1.0; the 2.0.0 run was on 2026-10-14, 1.0.0 on 2026-10-05)
- Host: Windows 11 Pro, AMD Ryzen 5 5600H
- Toolchain: CMake 4.4.4, Ninja 1.13.2, Clang (zig 0.17 c++), win_flex/win_bison 2.5.25, Python 3.13,
  Icarus Verilog and Yosys from the OSS CAD Suite (2026-10-14)
- Build: `cmake -S . -B build -G Ninja && cmake --build build` (no errors)

## Full Suite

```bash
cd build && ctest --output-on-failure -j8
```

78 tests: 77 passed, 1 skipped (`nexus_toolchain_compare`, `clang` not in PATH). The OpenMP and MPI benchmark
tests are not registered on this host because no OpenMP-capable compiler and no `mpicxx` were found;
`nexus_gpu_optional` passes through its CUDA-disabled path.

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
| hash table | `std::unordered_map` | 20,000 random operations | equal |
| Warshall closure, Tarjan SCCs, Kahn order | worklist reachability, mutual reachability | 200 random graphs | equal |
| attribute grammars | direct binary conversion; dependency-graph evaluation | 500 random binary numbers; declarations | equal; one-pass = graph on L-attributed grammars |
| simplex and branch and bound | exhaustive search | 150 random integer programs + 4 textbook LPs | equal |
| hyperplane schedules (ILP) | brute force over small integer vectors; execution of the transformed nest | 120 random dependence sets; 4 nests | equal optimum; legal and verified |
| symbolic unrolling with a remainder loop | the original loop, compiled and run | 160 random loops | equal results |
| modulo schedules | every dependence and resource constraint | 2 examples + 400 random loop bodies | all valid; II = MII in 396/400 |
| vector processor | scalar DAXPY; convoy counts | n = 1-257, 1-4 lanes | equal results; 3 (chained) / 4 convoys per strip |
| multithreading policies | functional exit codes; one-thread equality | 1-4 threads, 2 miss penalties | equal results; textbook trade-off |
| page replacement | reference LRU stack; OPT lower bound; Belady's string | 300 random strings | equal; bounds and anomaly hold |
| virtual-memory cycles | sum of TLB, page-walk and disk costs | sequential writes over 12 pages | exact |
| non-blocking cache | blocking formula; prefetch and MSHR monotonicity | 1000-element stream | exact; as expected |
| message-passing collectives | closed-form costs | 2-16 nodes, 1 and 1000 words | exact |
| GPU block reduction | closed-form block sums; bank-conflict cycles | warp sizes 4, 8, 32 | equal; exact |
| multi-cycle Verilog CPU, hardwired and microprogrammed | functional simulator (exit code and instructions) | 16 runs x 2 control units | all equal, identical cycles |
| branch predictors in the Verilog pipelined CPU | functional simulator | 16 runs x 3 predictors | all equal; fewer mispredictions and cycles |
| the full flow (`nexus_full_flow`) | one compiled program on 10 CPU models x 2 allocators and on the Verilog CPU with its console; a timer interrupt on the Verilog CPU vs the pipeline simulator | 1 program + 1 interrupt program | identical console text, exit codes and interrupt count |

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
