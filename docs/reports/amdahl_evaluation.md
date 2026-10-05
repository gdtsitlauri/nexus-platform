# Phase 8 Amdahl Evaluation

## Scope

This report captures the tiny Phase 8 advanced-architecture study. The workloads are intentionally
small and deterministic:

- one compiled NexusLang loop benchmark: `arrays_and_loops`
- one hand-written branch-prediction loop: `tests/golden/advanced_branch_demo.s`
- one hand-written scheduling demo: `tests/golden/advanced_vliw_demo.s`

The supporting scripts are:

```bash
python3 scripts/run_advanced_experiments.py ./build/bin/nexusc ./build/bin/mips-sim . /tmp/nexus_adv.csv
python3 scripts/generate_amdahl_report.py /tmp/nexus_adv.csv /tmp/nexus_adv_summary.md /tmp/nexus_amdahl.md
```

## Predictor Comparison

| Configuration | Cycles | Branch predictions | Branch mispredictions |
| --- | ---: | ---: | ---: |
| advanced-static | 18 | 4 | 3 |
| advanced-2bit | 16 | 4 | 2 |

Interpretation:

- the 2-bit predictor learns the repeated taken loop outcome after the first miss
- the final loop exit still incurs a miss in this bounded example

## Scheduling Comparison

| Configuration | Cycles | IPC | Slot utilization |
| --- | ---: | ---: | ---: |
| advanced-inorder-width2 | 5 | 1.20 | 60.00% |
| advanced-vliw-lite | 4 | 1.50 | 75.00% |

Interpretation:

- the VLIW-lite scheduler improves bundle packing by reordering independent instructions
- the repository now also contains a separate bounded scoreboard scheduler, but it is not part of
  this original Phase 8 measurement table

## Pipeline Baseline Comparison

| Configuration | Cycles | CPI |
| --- | ---: | ---: |
| pipeline-baseline | 324 | 1.38 |
| advanced-width1 | 242 | 1.03 |
| advanced-width2 | 235 | 1.00 |

This is not a claim that the advanced sandbox is a drop-in replacement for the pipeline timing
model. Instead, it provides a lightweight experimental frame for asking “what if” questions about
branch prediction and issue width using the same bounded instruction subset.

## Amdahl Projection

- measured width-1 to width-2 speedup on `compiled_arrays`: `1.03x`
- effective accelerated fraction estimated with Amdahl’s Law for `k = 2`: `5.79%`
- projected speedup if the same accelerated fraction scaled to width 4: `1.05x`

The small projected gain is the important lesson here: the benchmark contains enough dependence and
control structure that extra width cannot help very much. Amdahl’s framing makes that bottleneck
visible.

## Limits Of This Phase-Specific Report

- tiny workloads only; this is an educational measurement set
- the report measures in-order and VLIW-lite scheduling only
- scoreboard exists elsewhere in the repository but is not benchmarked here
- the Tomasulo/ROB scheduler (added in 1.0.0) is measured in `docs/microarchitecture/tomasulo_rob_smt.md`
- this report does not attempt later L1+L2 or multicore experiments
- advanced-mode cycle counts are sandbox metrics, not hardware-validated timing numbers
