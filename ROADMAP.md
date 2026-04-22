# Nexus Roadmap

## Current State

- Current phase: Phase 11 completed
- Final validation status: verified on 2026-04-22
- Repository state: bounded educational/research-grade platform with a passing 56/56 low-memory test run

## Phase Sequence

1. Phase 1: repository bootstrap and build scaffolding
2. Phase 2: language spec, frontend, AST, semantics
3. Phase 3: IR, CFG, dominators, baseline analysis
4. Phase 4: MIPS backend and functional execution
5. Phase 5: single-cycle and multi-cycle organization models
6. Phase 6: 5-stage pipeline, hazards, forwarding, branch handling
7. Phase 7: memory/cache/I/O/DMA/interrupts and metrics
8. Phase 8: advanced architecture sandbox and Amdahl evaluation
9. Phase 9: advanced compiler prototypes and toolchain comparison
10. Phase 10: multicore/parallel simulation, coherence/consistency-lite, synchronization, OpenMP/MPI/SIMD integration
11. Phase 11: HDL modules/testbenches, optional GPU demos, literature completion, and final validation

## Final Gap-Closure Status

| Topic | Status | Exit evidence |
| --- | --- | --- |
| FPU-lite | experimentally implemented | `src/common/src/fpu_lite.cpp`, `nexus_fpu_lite_test` |
| L1+L2 hierarchy | experimentally implemented | `src/sim/memory/`, `nexus_memory_cache_test`, `nexus_pipeline_memory_system_test`, CLI coverage |
| Flex/Bison LR parser path | experimentally implemented | `src/compiler/experimental_parallel_parsing/`, `nexus_experimental_parse_test` |
| scoreboard scheduler | experimentally implemented | `src/sim/advanced/src/model.cpp`, `nexus_advanced_model_test` |
| affine/locality compiler slice | experimentally implemented | `src/compiler/analysis/src/affine_analysis.cpp`, `src/compiler/passes/src/affine_stripmine.cpp`, focused tests |
| stronger GPU demo | experimentally implemented | optional CUDA demo source plus validated clean skip path |
| HDL CPU slice | experimentally implemented | `src/hdl/cpu_slice/`, `nexus_hdl_cpu_slice`, `nexus_hdl_all` |
| core compiler/simulator/CLI baseline | fully implemented | existing end-to-end stack and full-suite coverage |
| broader theory-only material such as Tomasulo, generalized ambiguity-supporting parsing, and industrial polyhedral scheduling | documented with worked examples | `docs/` and `docs/reports/` |
| full synthesizable CPU, mandatory GPU execution, industrial out-of-order core, industrial optimizer depth | still outside bounded scope | explicit final-scope limits |

## Final Verdict Frame

The roadmap is complete in bounded educational/research-grade form. The repository now covers the
course domains through a deliberate mix of fully implemented core flows, experimentally implemented
extensions, and documented worked examples, while still naming the topics that remain outside final
bounded scope.

## Final Submission Set

- `README.md`
- `STATUS.md`
- `ROADMAP.md`
- `docs/reports/final_nexus_system_paper.md`
- `docs/reports/validation_report.md`
- `docs/reports/course_mapping.md`
- `docs/reports/full_syllabus_checklist.md`
- `docs/reports/repository_wide_truth_audit.md`

## Post-Project Future Work

- full synthesizable HDL CPU integration
- stronger heterogeneous and device-backed GPU evaluation
- Tomasulo/reorder-buffer out-of-order execution experiments
- industrial SSA, register allocation, and deeper optimization infrastructure
- larger manycore and richer coherence studies
