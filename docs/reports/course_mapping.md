# Course Mapping

This report maps the final Nexus repository to the six required educational domains using the final
audit vocabulary.

## Audit Inputs

The mapping was derived from:

- project-contract files: `README.md`, `ROADMAP.md`, `STATUS.md`, `ARCHITECTURE.md`, `CHANGELOG.md`
- implementation evidence in `src/`, `tests/`, `examples/`, `scripts/`, `tools/`, `benchmarks/`, and `cmake/`
- current report/checklist files
- six syllabus PDFs audited on 2026-04-22

No separate standalone "mega-project brief" file was present in the repository snapshot. For that
axis of the audit, the project-contract files above were treated as the maintained statement of
project intent and bounded scope.

## Status Vocabulary

- fully implemented
- experimentally implemented
- documented with worked examples
- still outside bounded scope

## Domain Summary

| Domain | Overall status | Strongest evidence | Remaining bounded-scope note |
| --- | --- | --- | --- |
| 1. Principles of Computer Operation | experimentally implemented | MIPS subset docs, backend, functional execution, arithmetic helpers, HDL arithmetic modules, FPU-lite demo, tests | broader industrial floating-point and synthesis depth remain outside bounded scope |
| 2. Computer Organization | experimentally implemented | functional/single/multi/pipeline stack, L1+L2 cache hierarchy, I/O/DMA/interrupt support, HDL control/pipeline-register/CPU-slice modules, tests | full industrial memory-system and full CPU synthesis remain outside bounded scope |
| 3. Compilers | fully implemented | handwritten lexer/parser, AST, semantics, IR, MIPS backend, compile/run CLI flows, end-to-end tests | generated production parsing is not the default path |
| 4. Computer Architecture | experimentally implemented | advanced predictor support, width experiments, VLIW-lite, bounded scoreboard scheduler, Amdahl evaluation, literature notes | Tomasulo, reorder buffers, and industrial out-of-order machinery remain outside bounded scope |
| 5. Advanced Compiler Topics | experimentally implemented | experimental parallel parse, Flex/Bison LR parser path, symbolic analysis, affine analysis/strip-mining, alias and interprocedural summaries, toolchain comparison, compiler reading notes | industrial SSA/polyhedral/generalized parsing depth remains outside bounded scope |
| 6. Parallel Systems and Parallel Programming | experimentally implemented | `src/sim/parallel/`, parallel tests, OpenMP/MPI/SIMD binaries, optional CUDA demo path, `parallel-bench`, interconnect docs | mandatory device-backed GPU success and large manycore studies remain outside bounded scope |

## Relevant Topic Crosswalk

| Relevant topic | Classification | Evidence |
| --- | --- | --- |
| FPU-lite | experimentally implemented | `src/common/src/fpu_lite.cpp`, `tests/unit/fpu_lite_test.cpp` |
| L1+L2 hierarchy | experimentally implemented | `src/sim/memory/`, `tests/unit/memory_cache_test.cpp`, `tests/unit/pipeline_memory_system_test.cpp`, CLI coverage |
| Flex/Bison LR parser path | experimentally implemented | `src/compiler/experimental_parallel_parsing/`, `tests/unit/experimental_parse_test.cpp` |
| scoreboard scheduler | experimentally implemented | `src/sim/advanced/src/model.cpp`, `tests/unit/advanced_model_test.cpp` |
| affine/locality compiler slice | experimentally implemented | `src/compiler/analysis/src/affine_analysis.cpp`, `src/compiler/passes/src/affine_stripmine.cpp`, focused tests |
| stronger GPU demo | experimentally implemented | `benchmarks/gpu_optional/gpu_optional_bench.cu`, `tests/integration/gpu_optional_test.py` |
| HDL CPU slice | experimentally implemented | `src/hdl/cpu_slice/nexus_cpu_slice.v`, `src/hdl/cpu_slice/cpu_slice_tb.v`, HDL tests |
| core handwritten compiler path | fully implemented | `nexusc`, `tests/unit/`, `tests/integration/nexusc_cli_test.py` |
| Tomasulo/reservation stations and generalized ambiguity-supporting parsing theory | documented with worked examples | `docs/microarchitecture/advanced_scheduling.md`, `docs/compiler/generalized_and_parallel_parsing.md` |
| full synthesizable HDL CPU, mandatory GPU execution, industrial SSA/register allocation, reorder-buffer OOO core | still outside bounded scope | final-scope documentation and paper |

## Companion Audit

The detailed examiner-style breakdown lives in `docs/reports/repository_wide_truth_audit.md`.

## Mapping Rule

Nexus now fully covers the course set only in bounded educational/research-grade form. That means
the domains are satisfied by a deliberate mix of fully implemented core paths, experimentally
implemented extensions, and worked-example documentation, while the final reports still name the
topics that remain outside bounded scope.
