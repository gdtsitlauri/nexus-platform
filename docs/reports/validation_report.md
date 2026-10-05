# Validation Report (version 1.0.0)

## Environment

- Date: 2026-10-05
- Host: Windows 11 Pro, 12-core CPU
- Toolchain: CMake 3.30.5, Ninja 1.12.1, Clang 22 (zig c++), win_flex/win_bison 2.5.25, Python 3.12,
  Icarus Verilog and Yosys from the OSS CAD Suite
- Build: `cmake -S . -B build -G Ninja && cmake --build build` (150 targets, no errors)

## Full Suite

```bash
cd build && ctest --output-on-failure -j8
```

65 tests: 64 passed, 1 skipped (`nexus_toolchain_compare`, `clang` not in PATH). The OpenMP and MPI
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

## Defects Found In The 0.11 Code

1. Pipeline with caches returned wrong results for programs with calls: an instruction held in ID/EX
   during a memory freeze kept stale operands, and the load-use stall path could drop a frozen memory
   access. Fixed and covered by the differential test.
2. Passing a row of a two-dimensional array used the wrong address in the MIPS back end. Fixed and
   covered by `tests/programs/array_rows.nx`.

The 0.11 test suite did not run any compiled program through the cached pipeline or pass array rows,
which is why neither defect was visible before.
