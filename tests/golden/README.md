# Golden Tests

Phase 6 extends deterministic golden-output fixtures across the functional, single-cycle,
multi-cycle, and pipeline execution paths.

Current fixtures:

- `factorial_stdout.txt`: exact expected functional summary for compiling and running `factorial.nx`
- `trace_demo.s`: tiny hand-written assembly program for trace-mode validation
- `trace_demo.stdout.txt`: exact expected functional trace output for `trace_demo.s`
- `single_cycle_trace.stdout.txt`: exact expected single-cycle trace output for `trace_demo.s`
- `multi_cycle_trace.stdout.txt`: exact expected multi-cycle microcode trace output for `trace_demo.s`
- `pipeline_trace.stdout.txt`: exact expected pipeline trace output for `trace_demo.s`
- `pipeline_branch_demo.s`: tiny hand-written branch program for pipeline timeline validation
- `pipeline_timeline.stdout.txt`: exact expected pipeline timeline output for `pipeline_branch_demo.s`
