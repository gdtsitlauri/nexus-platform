# Branch Prediction

Nexus now exposes branch prediction in two layers:

- the Phase 6 pipeline uses bounded static predictors
- the Phase 8 advanced sandbox adds a small dynamic `2bit` predictor experiment

## Implemented Predictors

Pipeline mode supports:

- `static-not-taken`
- `static-taken`
- `static-btfnt`

Advanced mode supports:

- `static-not-taken`
- `2bit`

The CLI surface is:

```bash
mips-sim run out.s --mode pipeline --predictor static-not-taken
mips-sim run out.s --mode pipeline --predictor static-taken
mips-sim run out.s --mode pipeline --predictor static-btfnt
mips-sim run out.s --mode advanced --predictor static-not-taken
mips-sim run out.s --mode advanced --predictor 2bit
```

## Scope

Prediction is currently applied to:

- `beq`
- `bne`

Unconditional jumps (`j`, `jal`, `jr`) remain explicit control redirects rather than predictor-driven
events.

## Pipeline Behavior

The educational pipeline resolves conditional branches in EX using forwarded operands when necessary.

When prediction is correct:

- the pipeline continues without flushing younger instructions

When prediction is wrong:

- PC is redirected to the actual target
- younger wrong-path instructions are flushed
- branch misprediction counters increase

This penalty is visible in cycle counts, trace events, and timeline markers.

## Advanced Sandbox Behavior

The advanced sandbox reuses correct architectural execution from the loaded instruction stream, then
layers a deterministic experimental predictor model on top of the dynamic trace.

The `2bit` predictor uses a tiny per-PC saturating counter:

- initial state: weakly not taken
- taken update: move one step toward strongly taken
- not-taken update: move one step toward strongly not taken
- predict taken when the counter is `2` or `3`

Wrong predictions add bounded speculative flush cycles to the advanced summary. This is a teaching
model for speculation penalties, not a reorder-buffer implementation.

## Worked Comparisons

Pipeline branch demo:

```text
addiu $t0, $zero, 1
beq   $t0, $t0, taken
addiu $v0, $zero, 0
taken:
addiu $v0, $zero, 7
jr    $ra
```

Observed pipeline behavior:

- `static-not-taken`: 1 prediction, 1 misprediction, 1 flushed instruction, 9 cycles
- `static-taken`: 1 prediction, 0 mispredictions, 0 flushes, 8 cycles

Advanced loop demo:

```text
addiu $t0, $zero, 0
addiu $t1, $zero, 4
loop:
addiu $t0, $t0, 1
bne   $t0, $t1, loop
addu  $v0, $t0, $zero
jr    $ra
```

Observed advanced behavior:

- `static-not-taken`: 4 predictions, 3 mispredictions, 18 cycles
- `2bit`: 4 predictions, 2 mispredictions, 16 cycles

These comparisons are covered by `tests/unit/pipeline_model_test.cpp`,
`tests/unit/advanced_predictor_test.cpp`, and `tests/golden/advanced_golden_test.py`.

## Trace Format

Pipeline mispredictions appear in trace output like this:

```text
mispredict(beq -> pc=3)
```

Advanced-mode speculative penalties appear as bounded flush-cycle entries such as:

```text
trace[advanced]: cycle=5 scheduler=inorder issue_width=1 packet=[] events=spec-flush(1/2)
```

## Limitations

- no BTB, return-address stack, or global-history structure
- no predictor-aware fetch queue
- no scoreboard/Tomasulo-style speculative wakeup flow
- no branch predictor beyond the bounded static set and the small 2-bit counter experiment
