# Validation Report

This report records the completed gap-closure audit against the current Phase 11 repository state.

## Audit Context

- Date: 2026-04-22
- Build tree used: existing `build/`
- Validation mode: low-memory only
- Build result: `ninja -j2` succeeded with `ninja: no work to do.`
- Full-suite result: `ctest --output-on-failure -j1` passed, 56/56 tests
- Companion examiner report: `docs/reports/repository_wide_truth_audit.md`

## Key Validation Commands Executed

```bash
cd build
ninja -j2
ctest --output-on-failure -j1 -R '^nexus_fpu_lite_test$'
ctest --output-on-failure -j1 -R '^(nexus_memory_cache_test|nexus_pipeline_memory_system_test|nexus_phase10_cli)$'
ctest --output-on-failure -j1 -R '^nexus_experimental_parse_test$'
ctest --output-on-failure -j1 -R '^nexus_advanced_model_test$'
ctest --output-on-failure -j1 -R '^(nexus_affine_analysis_test|nexus_unroll_pass_test)$'
ctest --output-on-failure -j1 -R '^nexus_gpu_optional$'
ctest --output-on-failure -j1 -R '^(nexus_hdl_cpu_slice|nexus_hdl_all)$'
ctest --output-on-failure -j1
./parallel-bench --gpu --build-dir ./build --repo-root .
ctest --output-on-failure -j1
```

## Focused Slice Results

| Slice | Classification | Validation evidence | Result |
| --- | --- | --- | --- |
| FPU-lite | experimentally implemented | `nexus_fpu_lite_test` | 1/1 passed |
| L1+L2 hierarchy | experimentally implemented | `nexus_memory_cache_test`, `nexus_pipeline_memory_system_test`, `nexus_phase10_cli` | 3/3 passed |
| Flex/Bison LR parser path | experimentally implemented | `nexus_experimental_parse_test`, `nexus_phase10_cli` cross-coverage | passed |
| scoreboard scheduler | experimentally implemented | `nexus_advanced_model_test`, `nexus_phase10_cli` cross-coverage | passed |
| affine/locality compiler slice | experimentally implemented | `nexus_affine_analysis_test`, `nexus_unroll_pass_test`, `nexus_phase10_cli` cross-coverage | 2/2 focused tests passed |
| stronger GPU demo | experimentally implemented | `nexus_gpu_optional`, direct `parallel-bench --gpu` wrapper check | passed with clean CUDA-disabled skip |
| HDL CPU slice | experimentally implemented | `nexus_hdl_cpu_slice`, `nexus_hdl_all` | 2/2 passed |

## Current Topic Classification

| Topic family | Classification | Notes |
| --- | --- | --- |
| handwritten frontend, semantics, IR, MIPS backend, functional/single/multi/pipeline execution, CLI flows | fully implemented | buildable and covered throughout the 56-test suite |
| FPU-lite, L1+L2 memory hierarchy, Flex/Bison LR path, scoreboard scheduler, affine/locality slice, stronger GPU demo, HDL CPU slice, parallel/coherence/consistency-lite | experimentally implemented | real code paths with focused tests and bounded interfaces |
| Tomasulo/reservation stations, generalized ambiguity-supporting parsing, deeper polyhedral theory, ISA-comparison material, literature synthesis | documented with worked examples | repository documentation remains explicit where theory exceeds executable scope |
| full synthesizable HDL CPU, mandatory device-backed GPU success, reorder-buffer out-of-order core, industrial SSA/register allocation, large manycore research platform | still outside bounded scope | final repository states these limits directly |

## GPU Validation Reality

The current CPU-only build does not contain `build/bin/gpu-bench`. The optional wrapper still
validated correctly and produced:

```text
suite=gpu kernel=vector-add status=skipped reason=cuda-disabled
```

That is a passing result for the bounded final audit because CUDA is explicitly optional.

## Command Surface Spot-Checks

The final polish pass also spot-checked the main documented entry points from the repository root:

```bash
./build/bin/nexusc --help
./build/bin/mips-sim --help
./build/bin/nexusc compile examples/source_lang/factorial.nx -S -o /tmp/nexus_factorial.s
./build/bin/mips-sim run /tmp/nexus_factorial.s --mode functional --stats
./build/bin/mips-sim fp-demo 1.5 0.25
./parallel-bench --all --build-dir ./build --repo-root .
./hdl-test all
```

Each completed successfully in the validated CPU-only environment.

## Full-Suite Summary

- total tests: 56
- passed: 56
- failed: 0
- total real time in the final serial rerun: 3.19 sec

## Known Limits After Audit

- the repository fully covers the course material only in bounded educational/research-grade form, not as an industrial product
- the stronger GPU path is optional and was validated in clean-skip mode rather than on a CUDA device in this audit
- HDL coverage now includes a CPU slice, but not a full synthesizable processor
- advanced scheduling now includes a bounded scoreboard experiment, but not Tomasulo or reorder-buffer execution
