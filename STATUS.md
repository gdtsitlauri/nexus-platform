# Nexus Status

## Snapshot

- Date: 2026-04-22
- Active phase: Phase 11 - HDL, literature, and final validation
- Build status: verified from the existing `build/` tree
- Full test status: verified, 56/56 passed
- Runtime output directory: `build/bin`

## Current Classification

| Topic | Status | Evidence |
| --- | --- | --- |
| core compiler and CPU simulator baseline | fully implemented | `nexusc`, `mips-sim`, unit/integration/golden coverage |
| FPU-lite demo | experimentally implemented | `nexus_fpu_lite_test`, `mips-sim fp-demo` |
| L1+L2 hierarchy | experimentally implemented | `nexus_memory_cache_test`, `nexus_pipeline_memory_system_test`, `nexus_phase10_cli` |
| Flex/Bison LR parser path | experimentally implemented | `nexus_experimental_parse_test`, `nexus_phase10_cli` |
| scoreboard scheduler | experimentally implemented | `nexus_advanced_model_test`, `nexus_phase10_cli` |
| affine/locality compiler slice | experimentally implemented | `nexus_affine_analysis_test`, `nexus_unroll_pass_test`, `nexus_phase10_cli` |
| stronger GPU demo | experimentally implemented | `nexus_gpu_optional`, `parallel-bench --gpu` clean skip in CPU-only build |
| HDL CPU slice | experimentally implemented | `nexus_hdl_cpu_slice`, `nexus_hdl_all` |
| Tomasulo/reservation stations and generalized ambiguity-supporting parsing | documented with worked examples | `docs/microarchitecture/advanced_scheduling.md`, `docs/compiler/generalized_and_parallel_parsing.md` |
| full synthesizable HDL CPU, mandatory device-backed GPU execution, reorder-buffer out-of-order engine, industrial SSA/register allocation | still outside bounded scope | final bounded-scope documentation |

## Exact Commands Used In This Audit

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
```

## Intentional Limits

- CUDA is optional only
- HDL modules and the CPU slice are bounded teaching artifacts rather than a full CPU implementation
- scoreboard, L1+L2, and affine/locality work are educational prototypes with focused tests
- several large topics remain documentation-backed rather than industrially executable

## Final Documentation Set

- overview and usage: `README.md`
- final paper: `docs/reports/final_nexus_system_paper.md`
- validation record: `docs/reports/validation_report.md`
- course mapping: `docs/reports/course_mapping.md`
- detailed checklist: `docs/reports/full_syllabus_checklist.md`
- repository-wide truth audit: `docs/reports/repository_wide_truth_audit.md`
